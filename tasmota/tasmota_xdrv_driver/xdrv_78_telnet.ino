/*
  xdrv_78_telnet.ino - Telnet console support for Tasmota

  SPDX-FileCopyrightText: 2025 Theo Arends

  SPDX-License-Identifier: GPL-3.0-only
*/

#ifdef USE_TELNET
/*********************************************************************************************\
 * Telnet console support for a single connection
 *
 * Supported commands:
 *  Telnet                - Show telnet server state
 *  Telnet 0              - Disable telnet server
 *  Telnet 1              - Enable telnet server on port 23
 *  Telnet 23             - Enable telnet server on port 23
 *  Telnet 1, 192.168.2.1 - Enable telnet server and only allow connection from 192.168.2.1
 *  TelnetBuffer          - Show current input buffer size (default 256)
 *  TelnetBuffer 300      - Change input buffer size to 300 characters
 *  TelnetColor           - Show prompt, response and log colors
 *  TelnetColor 0         - Set all colors to default
 *  TelnetColor 1         - Set colors to defined colors
 *  TelnetColor 33,32,37  - Set prompt (yellow), response (green) and log (white) colors
 *
 * To start telnet at restart add a rule like
 *  on system#boot do backlog telnetcolor 33,32,36; telnet 1 endon
 * 
 * Supported ANSI Escape Color codes:
 *          Normal   Bright
 *  Black     30       90
 *  Red       31       91
 *  Green     32       92
 *  Yellow    33       93
 *  Blue      34       94
 *  Magenta   35       95
 *  Cyan      36       96
 *  White     37       97
 *  Default   39
\*********************************************************************************************/

#define XDRV_78                78

#ifndef TELNET_BUF_SIZE
#define TELNET_BUF_SIZE        256                   // Size of input buffer
#endif

#ifndef TELNET_COL_PROMPT
#define TELNET_COL_PROMPT      33                    // Yellow - ANSI color escape code
#endif
#ifndef TELNET_COL_RESPONSE
#define TELNET_COL_RESPONSE    32                    // Green - ANSI color escape code
#endif
#ifndef TELNET_COL_LOGGING
#define TELNET_COL_LOGGING     36                    // Cyan - ANSI color escape code
#endif

struct {
  WiFiServer *server = nullptr;
  WiFiClient  client;
  IPAddress   ip_filter;
  char       *buffer = nullptr;
  uint16_t    port;
  uint16_t    buffer_size;
  uint16_t    in_byte_counter;
  uint8_t     log_index;
  uint8_t     prompt;
  uint8_t     color[3];
  bool        ip_filter_enabled;
} Telnet;

/********************************************************************************************/

void TelnetLoop(void) {
  // check for a new client connection
  if ((Telnet.server) && (Telnet.server->hasClient())) {
    WiFiClient new_client = Telnet.server->available();

    AddLog(LOG_LEVEL_INFO, PSTR("TLN: Connection from %s"), new_client.remoteIP().toString().c_str());

    if (Telnet.ip_filter_enabled) {                  // Check for IP filtering if it's enabled
      if (Telnet.ip_filter != new_client.remoteIP()) {
        AddLog(LOG_LEVEL_INFO, PSTR("TLN: Rejected due to filtering"));
        new_client.stop();
      }
    }

    if (Telnet.client) {
      Telnet.client.stop();
    }
    Telnet.client = new_client;
    if (Telnet.client) {
      Telnet.client.printf("Tasmota %s %s (%s) %s\r\n", TasmotaGlobal.hostname, TasmotaGlobal.version, GetBuildDateAndTime().c_str(), GetDeviceHardware().c_str());
      Telnet.log_index = 0;                          // Dump start of log buffer for restart messages
      Telnet.prompt = 3;
    }
  }

  if (Telnet.client) {
    // Output latest log buffer data
    uint32_t index = Telnet.log_index;               // Dump log buffer
    char* line;
    size_t len;
    bool any_line = false;
    while (GetLog(TasmotaGlobal.seriallog_level, &index, &line, &len)) {
      if (!any_line) {
        any_line = true;
        if (3 == Telnet.prompt) {                    // Print linefeed for non-requested data
          Telnet.prompt = 2;                         // Do not print linefeed for any data and use log color
          Telnet.client.write("\r\n");
        }
      }
      // line = 14:49:36.123-017 MQTT: stat/wemos5/RESULT = {"POWER":"OFF"}
      uint32_t textcolor = Telnet.color[Telnet.prompt];
      uint32_t diffcolor = textcolor;
      if ((textcolor >= 30) && (textcolor <= 37)) {
        diffcolor += 60;                             // Highlight color
      }
      else if ((textcolor >= 90) && (textcolor <= 97)) {
        diffcolor -= 60;                             // Lowlight color
      }
      char* time_end = (char*)memchr(line, ' ', len);  // Find first word (usually 14:49:36.123-017)
      uint32_t time_len = time_end - line;
      Telnet.client.printf("\x1b[%dm", diffcolor);
      Telnet.client.write(line, time_len);
      Telnet.client.printf("\x1b[%dm", textcolor);
      Telnet.client.write(time_end, len - time_len -1);
      Telnet.client.write("\r\n");
    }
    if (any_line) {
      Telnet.client.write("\x1b[0m");                // Restore colors
      if ((0 == Telnet.log_index) || (Telnet.prompt != 2)) {
        Telnet.client.printf("\x1b[%dm%s:#\x1b[0m ", Telnet.color[0], TasmotaGlobal.hostname);  // \x1b[33m = Yellow, \x1b[0m = end color
        Telnet.prompt = 3;                           // Print linefeed for non-requested data
        while (Telnet.client.available()) { Telnet.client.read(); }  // Flush input
      }
      Telnet.log_index = index;
      return;
    }
    // Input keyboard data
    while (Telnet.client.available()) {
      yield();
      uint8_t in_byte = Telnet.client.read();
      if (isprint(in_byte)) {                        // Any char between 32 and 127
        if (Telnet.in_byte_counter < Telnet.buffer_size -1) {  // Add char to string if it still fits
          Telnet.buffer[Telnet.in_byte_counter++] = in_byte;
        }
      }
      else if (in_byte == '\n') {
        Telnet.buffer[Telnet.in_byte_counter] = 0;   // Telnet data completed
        TasmotaGlobal.seriallog_level = (Settings->seriallog_level < LOG_LEVEL_INFO) ? (uint8_t)LOG_LEVEL_INFO : Settings->seriallog_level;
        Telnet.client.write("\r");                   // Move cursor to begin of line (needed for non-buffered input)
        Telnet.prompt = 1;                           // Do not print linefeed for requested data and use response color
        if (Telnet.in_byte_counter >= Telnet.buffer_size) {
          AddLog(LOG_LEVEL_INFO, PSTR("TLN: buffer overrun"));
        } else {
          AddLog(LOG_LEVEL_INFO, PSTR("TLN: %s"), Telnet.buffer);
          ExecuteCommand(Telnet.buffer, SRC_TELNET);
        }
        Telnet.in_byte_counter = 0;
        return;
      }
    }
  }
}

/********************************************************************************************/

void TelnetStop(void) {
  if (Telnet.client) {
    Telnet.client.stop();
  }
  if (Telnet.server) {
    Telnet.server->stop();
    delete Telnet.server;
    Telnet.server = nullptr;
  }
  free(Telnet.buffer);
  Telnet.buffer = nullptr;
}

void TelnetInit(void) {
  Telnet.buffer_size = TELNET_BUF_SIZE;
  Telnet.color[0] = TELNET_COL_PROMPT;
  Telnet.color[1] = TELNET_COL_RESPONSE;
  Telnet.color[2] = TELNET_COL_LOGGING;
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

const char kTelnetCommands[] PROGMEM = "Telnet|"   // Prefix
  "|Buffer|Color";

void (* const TelnetCommand[])(void) PROGMEM = {
  &CmndTelnet, &CmndTelnetBuffer, &CmndTelnetColor };

void CmndTelnet(void) {
  // Telnet                - Show telnet server state
  // Telnet 0              - Disable telnet server
  // Telnet 1              - Enable telnet server on port 23
  // Telnet 23             - Enable telnet server on port 23
  // Telnet 1, 192.168.2.1 - Enable telnet server and only allow connection from 192.168.2.1
  if (!TasmotaGlobal.global_state.network_down) {
    if (XdrvMailbox.data_len) {
      Telnet.port = XdrvMailbox.payload;

      if (ArgC() == 2) {
        char sub_string[XdrvMailbox.data_len];
        Telnet.ip_filter.fromString(ArgV(sub_string, 2));
        Telnet.ip_filter_enabled = true;
      } else {
        Telnet.ip_filter_enabled = false;            // Disable whitelist if previously set
      }

      if (Telnet.server) {
        TelnetStop();
      }
      if (Telnet.port > 0) {
        if (!Telnet.buffer) {
          Telnet.buffer = (char*)malloc(Telnet.buffer_size);
        }
        if (Telnet.buffer) { 
          if (1 == Telnet.port) { Telnet.port = 23; }
          Telnet.server = new WiFiServer(Telnet.port);
          Telnet.server->begin();                    // Start TCP server
          Telnet.server->setNoDelay(true);
        }
      }
    }
    if (Telnet.server) {
      ResponseCmndChar_P(PSTR("Started"));
    } else {
      ResponseCmndChar_P(PSTR("Stopped"));
    }
  }
}

void CmndTelnetBuffer(void) {
  // TelnetBuffer     - Show current input buffer size (default 256)
  // TelnetBuffer 300 - Change input buffer size to 300 characters
  if (XdrvMailbox.data_len > 0) {
    uint16_t bsize = Telnet.buffer_size;
    Telnet.buffer_size = XdrvMailbox.payload;
    if (XdrvMailbox.payload < MIN_INPUT_BUFFER_SIZE) {
      Telnet.buffer_size = MIN_INPUT_BUFFER_SIZE;    // 256 / 256
    }
    else if (XdrvMailbox.payload > INPUT_BUFFER_SIZE) {
      Telnet.buffer_size = INPUT_BUFFER_SIZE;        // 800
    }

    if (Telnet.buffer && (bsize != Telnet.buffer_size)) {
      Telnet.buffer = (char*)realloc(Telnet.buffer, Telnet.buffer_size);
      if (!Telnet.buffer) { 
        TelnetStop();
        ResponseCmndChar_P(PSTR("Stopped"));
        return;
      }
    }
  }
  ResponseCmndNumber(Telnet.buffer_size);
}

void CmndTelnetColor(void) {
  // TelnetColor          - Show prompt, response and log colors
  // TelnetColor 0        - Set all colors to default
  // TelnetColor 1        - Set colors to defined colors
  // TelnetColor 33,32,37 - Set prompt (yellow), response (green) and log (white) colors
  if (XdrvMailbox.data_len > 0) {
    uint32_t colors[sizeof(Telnet.color)];
    uint32_t count = ParseParameters(sizeof(Telnet.color), colors);
    if (1 == count) {
      if (0 == colors[0]) {                          // TelnetColor 0
        Telnet.color[0] = 39;
        Telnet.color[1] = 39;
        Telnet.color[2] = 39;
      }
      else if (1 == colors[0]) {                     // TelnetColor 1
        Telnet.color[0] = TELNET_COL_PROMPT;
        Telnet.color[1] = TELNET_COL_RESPONSE;
        Telnet.color[2] = TELNET_COL_LOGGING;
      }
    }
    if (sizeof(Telnet.color) == count) {
      for (uint32_t i = 0; i < sizeof(Telnet.color); i++) {
        Telnet.color[i] = colors[i];
      }
    }
  }
  Response_P(PSTR("{\"%s\":[%d,%d,%d]}"),
    XdrvMailbox.command, Telnet.color[0], Telnet.color[1], Telnet.color[2]);
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv78(uint32_t function) {
  bool result = false;

  if (FUNC_INIT == function) {
    TelnetInit();
  }
  else if (FUNC_COMMAND == function) {
    result = DecodeCommand(kTelnetCommands, TelnetCommand);
  } else if (Telnet.buffer) {
    switch (function) {
      case FUNC_LOOP:
        TelnetLoop();
        break;
      case FUNC_SAVE_BEFORE_RESTART:
        TelnetStop();
        break;
      case FUNC_ACTIVE:
        result = true;
        break;
    }
  }
  return result;
}

#endif  // USE_TELNET

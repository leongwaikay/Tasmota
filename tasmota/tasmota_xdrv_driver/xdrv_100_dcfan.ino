#ifdef USE_DCFAN

//#include "Arduino.h"       // for delayMicroseconds, digitalPinToBitMask, etc
//#include <TasmotaSerial.h>

#define XDRV_100                    100


struct dcfan {
  bool active = true;
  bool on = false;
  uint8_t speed = 12;
  uint8_t hori = 3;
  uint8_t vert = 0;
} dcfan;


void dcfan_init(void) {
    if (PinUsed(GPIO_DCFAN)) {
        pinMode(Pin(GPIO_DCFAN), INPUT);
        AddLog(LOG_LEVEL_DEBUG, PSTR("DCFan init is successful..."));
    } else {
        dcfan.active = false;
        AddLog(LOG_LEVEL_DEBUG, PSTR("DCFan init failed..."));
    }
}

void write_bit(uint8_t v) {
    if (v & 1) {
        DigitalWrite(GPIO_DCFAN, 0, HIGH); 
        delayMicroseconds(480);
        DigitalWrite(GPIO_DCFAN, 0, LOW);
        delayMicroseconds(640);        
    } else {
        DigitalWrite(GPIO_DCFAN, 0, LOW);
        delayMicroseconds(1120);    
    }
    DigitalWrite(GPIO_DCFAN, 0, HIGH);
    delayMicroseconds(880);
}


void dcfan_every_250ms(void) {
    pinMode(Pin(GPIO_DCFAN), OUTPUT);

    noInterrupts();
    // bit bang output signal
    write_bit(0);
    write_bit(0);
    write_bit(0);
    write_bit(0);

    write_bit(dcfan.hori >> 1);
    write_bit(dcfan.hori);
    
    write_bit(dcfan.vert >> 1);
    write_bit(dcfan.vert);

    write_bit(dcfan.on);

    write_bit(0);
    write_bit(0);
    write_bit(0);

    write_bit(dcfan.speed >> 3);
    write_bit(dcfan.speed >> 2);
    write_bit(dcfan.speed >> 1);
    write_bit(dcfan.speed);

    interrupts();
    pinMode(Pin(GPIO_DCFAN), INPUT);
}

static void 
dcfan_cmnd_setfanspeed(void) {
    if (XdrvMailbox.data_len == 0)
		return;
    if (0 < XdrvMailbox.payload && XdrvMailbox.payload <= 12) {
        dcfan.speed = XdrvMailbox.payload;
    } 
    ResponseCmndDone();
    dcfan_publish_settings();
}

static void
dcfan_cmnd_setpower(void) {
    if (XdrvMailbox.data_len == 0)
        return;
    dcfan.on = (XdrvMailbox.payload != 0);
    ResponseCmndDone();
    dcfan_publish_settings();
}

static void
dcfan_cmnd_setswingh(void) {
    if (XdrvMailbox.data_len == 0)
        return;
    if (0 < XdrvMailbox.payload && XdrvMailbox.payload <= 90) {
        dcfan.hori = XdrvMailbox.payload / 30;
    } 
    ResponseCmndDone();
    dcfan_publish_settings();
}

static void
dcfan_cmnd_setswingv(void) {
    if (XdrvMailbox.data_len == 0)
        return;
    if (0 < XdrvMailbox.payload && XdrvMailbox.payload <= 90) {
        dcfan.vert = XdrvMailbox.payload / 30;
    } 
    ResponseCmndDone();
    dcfan_publish_settings();
}

static void
dcfan_publish_settings(void) {
    Response_P(PSTR("{\"" D_JSON_IRHVAC_POWER "\": \"%s\""), dcfan.on ? "ON" : "OFF");
    ResponseAppend_P(PSTR(",\"" D_JSON_IRHVAC_FANSPEED "\": %d"), dcfan.speed);
    ResponseAppend_P(PSTR(",\"" D_JSON_IRHVAC_SWINGH "\": %d"), dcfan.hori * 30);
    ResponseAppend_P(PSTR(",\"" D_JSON_IRHVAC_SWINGV "\": %d"), dcfan.vert * 30);
    ResponseAppend_P(PSTR("}"));

    MqttPublishPrefixTopicRulesProcess_P(TELE, PSTR("DCFan"));
}


/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

static const char dcfan_cmnd_names[] PROGMEM = 
    // No prefix
    "|DCFanSetSpeed"
    "|DCFanPower"
    "|DCFanSwingH"
    "|DCFanSwingV"
    ;
    
static void (*const dcfan_cmds[])(void) PROGMEM = {
    &dcfan_cmnd_setfanspeed,
    &dcfan_cmnd_setpower,
    &dcfan_cmnd_setswingh,
    &dcfan_cmnd_setswingv,
};


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv100(uint32_t function) {
    bool result = false;

    if (dcfan.active) {
        switch (function) {
        case FUNC_PRE_INIT:
            dcfan_init();
            break;
        case FUNC_EVERY_250_MSECOND:
            dcfan_every_250ms();
            break;
        case FUNC_COMMAND:
            AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("Calling DCFan Command..."));
            result = DecodeCommand(dcfan_cmnd_names, dcfan_cmds);
            break;
        case FUNC_AFTER_TELEPERIOD:
            dcfan_publish_settings();
            break;
        case FUNC_SET_POWER:
            dcfan.on = (XdrvMailbox.index != 0);
            AddLog(LOG_LEVEL_INFO, PSTR("FUNC_SET_DEVICE:%d, %d"),XdrvMailbox.payload, XdrvMailbox.index);
            break;
        }
    }
    return result;
}
#endif
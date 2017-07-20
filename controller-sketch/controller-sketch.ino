/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "battery.h"

Battery* battery[2];
int uplinkBatId;

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8]= { 0xA4, 0x64, 0x00, 0xF0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]= { 0x4B, 0xB7, 0x1A, 0xFC, 0x49, 0xE1, 0x50, 0x00 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

const uint8_t TX_PORT = 1;
//const unsigned long TX_INTERVAL = 5 * 60000UL;
unsigned long lastTx = 0;

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 10,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 9,
    .dio = {2, 6, 7},
};

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));

            // Disable link check validation (automatically enabled
            // during join, but not supported by TTN at this time).
            LMIC_setLinkCheckMode(0);
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
                Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
                Serial.print(F("Received "));
                Serial.print(LMIC.dataLen);
                Serial.println(F(" bytes of payload"));
                uint8_t port = *(LMIC.frame + LMIC.dataBeg - 1);
		handleDownlink(port, LMIC.frame + LMIC.dataBeg, LMIC.dataLen);
            }
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
         default:
            Serial.println(F("Unknown event"));
            break;
    }
}

void printByte(uint8_t b) {
    if (b < 0x10)
        Serial.print('0');
    Serial.print(b, HEX);
    Serial.print(' ');
}


void handleDownlink(uint8_t port, uint8_t *buf, uint8_t len) {
    for (uint8_t i = 0; i < len; ++i)
        printByte(buf[i]);
    Serial.println();

    if (port != 1 || len != 13) {
        Serial.println("Skipping unknown or invalid packet");
        return;
    }

    int downlinkBatId = buf[0] >> 4;
    if (downlinkBatId >= lengthof(battery)) {
        Serial.println("Invalid battery index");
        return;
    }

    uint16_t timeout = (buf[1] << 8) | buf[2];
    battery[downlinkBatId]->setManualTimeout(timeout);
    if (timeout > 0) {
      battery[downlinkBatId]->level[0]->setPumpState(buf[0] & (1<<0));
      battery[downlinkBatId]->level[1]->setPumpState(buf[0] & (1<<1));
      battery[downlinkBatId]->level[2]->setPumpState(buf[0] & (1<<2));
      battery[downlinkBatId]->flow[1]->setPumpState(buf[0] & (1<<3));
    }

    battery[downlinkBatId]->flow[1]->setTargetFlow(buf[3]);

    battery[downlinkBatId]->level[0]->setTargetLevel(buf[4]);
    battery[downlinkBatId]->level[1]->setTargetLevel(buf[5]);
    battery[downlinkBatId]->level[2]->setTargetLevel(buf[6]);

    battery[downlinkBatId]->level[0]->setMinLevel(buf[7]);
    battery[downlinkBatId]->level[1]->setMinLevel(buf[8]);
    battery[downlinkBatId]->level[2]->setMinLevel(buf[9]);

    battery[downlinkBatId]->level[0]->setMaxLevel(buf[10]);
    battery[downlinkBatId]->level[1]->setMaxLevel(buf[11]);
    battery[downlinkBatId]->level[2]->setMaxLevel(buf[12]);

    // Send an update ASAP
    lastTx = 0;
}

void queueUplink() {
    uint8_t buf[21];
    // Timeout in minutes
    uint16_t timeout = battery[uplinkBatId]->panic ? -1 : battery[uplinkBatId]->getManualTimeout();
    buf[0]  = timeout >> 8;
    buf[1]  = timeout;

    // Pump state in 255ths
    buf[2]  = battery[uplinkBatId]->level[0]->getPumpState();
    buf[3]  = battery[uplinkBatId]->level[1]->getPumpState();
    buf[4]  = battery[uplinkBatId]->level[2]->getPumpState();
    buf[5]  = battery[uplinkBatId]->flow[1]->getPumpState();

    // Flow is in M²/hour
    buf[6]  = battery[uplinkBatId]->flow[0]->getCurrentFlow();
    buf[7]  = battery[uplinkBatId]->flow[1]->getCurrentFlow();
    buf[8]  = battery[uplinkBatId]->flow[1]->getTargetFlow();

    // Levels are in cm
    buf[9]  = battery[uplinkBatId]->level[0]->getCurrentLevel();
    buf[10] = battery[uplinkBatId]->level[1]->getCurrentLevel();
    buf[11] = battery[uplinkBatId]->level[2]->getCurrentLevel();

    buf[12] = battery[uplinkBatId]->level[0]->getTargetLevel();
    buf[13] = battery[uplinkBatId]->level[1]->getTargetLevel();
    buf[14] = battery[uplinkBatId]->level[2]->getTargetLevel();

    buf[15] = battery[uplinkBatId]->level[0]->getMinLevel();
    buf[16] = battery[uplinkBatId]->level[1]->getMinLevel();
    buf[17] = battery[uplinkBatId]->level[2]->getMinLevel();

    buf[18] = battery[uplinkBatId]->level[0]->getMaxLevel();
    buf[19] = battery[uplinkBatId]->level[1]->getMaxLevel();
    buf[20] = battery[uplinkBatId]->level[2]->getMaxLevel();

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(TX_PORT, buf, sizeof(buf), 0);

    Serial.println(F("Packet queued"));
    uplinkBatId = 1 - uplinkBatId;
}

#define SENSORPIN 2
#define PUMPPIN 2
void setup() {
    battery[0] = new Battery();
    battery[0]->attachFlowController(0, SENSORPIN);
    battery[0]->attachFlowController(1, SENSORPIN, PUMPPIN);
    battery[0]->attachLevelController(0, SENSORPIN, PUMPPIN);
    battery[0]->attachLevelController(1, SENSORPIN, PUMPPIN);
    battery[0]->attachLevelController(2, SENSORPIN, PUMPPIN);

    battery[1] = new Battery();
    battery[1]->attachFlowController(0, SENSORPIN);
    battery[1]->attachFlowController(1, SENSORPIN, PUMPPIN);
    battery[1]->attachLevelController(0, SENSORPIN, PUMPPIN);
    battery[1]->attachLevelController(1, SENSORPIN, PUMPPIN);
    battery[1]->attachLevelController(2, SENSORPIN, PUMPPIN);

    uplinkBatId = 0;

    Serial.begin(9600);
    Serial.println(F("Starting"));

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // TODO: Less?
    LMIC_setClockError(MAX_CLOCK_ERROR * 5 / 100);
}

void loop() {
    os_runloop_once();
    unsigned long now = millis();
    if (lastTx == 0 || now - lastTx > TX_INTERVAL) {
        for(int i=0;i<lengthof(battery);i++) battery[i]->doCycle(lastTx, now); queueUplink();
        lastTx = now;
    }
    for(int i=0;i<lengthof(battery);i++) battery[i]->doLoop(lastTx, now);
}


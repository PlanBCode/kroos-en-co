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
#include <avr/wdt.h>
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
const uint8_t RX_PORT = 1;
unsigned long lastCycleTime = 0;

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
            // Set spreading factor to lowest value, allowing for regular status updates within the boundaries of TTN's fair use policy
            LMIC_setDrTxpow(DR_SF7, 14);
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
    Serial.print(F("Received "));
    Serial.print(len);
    Serial.print(F(" bytes of payload on port"));
    Serial.print(port);
    Serial.print(F(", frame counter "));
    Serial.println(LMIC.seqnoDn);
    for (uint8_t i = 0; i < len; ++i)
        printByte(buf[i]);
    Serial.println();

    if (port < RX_PORT || port >= RX_PORT + lengthof(battery) || len != 16) {
        Serial.println("Skipping unknown or invalid packet");
        return;
    }

    unsigned downlinkBatId = port - RX_PORT;
    Serial.print("Received config for battery ");
    Serial.println(downlinkBatId + 1);

    battery[downlinkBatId]->panic = false;
    for (size_t i=0;i<lengthof(battery[downlinkBatId]->flow);i++) battery[downlinkBatId]->flow[i]->enable();
    for (size_t i=0;i<lengthof(battery[downlinkBatId]->level);i++) battery[downlinkBatId]->level[i]->enable();

    uint16_t timeout = (buf[0] << 8) | buf[1];
    battery[downlinkBatId]->setManualTimeout(timeout);
    if (timeout > 0) {
      battery[downlinkBatId]->flow[0]->setPumpDutyCycle(buf[2]);
      battery[downlinkBatId]->level[0]->setPumpDutyCycle(buf[3]);
      battery[downlinkBatId]->level[1]->setPumpDutyCycle(buf[4]);
      battery[downlinkBatId]->level[2]->setPumpDutyCycle(buf[5]);
    }

    battery[downlinkBatId]->flow[0]->setTargetFlow(buf[6]);

    battery[downlinkBatId]->level[0]->setTargetLevel(buf[7] * 4);
    battery[downlinkBatId]->level[1]->setTargetLevel(buf[8] * 4);
    battery[downlinkBatId]->level[2]->setTargetLevel(buf[9] * 4);

    battery[downlinkBatId]->level[0]->setMinLevel(buf[10] * 4);
    battery[downlinkBatId]->level[1]->setMinLevel(buf[11] * 4);
    battery[downlinkBatId]->level[2]->setMinLevel(buf[12] * 4);

    battery[downlinkBatId]->level[0]->setMaxLevel(buf[13] * 4);
    battery[downlinkBatId]->level[1]->setMaxLevel(buf[14] * 4);
    battery[downlinkBatId]->level[2]->setMaxLevel(buf[15] * 4);
}

void queueUplink() {
    uint8_t buf[23];
    // Timeout in minutes
    uint16_t timeout = min(battery[uplinkBatId]->getManualTimeout(), 0x7FFF);
    if (battery[uplinkBatId]->panic) {
      timeout |= 0x8000;
    }
    buf[0]  = timeout >> 8;
    buf[1]  = timeout;

    // Pump state in 255ths
    buf[2]  = battery[uplinkBatId]->flow[0]->prevPumpDutyCycle;
    buf[3]  = battery[uplinkBatId]->level[0]->prevPumpDutyCycle;
    buf[4]  = battery[uplinkBatId]->level[1]->prevPumpDutyCycle;
    buf[5]  = battery[uplinkBatId]->level[2]->prevPumpDutyCycle;

    // Flow is in m³/hour
    buf[6]  = round(battery[uplinkBatId]->flow[0]->getForwardFlow());
    buf[7]  = round(battery[uplinkBatId]->flow[1]->getForwardFlow());
    buf[8]  = round(battery[uplinkBatId]->flow[0]->getTargetFlow());

    // Levels are in ADC value, divided by four to fit in a byte
    buf[9]  = round(battery[uplinkBatId]->level[0]->getCurrentLevel() / 4);
    buf[10] = round(battery[uplinkBatId]->level[1]->getCurrentLevel() / 4);
    buf[11] = round(battery[uplinkBatId]->level[2]->getCurrentLevel() / 4);

    buf[12] = round(battery[uplinkBatId]->level[0]->getTargetLevel() / 4);
    buf[13] = round(battery[uplinkBatId]->level[1]->getTargetLevel() / 4);
    buf[14] = round(battery[uplinkBatId]->level[2]->getTargetLevel() / 4);

    buf[15] = round(battery[uplinkBatId]->level[0]->getMinLevel() / 4);
    buf[16] = round(battery[uplinkBatId]->level[1]->getMinLevel() / 4);
    buf[17] = round(battery[uplinkBatId]->level[2]->getMinLevel() / 4);

    buf[18] = round(battery[uplinkBatId]->level[0]->getMaxLevel() / 4);
    buf[19] = round(battery[uplinkBatId]->level[1]->getMaxLevel() / 4);
    buf[20] = round(battery[uplinkBatId]->level[2]->getMaxLevel() / 4);

    buf[21]  = round(battery[uplinkBatId]->flow[0]->getReverseFlow());
    buf[22]  = round(battery[uplinkBatId]->flow[1]->getReverseFlow());

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(TX_PORT + uplinkBatId, buf, sizeof(buf), 0);

    Serial.print(F("Packet queued, frame counter "));
    Serial.println(LMIC.seqnoUp);
    for (uint8_t i = 0; i < sizeof(buf); ++i)
        printByte(buf[i]);
    Serial.println();

    uplinkBatId = (uplinkBatId+1)%lengthof(battery);
}

void setup() {
    // Use the external AREF, which is wired to 3.3V. Never disable
    // this, since using analogRead with the default VCC or internal
    // 1.1V reference drives the AREF pin, causing a short circuit.
    analogReference(EXTERNAL);

    battery[0] = new Battery();
    battery[0]->attachFlowController(0, 8, 47, 23);
    // 3316mV, 1023 steps, 100Ohm, 0.154mA/cm. 4mA/0.154 - 20cm
    // Calibration values are used for debug printing only!
    battery[0]->attachLevelController(0, A0, 25, 3316.0/1023/100/0.176, -4/0.176 - 20);
    battery[0]->attachLevelController(1, A1, 27, 3316.0/1023/100/0.165, -4/0.165 - 35);
    battery[0]->attachLevelController(2, A2, 29, 3316.0/1023/100/0.154, -4/0.154 - 20);
    battery[0]->attachFlowController(1, 5, 49);

    battery[1] = new Battery();
    battery[1]->attachFlowController(0, 4, 51, 31);
    battery[1]->attachLevelController(0, A3, 33, 3316.0/1023/100/0.176, -4/0.176 - 16);
    battery[1]->attachLevelController(1, A4, 35, 3316.0/1023/100/0.159, -4/0.159 - 24);
    battery[1]->attachLevelController(2, A5, 37, 3316.0/1023/100/0.154, -4/0.154 - 13);
    battery[1]->attachFlowController(1, 3, 53);

    for (size_t b=0; b<2; b++) {
      for (size_t i=0;i<lengthof(battery[b]->flow);i++) battery[b]->flow[i]->disable();
      for (size_t i=0;i<lengthof(battery[b]->level);i++) battery[b]->level[i]->disable();
    }

    uplinkBatId = 0;

    Serial.begin(9600);
    Serial.println(F("Starting"));

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // prevent a floating reset pin from triggering on random inductive signals
    digitalWrite(lmic_pins.rst, HIGH);
    pinMode(lmic_pins.rst, OUTPUT);

    // TODO: Less?
    LMIC_setClockError(MAX_CLOCK_ERROR * 5 / 100);

    // Enable watchdog timer
    wdt_enable(WDTO_2S);
}

void loop() {
    os_runloop_once();
    unsigned long now = millis();
    if (lastCycleTime == 0 || now - lastCycleTime > CYCLE_INTERVAL) {
        printf("Cycle time: %lu\n", now - lastCycleTime);
        for(size_t i=0;i<lengthof(battery);i++) {
            printf("***** Battery %d *****\n", i+1);
            battery[i]->doCycle(now - lastCycleTime);
        }
        printf("**********\n");
        queueUplink();
        lastCycleTime = now;
    }
    for(size_t i=0;i<lengthof(battery);i++) battery[i]->doLoop(now - lastCycleTime);
    wdt_reset();
}


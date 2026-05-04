#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_SPIFlash.h>

#define FLASH_CS_PIN 10

Adafruit_FlashTransport_SPI flashTransport(FLASH_CS_PIN, &SPI);
Adafruit_SPIFlash spiFlash(&flashTransport);

static const uint32_t SECTOR_SIZE = 4096;

// --------------------------------------------------
// LOW LEVEL BUS RESET (fixes post-erase desync)
// --------------------------------------------------
void resetSPIBus() {
    SPI.end();
    delay(2);
    SPI.begin();
}

// --------------------------------------------------
// VERIFY ONE SECTOR IS BLANK (0xFF)
// --------------------------------------------------
bool verifySector(uint32_t addr) {
    uint8_t buf[64];

    for (uint32_t off = 0; off < SECTOR_SIZE; off += sizeof(buf)) {
        spiFlash.readBuffer(addr + off, buf, sizeof(buf));

        for (int i = 0; i < sizeof(buf); i++) {
            if (buf[i] != 0xFF) {
                Serial.print("VERIFY FAIL @ ");
                Serial.print(addr + off + i);
                Serial.print(" val=0x");
                Serial.println(buf[i], HEX);
                return false;
            }
        }
    }
    return true;
}

// --------------------------------------------------
// FORMAT + VERIFY WITH TRANSACTION ISOLATION
// --------------------------------------------------
void formatFlash() {
    Serial.println("\nFLASH FORMAT START");

    if (!spiFlash.begin()) {
        Serial.println("Flash init failed");
        while (1);
    }

    uint32_t size = spiFlash.size();
    uint32_t lastAddr = size - SECTOR_SIZE;

    Serial.print("Flash size: ");
    Serial.println(size);

    // ---------------- ERASE + IMMEDIATE VERIFY ----------------
    for (uint32_t addr = 0; addr <= lastAddr; addr += SECTOR_SIZE) {

        Serial.print("Erasing: ");
        Serial.println(addr);

        if (!spiFlash.eraseSector(addr)) {
            Serial.print("ERASE FAIL @ ");
            Serial.println(addr);
            return;
        }

        spiFlash.waitUntilReady();
        resetSPIBus();

        delay(2);

        // immediate verify (not full scan)
        if (!verifySector(addr)) {
            Serial.print("DIRTY SECTOR AFTER ERASE @ ");
            Serial.println(addr);
            return;
        }

        resetSPIBus();
        delay(1);
    }

    Serial.println("FLASH CLEAN (ALL SECTORS VERIFIED)");
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    SPI.begin();

    formatFlash();

    Serial.println("DONE");
}

// --------------------------------------------------
void loop() {}
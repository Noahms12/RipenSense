#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_SPIFlash.h>

#define FLASH_CS_PIN 10

Adafruit_FlashTransport_SPI flashTransport(FLASH_CS_PIN, &SPI);
Adafruit_SPIFlash spiFlash(&flashTransport);

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println("\n=== RAW FLASH TEST (NO FATFS) ===");

    SPI.begin();

    if (!spiFlash.begin()) {
        Serial.println("ERROR: flash init failed");
        while (1);
    }

    Serial.print("Flash size (bytes): ");
    Serial.println(spiFlash.size());

    uint32_t addr = 0x000000;

    // ---- ERASE ----
    Serial.println("[ERASE]");
    if (!spiFlash.eraseSector(addr)) {
        Serial.println("ERROR: erase failed");
        return;
    }

    // ---- WRITE ----
    Serial.println("[WRITE]");
    uint8_t tx[32];
    for (int i = 0; i < 32; i++) tx[i] = i;

    if (!spiFlash.writeBuffer(addr, tx, sizeof(tx))) {
        Serial.println("ERROR: write failed");
        return;
    }

    // ---- READ ----
    Serial.println("[READ]");
    uint8_t rx[32];

    if (!spiFlash.readBuffer(addr, rx, sizeof(rx))) {
        Serial.println("ERROR: read failed");
        return;
    }

    // ---- VERIFY ----
    bool ok = true;
    for (int i = 0; i < 32; i++) {
        if (rx[i] != tx[i]) {
            ok = false;
            Serial.print("Mismatch @ ");
            Serial.print(i);
            Serial.print(": ");
            Serial.print(rx[i], HEX);
            Serial.print(" != ");
            Serial.println(tx[i], HEX);
        }
    }

    if (ok) {
        Serial.println("PASS: raw read/write OK");
    } else {
        Serial.println("FAIL: verification error");
    }

    // ---- CSV WRITE TEST ----
    const char *csv = "12345,23.5,50.2\n";

    Serial.println("[CSV WRITE]");
    spiFlash.eraseSector(0x001000);
    spiFlash.writeBuffer(0x001000, (const uint8_t*)csv, strlen(csv));

    uint8_t buf[64] = {0};
    spiFlash.readBuffer(0x001000, buf, strlen(csv));

    Serial.print("CSV READBACK: ");
    Serial.println((char*)buf);

    Serial.println("=== DONE ===");
}

void loop() {}
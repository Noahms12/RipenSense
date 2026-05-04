#include <Arduino.h>
#include <Wire.h>

// Change if needed
#define GNSS_ADDR 0x42

unsigned long lastBlink = 0;
unsigned long lastScan = 0;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(115200);
    while (!Serial) { delay(10); }  // ensure USB CDC is up

    delay(500); // give host time to attach

    Serial.println("NRF52 USB + I2C sanity test");

    Wire.begin();
    Wire.setClock(100000); // slower, more stable for testing
}

void loop() {
    // 1. LED heartbeat (proves firmware is running regardless of USB)
    if (millis() - lastBlink > 500) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        lastBlink = millis();
    }

    // 2. Periodic serial output (proves USB stability)
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 1000) {
        Serial.print("Alive ms=");
        Serial.println(millis());
        lastPrint = millis();
    }

    // 3. I2C scan every 5s (detects bus lock or device presence)
    if (millis() - lastScan > 5000) {
        Serial.println("Scanning I2C...");

        bool found = false;

        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            uint8_t err = Wire.endTransmission();

            if (err == 0) {
                Serial.print("Found device at 0x");
                if (addr < 16) Serial.print("0");
                Serial.println(addr, HEX);

                if (addr == GNSS_ADDR) {
                    Serial.println("GNSS detected at 0x42");
                }
                found = true;
            }
        }

        if (!found) {
            Serial.println("No I2C devices found");
        }

        lastScan = millis();
    }
}
#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>

SFE_MAX1704X lipo(MAX1704X_MAX17043); // change if needed

unsigned long lastPrint = 0;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(115200);
    while (!Serial) { delay(10); }

    delay(500);

    Serial.println("MAX1704x raw + library hybrid test");

    Wire.begin();
    Wire.setClock(100000);

    // Try init, but do NOT trust it blindly
    bool ok = lipo.begin(Wire);

    Serial.print("Library begin(): ");
    Serial.println(ok ? "OK" : "FAIL (continuing anyway)");

    // Direct sanity check: does device ACK on bus?
    Wire.beginTransmission(0x36);
    uint8_t err = Wire.endTransmission();

    Serial.print("I2C ACK test (0x36): ");
    Serial.println(err == 0 ? "OK" : "FAIL");

    if (err != 0) {
        Serial.println("No device on bus");
        while (1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(200);
        }
    }
}

float readSOC_raw() {
    Wire.beginTransmission(0x36);
    Wire.write(0x04);
    Wire.endTransmission(false);

    if (Wire.requestFrom(0x36, 2) != 2) return -1;

    uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
    return raw / 256.0f;
}

float readVoltage_raw() {
    Wire.beginTransmission(0x36);
    Wire.write(0x02);
    Wire.endTransmission(false);

    if (Wire.requestFrom(0x36, 2) != 2) return -1;

    uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
    return raw * 1.25f / 1000.0f;
}

void loop() {
    digitalWrite(LED_BUILTIN, millis() / 500 % 2);

    if (millis() - lastPrint > 1000) {
        lastPrint = millis();

        float soc_lib = lipo.getSOC();
        float v_lib   = lipo.getVoltage();

        float soc_raw = readSOC_raw();
        float v_raw   = readVoltage_raw();

        Serial.print("LIB SOC: ");
        Serial.print(soc_lib);
        Serial.print("%  V: ");
        Serial.print(v_lib);

        Serial.print(" | RAW SOC: ");
        Serial.print(soc_raw);
        Serial.print("%  V: ");
        Serial.println(v_raw);
    }
}
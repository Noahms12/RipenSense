
// WORKING GPS CODE ONLY (no other sensors, no BLE) -- just to verify GPS I2C comms and parsing
#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

SFE_UBLOX_GNSS gnss;

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) {}

    Serial.println("GPS I2C test starting...");

    Wire.begin();
    Wire.setClock(400000);

    if (gnss.begin(Wire)) {
        Serial.println("GPS: found on I2C");
        gnss.setI2COutput(COM_TYPE_UBX);
        gnss.setNavigationFrequency(1);
        gnss.setAutoPVT(true);
        gnss.saveConfiguration();
    } else {
        Serial.println("GPS: NOT found -- check wiring (SDA/SCL, 3.3V, GND)");
    }
}

void loop() {
    if (gnss.getPVT(1100)) {
        Serial.print("fix=");      Serial.print(gnss.getFixType());
        Serial.print(" sats=");    Serial.print(gnss.getSIV());
        Serial.print(" timeOk=");  Serial.print(gnss.getTimeValid());
        Serial.print(" dateOk=");  Serial.println(gnss.getDateValid());

        if (gnss.getFixType() >= 2) {
            Serial.print("lat=");  Serial.print(gnss.getLatitude()  / 1e7, 6);
            Serial.print(" lon="); Serial.print(gnss.getLongitude() / 1e7, 6);
            Serial.print(" alt="); Serial.print(gnss.getAltitude()  / 1000.0, 1);
            Serial.println("m");
        }
    } else {
        Serial.println("GPS: no PVT response");
    }

    delay(1000);
}

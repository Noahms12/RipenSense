#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SHT31.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

#define ONE_WIRE_BUS 5
#define UTC_OFFSET_HOURS -4  // EDT (UTC-4); change to -5 for EST

BLEUart bleuart;

Adafruit_MPU6050  imu;
Adafruit_SHT31    sht31;
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
SFE_MAX1704X      fuelGauge(MAX1704X_MAX17048);
SFE_UBLOX_GNSS    gnss;

bool imuOk   = false;
bool sht31Ok = false;
bool gaugeOk = false;
bool gnssOk  = false;

void bleLog(const char* s) {
    Serial.print(s);
    if (bleuart.notifyEnabled()) bleuart.print(s);
}

void utcToEdt(int y, int mo, int d, int h, int mi, int s,
              int& oy, int& omo, int& od, int& oh, int& omi, int& os) {
    static const int daysInMonth[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    oh = h + UTC_OFFSET_HOURS;
    omi = mi; os = s; od = d; omo = mo; oy = y;

    if (oh < 0) {
        oh += 24;
        od--;
        if (od < 1) {
            omo--;
            if (omo < 1) { omo = 12; oy--; }
            bool leap = (oy % 4 == 0 && (oy % 100 != 0 || oy % 400 == 0));
            od = (omo == 2 && leap) ? 29 : daysInMonth[omo];
        }
    } else if (oh >= 24) {
        oh -= 24;
        od++;
        bool leap = (oy % 4 == 0 && (oy % 100 != 0 || oy % 400 == 0));
        int dim = (omo == 2 && leap) ? 29 : daysInMonth[omo];
        if (od > dim) { od = 1; omo++; if (omo > 12) { omo = 1; oy++; } }
    }
}

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 2000) {}

    Wire.begin();
    Wire.setClock(400000);

    // --- BLE ---
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName("RipenSense");
    bleuart.begin();
    Bluefruit.Advertising.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);

    Serial.println("RipenSense -- BLE advertising as 'RipenSense'");

    // --- GPS (I2C 0x42) ---
    if (gnss.begin(Wire)) {
        gnssOk = true;
        gnss.setI2COutput(COM_TYPE_UBX);
        gnss.setNavigationFrequency(1);
        gnss.setAutoPVT(false);  // polled only
        Serial.println("GPS:             OK");
    } else {
        Serial.println("GPS:             FAIL");
    }

    // --- Ethylene sensor (DGS-EC) on Serial1 ---
    Serial1.begin(9600);
    Serial.println("Ethylene sensor: ready on Serial1");

    // Battery fuel gauge (I2C 0x36)
    gaugeOk = fuelGauge.begin(Wire);
    Serial.println(gaugeOk ? "Battery gauge:   OK" : "Battery gauge:   FAIL");

    // Ambient temp + humidity (I2C 0x45)
    sht31Ok = sht31.begin(0x45);
    Serial.println(sht31Ok ? "Temp/Humidity:   OK" : "Temp/Humidity:   FAIL");

    // Accelerometer / gyro (I2C 0x68)
    imuOk = imu.begin();
    if (imuOk) {
        imu.setAccelerometerRange(MPU6050_RANGE_8_G);
        imu.setGyroRange(MPU6050_RANGE_500_DEG);
        imu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    }
    Serial.println(imuOk ? "Accel/Gyro:      OK" : "Accel/Gyro:      FAIL");

    // External temp probe (1-Wire pin 5)
    ds18b20.begin();
    ds18b20.setResolution(9);
    char dsBuf[40];
    snprintf(dsBuf, sizeof(dsBuf), "Temp probe:      %d device(s)\n", ds18b20.getDeviceCount());
    Serial.print(dsBuf);

    Serial.println("--- Setup complete ---");
}

bool readEthylene(char* out, size_t outLen, unsigned long timeoutMs = 300) {
    while (Serial1.available()) Serial1.read();
    Serial1.write('\r');
    unsigned long deadline = millis() + timeoutMs;
    size_t pos = 0;
    while (millis() < deadline) {
        if (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                out[pos] = '\0';
                if (pos > 0 && out[pos - 1] == '\r') out[--pos] = '\0';
                return pos > 0;
            }
            if (pos < outLen - 1) out[pos++] = c;
        }
    }
    return false;
}

void loop() {
    char buf[96];

    // Let I2C bus settle after previous iteration before hitting GPS
    delay(50);

    // --- GPS ---
    if (!gnssOk) {
        bleLog("GPS: module not found\r\n");
    } else if (!gnss.getPVT(1100)) {
        bleLog("GPS: timeout\r\n");
    } else {
        uint8_t fixType = gnss.getFixType();
        uint8_t siv     = gnss.getSIV();
        bool    timeOk  = gnss.getTimeValid();
        bool    dateOk  = gnss.getDateValid();

        snprintf(buf, sizeof(buf), "GPS: fix=%d sats=%d\r\n", fixType, siv);
        bleLog(buf);

        if (timeOk && dateOk) {
            int oy, omo, od, oh, omi, os;
            utcToEdt(gnss.getYear(), gnss.getMonth(),  gnss.getDay(),
                     gnss.getHour(), gnss.getMinute(), gnss.getSecond(),
                     oy, omo, od, oh, omi, os);
            snprintf(buf, sizeof(buf),
                "Date/Time: %04d-%02d-%02d %02d:%02d:%02d EDT\r\n",
                oy, omo, od, oh, omi, os);
            bleLog(buf);
        }

        if (fixType >= 2) {
            snprintf(buf, sizeof(buf),
                "Location:  lat=%.6f lon=%.6f alt=%.1fm\r\n",
                gnss.getLatitude()  / 1e7,
                gnss.getLongitude() / 1e7,
                gnss.getAltitude()  / 1000.0);
            bleLog(buf);
        }
    }

    // --- Battery ---
    if (gaugeOk) {
        snprintf(buf, sizeof(buf), "Battery:   %.1f%%  %.3fV\r\n",
            fuelGauge.getSOC(), fuelGauge.getVoltage());
        bleLog(buf);
    }

    // --- Ambient temp + humidity ---
    if (sht31Ok) {
        float t = sht31.readTemperature();
        float h = sht31.readHumidity();
        if (!isnan(t) && !isnan(h)) {
            snprintf(buf, sizeof(buf), "Temp:      %.2fC\r\n", t);
            bleLog(buf);
            snprintf(buf, sizeof(buf), "Humidity:  %.2f%%RH\r\n", h);
            bleLog(buf);
        } else {
            bleLog("Temp/Humidity: read error\r\n");
        }
    }

    // --- Accelerometer + gyro ---
    if (imuOk) {
        sensors_event_t accel, gyro, temp;
        imu.getEvent(&accel, &gyro, &temp);
        snprintf(buf, sizeof(buf), "Accel:     %.2f, %.2f, %.2f m/s2\r\n",
            accel.acceleration.x, accel.acceleration.y, accel.acceleration.z);
        bleLog(buf);
        snprintf(buf, sizeof(buf), "Gyro:      %.2f, %.2f, %.2f rad/s\r\n",
            gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);
        bleLog(buf);
    }

    // --- External temp probe ---
    ds18b20.requestTemperatures();
    float probe = ds18b20.getTempCByIndex(0);
    if (probe != DEVICE_DISCONNECTED_C) {
        snprintf(buf, sizeof(buf), "Probe Temp: %.2fC\r\n", probe);
    } else {
        snprintf(buf, sizeof(buf), "Probe Temp: not found\r\n");
    }
    bleLog(buf);

    // --- Ethylene (DGS-EC on Serial1) ---
    char ethBuf[80];
    if (readEthylene(ethBuf, sizeof(ethBuf))) {
        char* token = strtok(ethBuf, ",");  // SN
        token = strtok(NULL, ",");           // PPB
        if (token) {
            snprintf(buf, sizeof(buf), "Ethylene:  %s ppb\r\n", token);
        } else {
            snprintf(buf, sizeof(buf), "Ethylene:  parse error\r\n");
        }
    } else {
        snprintf(buf, sizeof(buf), "Ethylene:  no response\r\n");
    }
    bleLog(buf);

    bleLog("----\r\n");
    delay(2000);
}

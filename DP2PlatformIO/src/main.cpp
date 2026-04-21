#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// Your existing modules
#include "mpu6050_imu.h"
#include "sht31_climate.h"

// New modules
#include "ds18b20_sensor.h"
#include "dgs2_gas.h"
#include "rtc_rv1805.h"
#include "power_sys.h"
#include "gps_maxm10s.h"
#include "storage_w25q16.h"

// --- Pin Definitions ---
#define ONE_WIRE_BUS 5       // DS18B20 Data pin
#define LOAD_SWITCH_PIN 6    // TPS22918 enable pin
#define FLASH_CS 10          // W25Q16 Chip Select

// --- Instantiations ---
DS18B20Sensor tempProbe(ONE_WIRE_BUS);
DGS2Gas gasSensor(&Serial1); // Assuming RX/TX on Serial1
RTC_RV1805 rtc;
PowerSys power(LOAD_SWITCH_PIN);
GPS_MAXM10S gps;

// SPI setup for W25Q16
Adafruit_FlashTransport_SPI flashTransport(FLASH_CS, &SPI);
StorageW25Q16 flash(&flashTransport);

// Assuming your existing modules follow a similar class structure:
// MPU6050_IMU imu;
// SHT31_Climate climate;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    Serial.println("Initializing Fruit Spoilage Tracker...");

    // Initialize core buses
    Wire.begin(); // Standard Qwiic/I2C
    SPI.begin();  // Standard SPI
    
    // 1. Power Management
    if (power.begin()) {
        Serial.println("Power system & Fuel Gauge online.");
    }

    // 2. Storage
    if (flash.begin()) {
        Serial.println("SPI Flash ready.");
    }

    // 3. Navigation & Timing
    if (rtc.begin()) {
        Serial.println("RTC online.");
    }
    if (gps.begin()) {
        Serial.println("GPS online."); // AANE-AP-0164-1 Antenna should be attached
    }

    // 4. Environmental Sensors
    tempProbe.begin();
    gasSensor.begin(9600);
    
    // imu.begin();
    // climate.begin();
    
    Serial.println("Setup Complete.");
}

void loop() {
    // Example data loop
    Serial.print("Time: ");
    Serial.println(rtc.getTimestamp());

    Serial.print("Battery: ");
    Serial.print(power.getSOC());
    Serial.println("%");

    Serial.print("Probe Temp (C): ");
    Serial.println(tempProbe.getTemperatureC());
    
    // Read and clear the UART gas sensor buffer
    String gasData = gasSensor.readData();
    if(gasData.length() > 0) {
        Serial.print("Gas: ");
        Serial.println(gasData);
    }
    
    Serial.println("--------------------");
    delay(2000);
}
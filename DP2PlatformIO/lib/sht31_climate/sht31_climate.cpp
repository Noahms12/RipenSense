#include "sht31_climate.h"

SHT31Climate::SHT31Climate() : sensor(&Wire, 0x45), initialized(false) {
    memset(&lastReading, 0, sizeof(SHT31_Reading));
}

SHT31_Status SHT31Climate::init(uint8_t i2c_address) {
    // DFRobot_SHT3x constructor needs device address
    sensor.address = i2c_address;
    
    if (sensor.begin() != 0) {
        return SHT31_INIT_FAILED;
    }
    
    sensor.softReset();
    initialized = true;
    return SHT31_OK;
}

SHT31_Status SHT31Climate::read(SHT31_Reading &reading) {
    if (!initialized) {
        return SHT31_INIT_FAILED;
    }
    
    float temp = sensor.getTemperatureC();
    float hum = sensor.getHumidityRH();
    
    if (isnan(temp) || isnan(hum)) {
        return SHT31_READ_FAILED;
    }
    
    reading.temperature_c = temp;
    reading.humidity_percent = hum;
    reading.timestamp_ms = millis();
    
    lastReading = reading;
    return SHT31_OK;
}

bool SHT31Climate::isConnected() {
    if (!initialized) return false;
    // Query sensor by reading and checking for valid value
    return !isnan(sensor.getTemperatureC());
}

SHT31_Status SHT31Climate::getLastReading(SHT31_Reading &reading) {
    if (!initialized) {
        return SHT31_INIT_FAILED;
    }
    reading = lastReading;
    return SHT31_OK;
}
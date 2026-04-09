#include "ds18b20_probe.h"

DS18B20Probe::DS18B20Probe(uint8_t gpio_pin)
    : onewire(gpio_pin), sensors(&onewire), initialized(false),
      conversion_in_progress(false), conversion_start_ms(0) {
    memset(&lastReading, 0, sizeof(DS18B20_Reading));
}

DS18B20_Status DS18B20Probe::init() {
    sensors.begin();
    
    // Check if any devices found
    if (sensors.getDeviceCount() == 0) {
        return DS18B20_NO_SENSOR;
    }
    
    // Set resolution (12-bit default for accuracy)
    sensors.setResolution(12);
    initialized = true;
    
    return DS18B20_OK;
}

DS18B20_Status DS18B20Probe::read(DS18B20_Reading &reading) {
    if (!initialized) {
        return DS18B20_INIT_FAILED;
    }
    
    // Non-blocking approach:
    // First call: request conversion
    if (!conversion_in_progress) {
        sensors.requestTemperatures();
        conversion_in_progress = true;
        conversion_start_ms = millis();
        return DS18B20_READ_FAILED;  // Not ready yet
    }
    
    // Check if conversion is complete
    if ((millis() - conversion_start_ms) < CONVERSION_TIME_12BIT_MS) {
        return DS18B20_READ_FAILED;  // Still converting
    }
    
    // Conversion complete, read the value
    float temp_c = sensors.getTempCByIndex(0);
    
    if (temp_c == 85.0 || temp_c == -127.0) {
        // These are error codes from the sensor
        return DS18B20_CRC_ERROR;
    }
    
    reading.temperature_c = temp_c;
    reading.timestamp_ms = millis();
    lastReading = reading;
    
    conversion_in_progress = false;
    return DS18B20_OK;
}

DS18B20_Status DS18B20Probe::readBlocking(DS18B20_Reading &reading) {
    if (!initialized) {
        return DS18B20_INIT_FAILED;
    }
    
    sensors.requestTemperatures();
    delay(CONVERSION_TIME_12BIT_MS);  // Wait for conversion
    
    float temp_c = sensors.getTempCByIndex(0);
    
    if (temp_c == 85.0 || temp_c == -127.0) {
        return DS18B20_CRC_ERROR;
    }
    
    reading.temperature_c = temp_c;
    reading.timestamp_ms = millis();
    lastReading = reading;
    
    return DS18B20_OK;
}

bool DS18B20Probe::isConnected() {
    if (!initialized) return false;
    return sensors.getDeviceCount() > 0;
}

DS18B20_Status DS18B20Probe::getLastReading(DS18B20_Reading &reading) {
    if (!initialized) {
        return DS18B20_INIT_FAILED;
    }
    reading = lastReading;
    return DS18B20_OK;
}

void DS18B20Probe::setResolution(uint8_t bits) {
    if (bits >= 9 && bits <= 12) {
        sensors.setResolution(bits);
    }
}

#include "battery_gauge.h"

// Register addresses for SparkFun LiPo Fuel Gauge
#define VCELL           0x02
#define SOC             0x04
#define STATUS          0x1A
#define MODE            0x06

BatteryGauge::BatteryGauge() : i2c_addr(0x36), initialized(false) {
    memset(&lastReading, 0, sizeof(BatteryReading));
}

Gauge_Status BatteryGauge::init(uint8_t i2c_address) {
    i2c_addr = i2c_address;
    
    // Check if device is present on I2C bus
    Wire.beginTransmission(i2c_addr);
    if (Wire.endTransmission() != 0) {
        return GAUGE_INIT_FAILED;
    }
    
    initialized = true;
    return GAUGE_OK;
}

Gauge_Status BatteryGauge::read(BatteryReading &reading) {
    if (!initialized) {
        return GAUGE_INIT_FAILED;
    }
    
    // Read voltage (register 0x02)
    uint16_t vcell_raw = readRegister16(VCELL);
    // VCELL register: each LSB = 1.25 mV
    float voltage = (vcell_raw >> 4) * 0.00125f;
    
    // Read state of charge (register 0x04)
    uint16_t soc_raw = readRegister16(SOC);
    // SOC register: upper byte = integer %, lower byte = decimal %
    uint8_t soc_integer = (soc_raw >> 8) & 0xFF;
    
    // Read status register for alerts
    uint8_t status = readRegister(STATUS);
    bool alert = (status & 0x01) != 0;  // Bit 0 = alert flag
    
    reading.voltage = voltage;
    reading.state_of_charge = soc_integer;
    reading.alert_low_battery = alert;
    reading.timestamp_ms = millis();
    
    lastReading = reading;
    return GAUGE_OK;
}

uint8_t BatteryGauge::getStateOfCharge() {
    BatteryReading rd;
    if (read(rd) == GAUGE_OK) {
        return rd.state_of_charge;
    }
    return 0;
}

float BatteryGauge::getVoltage() {
    BatteryReading rd;
    if (read(rd) == GAUGE_OK) {
        return rd.voltage;
    }
    return 0.0f;
}

bool BatteryGauge::isLowBattery(uint8_t threshold) {
    return getStateOfCharge() < threshold;
}

Gauge_Status BatteryGauge::getLastReading(BatteryReading &reading) {
    if (!initialized) {
        return GAUGE_INIT_FAILED;
    }
    reading = lastReading;
    return GAUGE_OK;
}

bool BatteryGauge::isConnected() {
    if (!initialized) return false;
    
    Wire.beginTransmission(i2c_addr);
    return Wire.endTransmission() == 0;
}

uint8_t BatteryGauge::readRegister(uint8_t reg) {
    Wire.beginTransmission(i2c_addr);
    Wire.write(reg);
    Wire.endTransmission();
    
    Wire.requestFrom(i2c_addr, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;
}

void BatteryGauge::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(i2c_addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint16_t BatteryGauge::readRegister16(uint8_t reg) {
    Wire.beginTransmission(i2c_addr);
    Wire.write(reg);
    Wire.endTransmission();
    
    Wire.requestFrom(i2c_addr, (uint8_t)2);
    uint16_t value = 0;
    if (Wire.available()) {
        value = (Wire.read() << 8);
        value |= Wire.read();
    }
    return value;
}

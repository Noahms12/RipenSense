#ifndef BATTERY_GAUGE_H
#define BATTERY_GAUGE_H

#include <Arduino.h>
#include <Wire.h>

// SparkFun LiPo Fuel Gauge (SX1509) - I2C device for battery monitoring
typedef enum {
    GAUGE_OK = 0,
    GAUGE_INIT_FAILED = -1,
    GAUGE_READ_FAILED = -2,
    GAUGE_I2C_ERROR = -3
} Gauge_Status;

typedef struct {
    float voltage;              // Battery voltage in volts
    uint8_t state_of_charge;    // Battery percentage (0-100%)
    bool alert_low_battery;     // Alert flag for low battery
    uint32_t timestamp_ms;
} BatteryReading;

class BatteryGauge {
public:
    BatteryGauge();
    
    // Initialize I2C fuel gauge (address 0x36 default)
    Gauge_Status init(uint8_t i2c_address = 0x36);
    
    // Read battery voltage and state of charge
    Gauge_Status read(BatteryReading &reading);
    
    // Get state of charge percentage (0-100)
    uint8_t getStateOfCharge();
    
    // Get battery voltage in volts
    float getVoltage();
    
    // Check if battery is low
    bool isLowBattery(uint8_t threshold = 20);  // Default 20%
    
    // Get last reading
    Gauge_Status getLastReading(BatteryReading &reading);
    
    // Check if gauge is connected
    bool isConnected();
    
private:
    uint8_t i2c_addr;
    BatteryReading lastReading;
    bool initialized;
    
    // I2C helper functions
    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);
    uint16_t readRegister16(uint8_t reg);
};

#endif // BATTERY_GAUGE_H

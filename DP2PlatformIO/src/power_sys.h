#ifndef POWER_SYS_H
#define POWER_SYS_H
#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>

class PowerSys {
public:
    PowerSys(uint8_t loadSwitchPin);
    bool begin();
    float getVoltage();
    float getSOC(); // State of Charge (%)
    void setPeripheralPower(bool state);
private:
    SFE_MAX1704X _fuelGauge;
    uint8_t _loadSwitchPin;
};
#endif
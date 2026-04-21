#include "power_sys.h"

PowerSys::PowerSys(uint8_t loadSwitchPin) : _loadSwitchPin(loadSwitchPin) {}

bool PowerSys::begin() {
    pinMode(_loadSwitchPin, OUTPUT);
    setPeripheralPower(true); 
    return _fuelGauge.begin(); 
}

void PowerSys::setPeripheralPower(bool state) {
    digitalWrite(_loadSwitchPin, state ? HIGH : LOW);
}

float PowerSys::getVoltage() {
    return _fuelGauge.getVoltage();
}

float PowerSys::getSOC() {
    return _fuelGauge.getSOC();
}
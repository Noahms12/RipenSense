#include "ds18b20_sensor.h"

DS18B20Sensor::DS18B20Sensor(uint8_t pin) : _oneWire(pin), _sensor(&_oneWire) {}

void DS18B20Sensor::begin() {
    _sensor.begin();
}

float DS18B20Sensor::getTemperatureC() {
    _sensor.requestTemperatures();
    return _sensor.getTempCByIndex(0);
}
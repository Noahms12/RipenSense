#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H
#include <OneWire.h>
#include <DallasTemperature.h>

class DS18B20Sensor {
public:
    DS18B20Sensor(uint8_t pin);
    void begin();
    float getTemperatureC();
private:
    OneWire _oneWire;
    DallasTemperature _sensor;
};
#endif
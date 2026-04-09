#ifndef DS18B20_PROBE_H
#define DS18B20_PROBE_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

typedef enum {
    DS18B20_OK = 0,
    DS18B20_INIT_FAILED = -1,
    DS18B20_READ_FAILED = -2,
    DS18B20_NO_SENSOR = -3,
    DS18B20_CRC_ERROR = -4
} DS18B20_Status;

typedef struct {
    float temperature_c;
    uint32_t timestamp_ms;
} DS18B20_Reading;

class DS18B20Probe {
public:
    // Constructor takes GPIO pin for 1-Wire bus
    DS18B20Probe(uint8_t gpio_pin);
    
    // Initialize 1-Wire bus and detect sensor
    DS18B20_Status init();
    
    // Read temperature from probe
    // Non-blocking: first call triggers conversion, second call gets result
    DS18B20_Status read(DS18B20_Reading &reading);
    
    // Force a synchronous read (blocking ~750ms for 12-bit resolution)
    DS18B20_Status readBlocking(DS18B20_Reading &reading);
    
    // Check if sensor is present
    bool isConnected();
    
    // Get last valid reading
    DS18B20_Status getLastReading(DS18B20_Reading &reading);
    
    // Set resolution (9, 10, 11, or 12 bits; default 12)
    void setResolution(uint8_t bits);
    
private:
    OneWire onewire;
    DallasTemperature sensors;
    DS18B20_Reading lastReading;
    bool initialized;
    bool conversion_in_progress;
    uint32_t conversion_start_ms;
    
    static constexpr uint32_t CONVERSION_TIME_12BIT_MS = 750;
};

#endif // DS18B20_PROBE_H

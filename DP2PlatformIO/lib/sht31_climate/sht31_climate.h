#ifndef SHT31_CLIMATE_H
#define SHT31_CLIMATE_H

#include <Arduino.h>
#include <DFRobot_SHT3x.h>

typedef enum {
    SHT31_OK = 0,
    SHT31_INIT_FAILED = -1,
    SHT31_READ_FAILED = -2,
    SHT31_CHECKSUM_ERROR = -3
} SHT31_Status;

typedef struct {
    float temperature_c;
    float humidity_percent;
    uint32_t timestamp_ms;
} SHT31_Reading;

// Wrapper for DFRobot SEN0331 (SHT31/SHT35) I2C sensor
class SHT31Climate {
public:
    SHT31Climate();
    
    // Initialize I2C sensor (address 0x45 default for SEN0331)
    SHT31_Status init(uint8_t i2c_address = 0x45);
    
    // Read temperature and humidity
    SHT31_Status read(SHT31_Reading &reading);
    
    // Check if sensor is connected
    bool isConnected();
    
    // Get last read values
    SHT31_Status getLastReading(SHT31_Reading &reading);
    
private:
    DFRobot_SHT3x sensor;
    SHT31_Reading lastReading;
    bool initialized;
};

#endif // SHT31_CLIMATE_H
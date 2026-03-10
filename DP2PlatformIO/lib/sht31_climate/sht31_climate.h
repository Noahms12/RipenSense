// sht31_climate.h

// These "Include Guards" prevent the compiler from accidentally 
// copying this file multiple times if different team members include it.
#ifndef SHT31_CLIMATE_H
#define SHT31_CLIMATE_H

#include <Arduino.h>

// Initialize the sensor on the I2C bus
// Returns true if successful, false if the sensor isn't found
bool Climate_Init();

// Fetch the latest temperature in Celsius
float Climate_ReadTemp();

// Fetch the latest relative humidity percentage
float Climate_ReadHumidity();

#endif // SHT31_CLIMATE_H
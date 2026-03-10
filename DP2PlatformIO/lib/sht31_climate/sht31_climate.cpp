// sht31_climate.cpp

#include "sht31_climate.h"
#include <DFRobot_SHT3x.h>  // Include the vendor library

// Instantiate the sensor object. 
// The default I2C address for the SEN0331 is 0x45.
DFRobot_SHT3x sht3x(&Wire, 0x45); 

bool Climate_Init() {
    // sht3x.begin() returns 0 if initialization is successful
    if (sht3x.begin() != 0) {
        return false; 
    }
    
    // Optional: Send a soft reset to ensure it starts in a clean state
    sht3x.softReset();
    
    return true;
}

float Climate_ReadTemp() {
    // The library has a built-in method to get the temperature in Celsius
    return sht3x.getTemperatureC(); 
}

float Climate_ReadHumidity() {
    // The library has a built-in method to get the relative humidity
    return sht3x.getHumidityRH(); 
}
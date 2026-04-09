#ifndef DGS2_GAS_H
#define DGS2_GAS_H

#include <Arduino.h>

typedef enum {
    DGS2_OK = 0,
    DGS2_INIT_FAILED = -1,
    DGS2_READ_FAILED = -2,
    DGS2_WARMUP = -3,        // Sensor still warming up
    DGS2_CRC_ERROR = -4
} DGS2_Status;

typedef struct {
    float ethylene_ppm;
    float sensor_status;      // Internal sensor status (0-100%)
    uint32_t timestamp_ms;
} DGS2_Reading;

class DGS2Gas {
public:
    DGS2Gas(HardwareSerial &serial);
    
    // Initialize UART sensor (default baud 19200)
    DGS2_Status init(unsigned long baud_rate = 19200);
    
    // Read ethylene concentration (ppm)
    // Returns DGS2_WARMUP if sensor is still warming up (typically first 30 seconds)
    DGS2_Status read(DGS2_Reading &reading);
    
    // Check if sensor is connected and responding
    bool isConnected();
    
    // Get time remaining in warmup phase (milliseconds)
    int32_t getWarmupTimeRemaining();
    
    // Get last valid reading
    DGS2_Status getLastReading(DGS2_Reading &reading);
    
private:
    HardwareSerial &serial_;
    bool initialized;
    uint32_t init_time_ms;
    DGS2_Reading lastReading;
    
    static const uint32_t WARMUP_TIME_MS = 30000;  // 30 second warmup
    
    // Helper: Parse UART frame and extract PPM value
    // DGS2 frame format: typically "PPM: " followed by float value
    float parseSerialFrame(const String &frame);
    
    // Helper: Calculate and verify CRC (if applicable)
    bool verifyCRC(const String &frame);
};

#endif // DGS2_GAS_H

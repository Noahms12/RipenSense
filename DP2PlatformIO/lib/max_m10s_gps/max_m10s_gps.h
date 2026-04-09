#ifndef MAX_M10S_GPS_H
#define MAX_M10S_GPS_H

#include <Arduino.h>
#include <Adafruit_GPS.h>

typedef enum {
    GPS_OK = 0,
    GPS_INIT_FAILED = -1,
    GPS_READ_FAILED = -2,
    GPS_NO_FIX = -3,
    GPS_INVALID_DATA = -4
} GPS_Status;

typedef struct {
    bool has_fix;              // Is location fix valid?
    float latitude;            // Degrees, -90 to +90
    float longitude;           // Degrees, -180 to +180
    float altitude_m;          // Meters above sea level
    float speed_knots;         // Speed over ground
    float angle;               // Course over ground (degrees)
    uint8_t satellites;        // Number of satellites in use
    uint32_t timestamp_ms;     // Local timestamp when reading taken
    uint32_t utc_epoch;        // UTC epoch from GPS
} GPS_Reading;

class MAXm10sGPS {
public:
    // Constructor takes reference to HardwareSerial (Serial1 on nRF52840)
    MAXm10sGPS(HardwareSerial &serial);
    
    // Initialize UART GPS (default baud 9600)
    GPS_Status init(unsigned long baud_rate = 9600);
    
    // Read current GPS position
    // Returns GPS_NO_FIX if location not yet available
    GPS_Status read(GPS_Reading &reading);
    
    // Check if GPS has a fix
    bool hasFix();
    
    // Get last valid reading
    GPS_Status getLastReading(GPS_Reading &reading);
    
    // Get TTFF (Time To First Fix) in milliseconds (-1 if never fixed)
    int32_t getTTFF();
    
    // Enable/disable specific NMEA sentences (reduces UART traffic)
    void enableNMEA(uint8_t sentence);  // Sentence: 0=GGA, 1=RMC, 2=GSV, etc.
    void disableNMEA(uint8_t sentence);
    
private:
    Adafruit_GPS gps;
    bool initialized;
    GPS_Reading lastReading;
    uint32_t first_fix_time_ms;
    bool first_fix_obtained;
    
    // Helper: update GPS by reading available UART data
    void updateGPS();
};

#endif // MAX_M10S_GPS_H

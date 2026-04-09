#ifndef RV1805_RTC_H
#define RV1805_RTC_H

#include <Arduino.h>
#include <Adafruit_RV1805.h>

typedef enum {
    RTC_OK = 0,
    RTC_INIT_FAILED = -1,
    RTC_SET_FAILED = -2,
    RTC_READ_FAILED = -3
} RTC_Status;

typedef struct {
    uint16_t year;
    uint8_t month;    // 1-12
    uint8_t day;      // 1-31
    uint8_t hour;     // 0-23
    uint8_t minute;   // 0-59
    uint8_t second;   // 0-59
    uint32_t unix_epoch;  // Seconds since Jan 1 1970 UTC
} RTC_DateTime;

class RV1805RTC {
public:
    RV1805RTC();
    
    // Initialize I2C RTC (address 0x69 default, address bridge may be needed if 0x68 is in use by MPU6050)
    RTC_Status init(uint8_t i2c_address = 0x69);
    
    // Set the current date and time
    RTC_Status setDateTime(const RTC_DateTime &dt);
    
    // Get current date and time
    RTC_Status getDateTime(RTC_DateTime &dt);
    
    // Get Unix timestamp (seconds since epoch)
    uint32_t getUnixTime();
    
    // Convert from unix epoch to datetime
    static RTC_DateTime epochToDateTime(uint32_t epoch);
    
    // Check if RTC is connected
    bool isConnected();
    
private:
    Adafruit_RV1805 rtc;
    bool initialized;
    
    // Helper: convert datetime to unix epoch
    static uint32_t dateTimeToEpoch(const RTC_DateTime &dt);
};

#endif // RV1805_RTC_H

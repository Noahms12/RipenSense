#ifndef RTC_RV1805_H
#define RTC_RV1805_H
#include <Wire.h>
#include <SparkFun_RV1805.h>

class RTC_RV1805 {
public:
    bool begin();
    String getTimestamp();
private:
    RV1805 _rtc;
};
#endif
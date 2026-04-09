#include "rv1805_rtc.h"

RV1805RTC::RV1805RTC() : initialized(false) {}

RTC_Status RV1805RTC::init(uint8_t i2c_address) {
    if (!rtc.begin(i2c_address)) {
        return RTC_INIT_FAILED;
    }
    
    // If the RTC was previously running, it will continue
    // If not, we should set a time (in real scenario, from GPS or external source)
    if (!rtc.initialized()) {
        // Set to compile time as fallback
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    initialized = true;
    return RTC_OK;
}

RTC_Status RV1805RTC::setDateTime(const RTC_DateTime &dt) {
    if (!initialized) {
        return RTC_INIT_FAILED;
    }
    
    DateTime adafruit_dt(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    rtc.adjust(adafruit_dt);
    return RTC_OK;
}

RTC_Status RV1805RTC::getDateTime(RTC_DateTime &dt) {
    if (!initialized) {
        return RTC_INIT_FAILED;
    }
    
    DateTime now = rtc.now();
    dt.year = now.year();
    dt.month = now.month();
    dt.day = now.day();
    dt.hour = now.hour();
    dt.minute = now.minute();
    dt.second = now.second();
    dt.unix_epoch = now.unixtime();
    
    return RTC_OK;
}

uint32_t RV1805RTC::getUnixTime() {
    if (!initialized) {
        return 0;
    }
    return rtc.now().unixtime();
}

RTC_DateTime RV1805RTC::epochToDateTime(uint32_t epoch) {
    DateTime dt(epoch);
    RTC_DateTime result;
    result.year = dt.year();
    result.month = dt.month();
    result.day = dt.day();
    result.hour = dt.hour();
    result.minute = dt.minute();
    result.second = dt.second();
    result.unix_epoch = epoch;
    return result;
}

uint32_t RV1805RTC::dateTimeToEpoch(const RTC_DateTime &dt) {
    DateTime adafruit_dt(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    return adafruit_dt.unixtime();
}

bool RV1805RTC::isConnected() {
    if (!initialized) return false;
    // Simple check: read time
    DateTime now = rtc.now();
    return now.year() >= 2020;  // Sanity check
}

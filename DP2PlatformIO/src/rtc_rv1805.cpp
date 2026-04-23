#include "rtc_rv1805.h"

bool RTC_RV1805::begin() {
    if (!_rtc.begin()) {
        return false;
    }
    _rtc.setToCompilerTime(); // Bootstraps time on first flash
    return true;
}

String RTC_RV1805::getTimestamp() {
    _rtc.updateTime();
    // Wrap the results in String() to allow concatenation
    return String(_rtc.stringDate()) + " " + String(_rtc.stringTime());
}
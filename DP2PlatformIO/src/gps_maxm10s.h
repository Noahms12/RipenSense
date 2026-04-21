#ifndef GPS_MAXM10S_H
#define GPS_MAXM10S_H
#include <Wire.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

class GPS_MAXM10S {
public:
    bool begin();
    long getLatitude();
    long getLongitude();
private:
    SFE_UBLOX_GNSS _gnss;
};
#endif
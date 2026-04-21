#include "gps_maxm10s.h"

bool GPS_MAXM10S::begin() {
    return _gnss.begin();
}

long GPS_MAXM10S::getLatitude() {
    return _gnss.getLatitude();
}

long GPS_MAXM10S::getLongitude() {
    return _gnss.getLongitude();
}
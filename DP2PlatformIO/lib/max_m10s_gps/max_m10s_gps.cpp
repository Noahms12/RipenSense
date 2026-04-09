#include "max_m10s_gps.h"

MAXm10sGPS::MAXm10sGPS(HardwareSerial &serial)
    : gps(&serial), initialized(false), first_fix_time_ms(0), first_fix_obtained(false) {
    memset(&lastReading, 0, sizeof(GPS_Reading));
}

GPS_Status MAXm10sGPS::init(unsigned long baud_rate) {
    gps.begin(baud_rate);
    
    // Configure to use NMEA mode and enable GGA (fix data)
    gps.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
    gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);  // 1 Hz update rate
    gps.sendCommand(PMTK_API_SET_FIX_CTL_5HZ);  // 5 Hz fix update
    
    if (gps.LCHF > 0) {
        initialized = true;
        return GPS_OK;
    }
    
    return GPS_INIT_FAILED;
}

GPS_Status MAXm10sGPS::read(GPS_Reading &reading) {
    if (!initialized) {
        return GPS_INIT_FAILED;
    }
    
    updateGPS();  // Process any available UART data
    
    if (!gps.fix) {
        return GPS_NO_FIX;
    }
    
    reading.has_fix = gps.fix;
    reading.latitude = gps.latitudeDegrees;
    reading.longitude = gps.longitudeDegrees;
    reading.altitude_m = gps.altitude;
    reading.speed_knots = gps.speed;
    reading.angle = gps.angle;
    reading.satellites = gps.satellites;
    reading.timestamp_ms = millis();
    
    // Convert GPS date/time to Unix epoch (placeholder - uses local time)
    reading.utc_epoch = millis() / 1000;  // TODO: proper epoch conversion from GPS time
    
    // Track first fix time
    if (!first_fix_obtained && gps.fix) {
        first_fix_obtained = true;
        first_fix_time_ms = millis();
    }
    
    lastReading = reading;
    return GPS_OK;
}

bool MAXm10sGPS::hasFix() {
    if (!initialized) return false;
    updateGPS();
    return gps.fix;
}

GPS_Status MAXm10sGPS::getLastReading(GPS_Reading &reading) {
    if (!initialized) {
        return GPS_INIT_FAILED;
    }
    reading = lastReading;
    return lastReading.has_fix ? GPS_OK : GPS_NO_FIX;
}

int32_t MAXm10sGPS::getTTFF() {
    if (!first_fix_obtained) {
        return -1;
    }
    return (int32_t)first_fix_time_ms;
}

void MAXm10sGPS::enableNMEA(uint8_t sentence) {
    // TODO: Implement PMTK sentence enable commands
}

void MAXm10sGPS::disableNMEA(uint8_t sentence) {
    // TODO: Implement PMTK sentence disable commands
}

void MAXm10sGPS::updateGPS() {
    // Read and parse available NMEA data
    while (gps.available()) {
        if (!gps.parse(gps.read())) {
            // Parse error, continue
            continue;
        }
    }
}

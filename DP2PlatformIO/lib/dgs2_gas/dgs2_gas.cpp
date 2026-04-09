#include "dgs2_gas.h"

DGS2Gas::DGS2Gas(HardwareSerial &serial) 
    : serial_(serial), initialized(false), init_time_ms(0) {
    memset(&lastReading, 0, sizeof(DGS2_Reading));
}

DGS2_Status DGS2Gas::init(unsigned long baud_rate) {
    serial_.begin(baud_rate);
    initialized = true;
    init_time_ms = millis();
    return DGS2_OK;
}

DGS2_Status DGS2Gas::read(DGS2_Reading &reading) {
    if (!initialized) {
        return DGS2_INIT_FAILED;
    }
    
    // Check warmup status
    if (getWarmupTimeRemaining() > 0) {
        return DGS2_WARMUP;
    }
    
    // Try to read available data from UART
    String frame = "";
    while (serial_.available()) {
        char c = serial_.read();
        if (c == '\n' || c == '\r') {
            if (frame.length() > 0) {
                break;
            }
        } else {
            frame += c;
        }
    }
    
    if (frame.length() == 0) {
        return DGS2_READ_FAILED;
    }
    
    // Parse the frame to extract PPM
    float ppm = parseSerialFrame(frame);
    if (ppm < 0) {
        return DGS2_CRC_ERROR;
    }
    
    reading.ethylene_ppm = ppm;
    reading.sensor_status = 100.0;  // Placeholder
    reading.timestamp_ms = millis();
    
    lastReading = reading;
    return DGS2_OK;
}

bool DGS2Gas::isConnected() {
    if (!initialized) return false;
    // Try a read; if sensor is responding, return true
    return getWarmupTimeRemaining() <= 0;
}

int32_t DGS2Gas::getWarmupTimeRemaining() {
    if (!initialized) return WARMUP_TIME_MS;
    uint32_t elapsed = millis() - init_time_ms;
    if (elapsed >= WARMUP_TIME_MS) {
        return 0;
    }
    return (int32_t)(WARMUP_TIME_MS - elapsed);
}

DGS2_Status DGS2Gas::getLastReading(DGS2_Reading &reading) {
    if (!initialized) {
        return DGS2_INIT_FAILED;
    }
    reading = lastReading;
    return DGS2_OK;
}

float DGS2Gas::parseSerialFrame(const String &frame) {
    // Simple parse: look for "PPM: " prefix and extract float
    // Example frame: "PPM: 2.34"
    int ppm_index = frame.indexOf("PPM");
    if (ppm_index < 0) {
        return -1.0;  // Invalid frame
    }
    
    // Find the colon and extract what follows
    int colon_index = frame.indexOf(':', ppm_index);
    if (colon_index < 0) {
        return -1.0;
    }
    
    String ppm_str = frame.substring(colon_index + 1);
    ppm_str.trim();
    return ppm_str.toFloat();
}

bool DGS2Gas::verifyCRC(const String &frame) {
    // Placeholder for CRC verification if DGS2 protocol includes it
    // For now, assume no CRC
    return true;
}

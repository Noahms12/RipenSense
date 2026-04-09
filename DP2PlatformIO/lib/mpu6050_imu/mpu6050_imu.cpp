#include "mpu6050_imu.h"

MPU6050IMU::MPU6050IMU() : initialized(false) {
    memset(&lastReading, 0, sizeof(MPU6050_Reading));
}

MPU6050_Status MPU6050IMU::init(uint8_t i2c_address) {
    if (!sensor.begin(i2c_address)) {
        return MPU6050_INIT_FAILED;
    }
    
    // Configure for anomaly detection (8G accel, 500 deg/s gyro)
    sensor.setAccelerometerRange(MPU6050_RANGE_8_G);
    sensor.setGyroRange(MPU6050_RANGE_500_DEG);
    sensor.setFilterBandwidth(MPU6050_BAND_21_HZ);
    
    initialized = true;
    return MPU6050_OK;
}

MPU6050_Status MPU6050IMU::read(MPU6050_Reading &reading) {
    if (!initialized) {
        return MPU6050_INIT_FAILED;
    }
    
    sensors_event_t a, g, temp;
    sensor.getEvent(&a, &g, &temp);
    
    reading.accel_x = a.acceleration.x;
    reading.accel_y = a.acceleration.y;
    reading.accel_z = a.acceleration.z;
    reading.gyro_x = g.gyro.x;
    reading.gyro_y = g.gyro.y;
    reading.gyro_z = g.gyro.z;
    reading.timestamp_ms = millis();
    
    lastReading = reading;
    return MPU6050_OK;
}

bool MPU6050IMU::isConnected() {
    // Try a read; if we get valid data, we're connected
    MPU6050_Reading dummy;
    return read(dummy) == MPU6050_OK;
}

MPU6050_Status MPU6050IMU::getLastReading(MPU6050_Reading &reading) {
    if (!initialized) {
        return MPU6050_INIT_FAILED;
    }
    reading = lastReading;
    return MPU6050_OK;
}
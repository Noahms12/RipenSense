#include "mpu6050_imu.h"
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

bool IMU_Init() {
    // The default I2C address for the MPU6050 is 0x68
    if (!mpu.begin(0x68)) {
        return false;
    }
    
    // Set basic hardware configurations for anomaly detection
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    
    return true;
}

IMU_Data IMU_ReadMotion() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    IMU_Data data;
    data.accel_x = a.acceleration.x;
    data.accel_y = a.acceleration.y;
    data.accel_z = a.acceleration.z;
    data.gyro_x = g.gyro.x;
    data.gyro_y = g.gyro.y;
    data.gyro_z = g.gyro.z;
    
    return data;
}
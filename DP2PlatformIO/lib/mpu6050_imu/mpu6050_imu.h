#ifndef MPU6050_IMU_H
#define MPU6050_IMU_H

#include <Arduino.h>

// A struct to easily pass the 6-DoF data back to your main loop
struct IMU_Data {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
};

// Initialize the sensor on the I2C bus
bool IMU_Init();

// Fetch the latest accelerometer and gyroscope data
IMU_Data IMU_ReadMotion();

#endif // MPU6050_IMU_H
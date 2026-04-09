#ifndef MPU6050_IMU_H
#define MPU6050_IMU_H

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

typedef enum {
    MPU6050_OK = 0,
    MPU6050_INIT_FAILED = -1,
    MPU6050_READ_FAILED = -2
} MPU6050_Status;

typedef struct {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    uint32_t timestamp_ms;
} MPU6050_Reading;

class MPU6050IMU {
public:
    MPU6050IMU();
    
    // Initialize I2C sensor (address 0x68 default)
    MPU6050_Status init(uint8_t i2c_address = 0x68);
    
    // Read 6-axis motion data
    MPU6050_Status read(MPU6050_Reading &reading);
    
    // Check if sensor is connected
    bool isConnected();
    
    // Get last read values
    MPU6050_Status getLastReading(MPU6050_Reading &reading);
    
private:
    Adafruit_MPU6050 sensor;
    MPU6050_Reading lastReading;
    bool initialized;
};

#endif // MPU6050_IMU_H
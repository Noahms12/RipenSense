#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <Arduino.h>
#include <Adafruit_SPIFlash.h>

typedef enum {
    FLASH_OK = 0,
    FLASH_INIT_FAILED = -1,
    FLASH_WRITE_FAILED = -2,
    FLASH_READ_FAILED = -3,
    FLASH_FULL = -4,
    FLASH_INVALID_INDEX = -5
} Flash_Status;

// Core sensor data record (~60 bytes)
typedef struct {
    uint32_t timestamp_unix;
    float temperature_c;
    float humidity_percent;
    float accel_x, accel_y, accel_z;  // G's
    float gyro_x, gyro_y, gyro_z;     // deg/s
    float ethylene_ppm;
    float probe_temperature_c;
    float gps_latitude;
    float gps_longitude;
    float gps_altitude_m;
    float anomaly_score;               // 0.0-1.0
    uint8_t ripeness_category;         // 0=green, 1=yellow, 2=brown, 3=rotten
} SensorDataRecord;

class FlashStorage {
public:
    // Constructor (uses default SPI pins for nRF52840)
    FlashStorage();
    
    // Initialize SPI FLASH (W25Q16 or compatible)
    Flash_Status init();
    
    // Write a sensor record to FLASH (appends or uses ring buffer)
    Flash_Status write(const SensorDataRecord &record);
    
    // Read record at index (0 = oldest, count-1 = newest)
    Flash_Status read(uint32_t index, SensorDataRecord &record);
    
    // Get total number of records stored
    uint32_t getCount();
    
    // Get max capacity (number of records)
    uint32_t getCapacity();
    
    // Check if FLASH is full (important for ring buffer decision)
    bool isFull();
    
    // Format FLASH (erase all data)
    Flash_Status format();
    
    // Export all records as CSV to serial (for Bluetooth export)
    Flash_Status exportAsCSV(Stream &output);
    
    // Export all records as JSON to serial (alternative format)
    Flash_Status exportAsJSON(Stream &output);
    
    // Erase oldest N records (for cleanup)
    Flash_Status eraseOldest(uint32_t count);
    
    // Check FLASH connectivity
    bool isConnected();
    
private:
    Adafruit_SPIFlash flash;
    uint32_t record_count;
    uint32_t write_index;  // For ring buffer
    bool initialized;
    
    static constexpr uint32_t RECORD_SIZE = sizeof(SensorDataRecord);
    static constexpr uint32_t USABLE_FLASH_SIZE = 2 * 1024 * 1024;  // 2 MB for W25Q16
    static constexpr uint32_t CAPACITY = USABLE_FLASH_SIZE / RECORD_SIZE;  // ~33k records
    
    // Helper: Calculate flash address for record at index
    uint32_t getAddressForIndex(uint32_t index);
    
    // Helper: Erase sector containing address
    Flash_Status eraseSector(uint32_t address);
};

#endif // FLASH_STORAGE_H

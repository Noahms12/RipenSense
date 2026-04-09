#include "flash_storage.h"

FlashStorage::FlashStorage() : record_count(0), write_index(0), initialized(false) {}

Flash_Status FlashStorage::init() {
    if (!flash.begin()) {
        return FLASH_INIT_FAILED;
    }
    initialized = true;
    return FLASH_OK;
}

Flash_Status FlashStorage::write(const SensorDataRecord &record) {
    if (!initialized) {
        return FLASH_INIT_FAILED;
    }
    
    if (record_count >= CAPACITY) {
        // Ring buffer: overwrite oldest
        write_index = (write_index + 1) % CAPACITY;
    } else {
        record_count++;
    }
    
    uint32_t address = getAddressForIndex(write_index);
    
    if (!flash.writeBuffer(address, (uint8_t *)&record, RECORD_SIZE)) {
        return FLASH_WRITE_FAILED;
    }
    
    write_index = (write_index + 1) % CAPACITY;
    return FLASH_OK;
}

Flash_Status FlashStorage::read(uint32_t index, SensorDataRecord &record) {
    if (!initialized) {
        return FLASH_INIT_FAILED;
    }
    
    if (index >= record_count) {
        return FLASH_INVALID_INDEX;
    }
    
    uint32_t address = getAddressForIndex(index);
    
    if (!flash.readBuffer(address, (uint8_t *)&record, RECORD_SIZE)) {
        return FLASH_READ_FAILED;
    }
    
    return FLASH_OK;
}

uint32_t FlashStorage::getCount() {
    return record_count;
}

uint32_t FlashStorage::getCapacity() {
    return CAPACITY;
}

bool FlashStorage::isFull() {
    return record_count >= CAPACITY;
}

Flash_Status FlashStorage::format() {
    if (!initialized) {
        return FLASH_INIT_FAILED;
    }
    
    // Erase entire FLASH
    flash.eraseChip();
    record_count = 0;
    write_index = 0;
    
    return FLASH_OK;
}

Flash_Status FlashStorage::exportAsCSV(Stream &output) {
    if (!initialized) {
        return FLASH_INIT_FAILED;
    }
    
    // Write CSV header
    output.println(F("timestamp_unix,temperature_c,humidity_percent,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,ethylene_ppm,probe_temp_c,gps_lat,gps_lon,gps_alt_m,anomaly_score,ripeness_category"));
    
    // Write records
    for (uint32_t i = 0; i < record_count; i++) {
        SensorDataRecord rec;
        if (read(i, rec) != FLASH_OK) {
            continue;
        }
        
        // Format and output CSV line
        output.print(rec.timestamp_unix);
        output.print(",");
        output.print(rec.temperature_c);
        output.print(",");
        output.print(rec.humidity_percent);
        output.print(",");
        output.print(rec.accel_x);
        output.print(",");
        output.print(rec.accel_y);
        output.print(",");
        output.print(rec.accel_z);
        output.print(",");
        output.print(rec.gyro_x);
        output.print(",");
        output.print(rec.gyro_y);
        output.print(",");
        output.print(rec.gyro_z);
        output.print(",");
        output.print(rec.ethylene_ppm);
        output.print(",");
        output.print(rec.probe_temperature_c);
        output.print(",");
        output.print(rec.gps_latitude);
        output.print(",");
        output.print(rec.gps_longitude);
        output.print(",");
        output.print(rec.gps_altitude_m);
        output.print(",");
        output.print(rec.anomaly_score);
        output.print(",");
        output.println((int)rec.ripeness_category);
    }
    
    return FLASH_OK;
}

Flash_Status FlashStorage::exportAsJSON(Stream &output) {
    if (!initialized) {
        return FLASH_INIT_FAILED;
    }
    
    output.println("[");
    
    for (uint32_t i = 0; i < record_count; i++) {
        SensorDataRecord rec;
        if (read(i, rec) != FLASH_OK) {
            continue;
        }
        
        output.print("  {");
        output.print("\"ts\":");
        output.print(rec.timestamp_unix);
        output.print(",\"temp\":");
        output.print(rec.temperature_c);
        output.print(",\"humidity\":");
        output.print(rec.humidity_percent);
        output.print(",\"accel\":[");
        output.print(rec.accel_x);
        output.print(",");
        output.print(rec.accel_y);
        output.print(",");
        output.print(rec.accel_z);
        output.print("],\"gyro\":[");
        output.print(rec.gyro_x);
        output.print(",");
        output.print(rec.gyro_y);
        output.print(",");
        output.print(rec.gyro_z);
        output.print("],\"ethylene\":");
        output.print(rec.ethylene_ppm);
        output.print(",\"probe_temp\":");
        output.print(rec.probe_temperature_c);
        output.print(",\"gps\":[");
        output.print(rec.gps_latitude);
        output.print(",");
        output.print(rec.gps_longitude);
        output.print(",");
        output.print(rec.gps_altitude_m);
        output.print("],\"anomaly\":");
        output.print(rec.anomaly_score);
        output.print(",\"ripeness\":");
        output.print((int)rec.ripeness_category);
        
        if (i < record_count - 1) {
            output.println("},");
        } else {
            output.println("}");
        }
    }
    
    output.println("]");
    return FLASH_OK;
}

Flash_Status FlashStorage::eraseOldest(uint32_t count) {
    if (count > record_count) {
        return FLASH_INVALID_INDEX;
    }
    
    // Ring buffer: oldest automatically overwrites
    // This is a no-op for ring buffer mode
    return FLASH_OK;
}

bool FlashStorage::isConnected() {
    if (!initialized) return false;
    // Try a simple JEDEC ID read
    uint32_t jedec = flash.getJEDECID();
    return jedec > 0;
}

uint32_t FlashStorage::getAddressForIndex(uint32_t index) {
    return index * RECORD_SIZE;
}

Flash_Status FlashStorage::eraseSector(uint32_t address) {
    if (!flash.eraseSector(address)) {
        return FLASH_WRITE_FAILED;
    }
    return FLASH_OK;
}

// ============================================================================
// RipenSense Firmware: Banana Ripeness Monitoring with Edge-AI Anomaly Detection
// Adafruit Feather nRF52840 + TensorFlow Lite Micro
// ============================================================================

// Sensor Libraries
#include <sht31_climate.h>
#include <mpu6050_imu.h>
#include <rv1805_rtc.h>
#include <dgs2_gas.h>
#include <ds18b20_probe.h>
#include <max_m10s_gps.h>
#include <flash_storage.h>
#include <anomaly_model.h>
#include <battery_gauge.h>

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================

// Button pin for export trigger (GPIO 12 - adjust to your board)
// Alternatively, could be triggered by NFC field detection interrupt
#define EXPORT_BUTTON_PIN 12

// ============================================================================
// GLOBAL SENSOR INSTANCES
// ============================================================================

SHT31Climate climate;
MPU6050IMU imu;
RV1805RTC rtc;
DGS2Gas gas_sensor(Serial1);           // UART1 for DGS2 (pins TX=11, RX=9 on Feather)
DS18B20Probe probe_temp(30);           // GPIO 30 for 1-Wire (adjust pin as needed)
MAXm10sGPS gps(Serial2);               // UART2 for GPS (pins TX=47, RX=46 on Feather)
FlashStorage flash;
AnomalyModel model;
BatteryGauge battery;

// ============================================================================
// TIMING & CONFIGURATION
// ============================================================================

const unsigned long INFERENCE_INTERVAL_MS = 10000;    // 10 seconds between samples
const unsigned long GPS_UPDATE_INTERVAL_MS = 60000;   // GPS update every 60 seconds
const unsigned long BATTERY_CHECK_INTERVAL_MS = 30000; // Check battery every 30 seconds
const uint8_t ANOMALY_THRESHOLD = 220;                // Anomaly threshold (ripeness score ~> 0.85)
const uint8_t LOW_BATTERY_THRESHOLD = 20;             // Alert if < 20%

unsigned long lastInferenceTime = 0;
unsigned long lastGPSUpdateTime = 0;
unsigned long lastBatteryCheckTime = 0;
volatile bool export_requested = false;               // Flag from button press or export trigger

// ============================================================================
// SETUP: Initialize All Sensors & Systems
// ============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    delay(1000);  // Wait for serial to stabilize
    
    Serial.println("\n=========================================");
    Serial.println("RipenSense Firmware Initialization");
    Serial.println("=========================================\n");
    
    // --- Initialize Climate Sensor (I2C, SHT31) ---
    Serial.print("Initializing SHT31 Climate Sensor... ");
    if (climate.init(0x45) == SHT31_OK) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED");
    }
    
    // --- Initialize IMU (I2C, MPU6050) ---
    Serial.print("Initializing MPU6050 IMU... ");
    if (imu.init(0x68) == MPU6050_OK) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED");
    }
    
    // --- Initialize RTC (I2C, RV1805) ---
    // NOTE: Address collision at 0x68 with MPU6050! Use address bridge or separate I2C bus
    Serial.print("Initializing RV1805 RTC (0x69)... ");
    if (rtc.init(0x69) == RTC_OK) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED - Will use system millis() for timestamps");
    }
    
    // --- Initialize Gas Sensor (UART1, DGS2) ---
    Serial.print("Initializing DGS2 Gas Sensor... ");
    if (gas_sensor.init(19200) == DGS2_OK) {
        Serial.println("OK (Warming up...)");
    } else {
        Serial.println("FAILED");
    }
    
    // --- Initialize Probe Temperature (1-Wire, DS18B20) ---
    Serial.print("Initializing DS18B20 Probe Temperature... ");
    if (probe_temp.init() == DS18B20_OK) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED");
    }
    
    // --- Initialize Battery Fuel Gauge (I2C, 0x36) ---
    Serial.print("Initializing SparkFun LiPo Fuel Gauge... ");
    if (battery.init() == GAUGE_OK) {
        uint8_t soc = battery.getStateOfCharge();
        float voltage = battery.getVoltage();
        Serial.print("OK (");
        Serial.print(soc);
        Serial.print("%, ");
        Serial.print(voltage);
        Serial.println("V)");
    } else {
        Serial.println("FAILED");
    }
    
    // --- Initialize Export Button (GPIO pull-up, falling edge interrupt) ---
    // Physical button on GPIO 12 triggers data export (or attach NFC field detector)
    pinMode(EXPORT_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(EXPORT_BUTTON_PIN), 
                    []() { export_requested = true; }, 
                    FALLING);
    Serial.println("Export button: Ready (GPIO " + String(EXPORT_BUTTON_PIN) + ")");
    Serial.println("  Passive NTAG213 tags pre-written with shipment ID for tracking");
    
    // --- Initialize SPI FLASH Storage ---
    Serial.print("Initializing SPI FLASH Storage... ");
    if (flash.init() == FLASH_OK) {
        Serial.print("OK (Capacity: ");
        Serial.print(flash.getCapacity());
        Serial.println(" records)");
    } else {
        Serial.println("FAILED");
    }
    
    // --- Initialize TensorFlow Lite Model ---
    Serial.print("Initializing TFLite Anomaly Model... ");
    if (model.init() == MODEL_OK) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED");
    }
    
    Serial.println("\nInitialization Complete!\n");
    Serial.println("Starting sensor acquisition loop...\n");
}

// ============================================================================
// GPS ASYNC UPDATE (non-blocking GPS data acquisition)
// ============================================================================

GPS_Reading lastGPSReading = {false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0};

void updateGPS() {
    if ((millis() - lastGPSUpdateTime) >= GPS_UPDATE_INTERVAL_MS) {
        lastGPSUpdateTime = millis();
        GPS_Reading gps_reading;
        if (gps.read(gps_reading) == GPS_OK) {
            lastGPSReading = gps_reading;
            Serial.print("GPS Fix: ");
            Serial.print(gps_reading.latitude, 6);
            Serial.print(", ");
            Serial.println(gps_reading.longitude, 6);
        } else if (gps.read(gps_reading) == GPS_NO_FIX) {
            Serial.println("GPS: Searching for fix...");
        }
    }
}

// ============================================================================
// BATTERY STATUS CHECK (non-blocking battery monitoring)
// ============================================================================

void checkBatteryStatus() {
    if ((millis() - lastBatteryCheckTime) >= BATTERY_CHECK_INTERVAL_MS) {
        lastBatteryCheckTime = millis();
        BatteryReading battery_reading;
        if (battery.read(battery_reading) == GAUGE_OK) {
            Serial.print("Battery: ");
            Serial.print(battery_reading.state_of_charge);
            Serial.print("% (");
            Serial.print(battery_reading.voltage, 2);
            Serial.println("V)");
            
            if (battery_reading.alert_low_battery || battery_reading.state_of_charge < LOW_BATTERY_THRESHOLD) {
                Serial.println("  WARNING: Low battery!");
            }
        }
    }
}

// ============================================================================
// EXPORT DATA via BLUETOOTH
// ============================================================================

void exportData() {
    Serial.println("\n--- Exporting Data ---");
    Serial.println("Dumping FLASH buffer via Serial (Bluetooth export in Phase 2)");
    Serial.println("Format: CSV");
    Serial.println("");
    
    // Dump FLASH buffer as CSV to Serial
    // In Phase 2, this will be piped to Bluetooth BLE characteristic
    flash.exportAsCSV(Serial);
    
    Serial.println("\n--- End Export ---\n");
}

// ============================================================================
// MAIN LOOP: 4-Phase Inference Pipeline
// ============================================================================

void loop() {
    // Non-blocking timing check
    if ((millis() - lastInferenceTime) >= INFERENCE_INTERVAL_MS) {
        lastInferenceTime = millis();
        
        Serial.print("\n[");
        Serial.print(millis() / 1000);
        Serial.println("s] === PHASE A: Sensor Acquisition ===");
        
        // ========== PHASE A: DATA ACQUISITION ==========
        
        // Get current timestamp
        uint32_t timestamp_unix = rtc.getUnixTime();
        if (timestamp_unix == 0) {
            // Fallback if RTC not working: use local time
            timestamp_unix = millis() / 1000;
        }
        
        // Read Climate (Temperature & Humidity)
        SHT31_Reading climate_reading;
        SHT31_Status climate_status = climate.read(climate_reading);
        if (climate_status == SHT31_OK) {
            Serial.print("  SHT31: ");
            Serial.print(climate_reading.temperature_c);
            Serial.print("°C, ");
            Serial.print(climate_reading.humidity_percent);
            Serial.println("%");
        } else {
            Serial.println("  SHT31: Read failed");
        }
        
        // Read IMU (6-axis motion)
        MPU6050_Reading imu_reading;
        MPU6050_Status imu_status = imu.read(imu_reading);
        if (imu_status == MPU6050_OK) {
            Serial.print("  MPU6050: Accel=[");
            Serial.print(imu_reading.accel_x);
            Serial.print(",");
            Serial.print(imu_reading.accel_y);
            Serial.print(",");
            Serial.print(imu_reading.accel_z);
            Serial.print("] G");
            Serial.println();
        } else {
            Serial.println("  MPU6050: Read failed");
        }
        
        // Read Gas Sensor (Ethylene)
        DGS2_Reading gas_reading;
        DGS2_Status gas_status = gas_sensor.read(gas_reading);
        if (gas_status == DGS2_OK) {
            Serial.print("  DGS2: ");
            Serial.print(gas_reading.ethylene_ppm);
            Serial.println(" ppm");
        } else if (gas_status == DGS2_WARMUP) {
            Serial.println("  DGS2: Warming up...");
        } else {
            Serial.println("  DGS2: Read failed");
        }
        
        // Read Probe Temperature (DS18B20)
        DS18B20_Reading probe_reading;
        DS18B20_Status probe_status = probe_temp.readBlocking(probe_reading);
        if (probe_status == DS18B20_OK) {
            Serial.print("  DS18B20: ");
            Serial.print(probe_reading.temperature_c);
            Serial.println("°C");
        } else {
            Serial.println("  DS18B20: Read failed");
        }
        
        Serial.println("\n[Phase B: Anomaly Inference]");
        
        // ========== PHASE B: INFERENCE ==========
        
        // Construct feature vector from sensor readings
        Model_FeatureVector features;
        features.temperature_c = (climate_status == SHT31_OK) ? climate_reading.temperature_c : 20.0f;
        features.humidity_percent = (climate_status == SHT31_OK) ? climate_reading.humidity_percent : 50.0f;
        features.accel_x = (imu_status == MPU6050_OK) ? imu_reading.accel_x : 0.0f;
        features.accel_y = (imu_status == MPU6050_OK) ? imu_reading.accel_y : 0.0f;
        features.accel_z = (imu_status == MPU6050_OK) ? imu_reading.accel_z : 0.0f;
        features.gyro_x = (imu_status == MPU6050_OK) ? imu_reading.gyro_x : 0.0f;
        features.gyro_y = (imu_status == MPU6050_OK) ? imu_reading.gyro_y : 0.0f;
        features.gyro_z = (imu_status == MPU6050_OK) ? imu_reading.gyro_z : 0.0f;
        features.ethylene_ppm = (gas_status == DGS2_OK) ? gas_reading.ethylene_ppm : 0.0f;
        features.probe_temperature_c = (probe_status == DS18B20_OK) ? probe_reading.temperature_c : 20.0f;
        
        // Run inference
        Model_Output inference_result;
        Model_Status model_status = model.runInference(features, inference_result);
        
        if (model_status == MODEL_OK) {
            Serial.print("  Anomaly Score: ");
            Serial.print(inference_result.anomaly_score, 3);
            Serial.print(" (Inference time: ");
            Serial.print(inference_result.inference_time_ms);
            Serial.println(" ms)");
        } else {
            Serial.println("  Inference failed!");
            inference_result.anomaly_score = 0.0f;
        }
        
        // Convert to ripeness values
        uint8_t ripeness_numeric = AnomalyModel::scoreToRipeness(inference_result.anomaly_score);
        uint8_t ripeness_category = AnomalyModel::scoreToCategory(inference_result.anomaly_score);
        
        const char *category_names[] = {"Green", "Yellow", "Brown", "Rotten"};
        Serial.print("  Ripeness: ");
        Serial.print(ripeness_numeric);
        Serial.print("/100 (");
        Serial.print(category_names[ripeness_category]);
        Serial.println(")");
        
        Serial.println("\n[Phase C: Decision & Logging]");
        
        // ========== PHASE C: DECISION & LOGGING ==========
        
        // Create data record for storage
        SensorDataRecord record;
        record.timestamp_unix = timestamp_unix;
        record.temperature_c = features.temperature_c;
        record.humidity_percent = features.humidity_percent;
        record.accel_x = features.accel_x;
        record.accel_y = features.accel_y;
        record.accel_z = features.accel_z;
        record.gyro_x = features.gyro_x;
        record.gyro_y = features.gyro_y;
        record.gyro_z = features.gyro_z;
        record.ethylene_ppm = features.ethylene_ppm;
        record.probe_temperature_c = features.probe_temperature_c;
        record.gps_latitude = lastGPSReading.latitude;
        record.gps_longitude = lastGPSReading.longitude;
        record.gps_altitude_m = lastGPSReading.altitude_m;
        record.anomaly_score = inference_result.anomaly_score;
        record.ripeness_category = ripeness_category;
        
        // Write to FLASH
        Flash_Status flash_status = flash.write(record);
        if (flash_status == FLASH_OK) {
            Serial.print("  Logged to FLASH (");
            Serial.print(flash.getCount());
            Serial.println(" records)");
        } else if (flash_status == FLASH_FULL) {
            Serial.println("  FLASH: Buffer full - ring buffer wrapping");
        } else {
            Serial.println("  FLASH: Write failed");
        }
        
        // Check for anomaly
        if (inference_result.anomaly_score > 0.85) {
            Serial.println("  *** ANOMALY DETECTED! ***");
            // In production: trigger alert, log incident, notify user
        }
        
        // Check for export request
        if (export_requested) {
            exportData();
            export_requested = false;
        }
        
        Serial.println("\n[Phase D: Power Management]");
        
        // ========== PHASE D: POWER MANAGEMENT ==========
        
        // Update GPS asynchronously (every 60 seconds)
        updateGPS();
        
        // Check battery status asynchronously (every 30 seconds)
        checkBatteryStatus();
        
        // TODO: Implement deep-sleep or BLE sleep mode for power optimization
        Serial.println("  (Sleep mode placeholder)\n");
    }
}
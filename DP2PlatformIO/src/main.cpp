// Hardware Abstraction Modules
#include <sht31_climate.h>

// Uncomment when create folders in the lib/ directory
// #include <dgs2_gas.h>
// #include <ds18b20_probe.h>
// #include <mpu6050_imu.h>
// #include <rv1805_rtc.h>
// #include <anomaly_model.h>

// 2. Define global timing variables
unsigned long lastInferenceTime = 0;
const unsigned long INFERENCE_INTERVAL_MS = 10000; // Run every 10 seconds

void setup() {
    // Initialize USB serial for debugging
    Serial.begin(115200);
    while (!Serial) delay(10); 
    
    Serial.println("Initializing Environmental Anomaly Detector...");

    // Initialize all hardware components
    // If a sensor fails to start, the function should return false to flag a hardware error

    // Commented out until we create the corresponding .h and .cpp files in the lib/ directory
    // if (!RTC_Init())          Serial.println("ERR: RV-1805 RTC failed!");
    // if (!GasSensor_Init())    Serial.println("ERR: DGS2 UART failed!");
    // if (!Climate_Init())      Serial.println("ERR: SHT31 I2C failed!");
    // if (!TempProbe_Init())    Serial.println("ERR: DS18B20 1-Wire failed!");
    // if (!IMU_Init())          Serial.println("ERR: MPU6050 I2C failed!");
    
    // Load the Edge-AI model weights into memory

    // Commented out until we create the corresponding .h and .cpp files in the lib/ directory
    //if (!AnomalyModel_Init()) Serial.println("ERR: Edge-AI Model failed to load!");
    
    Serial.println("System Ready.");
}

void loop() {
    // 3. Non-blocking timing check
    if (millis() - lastInferenceTime >= INFERENCE_INTERVAL_MS) {
        lastInferenceTime = millis();
        
        // --- PHASE A: DATA ACQUISITION ---

        // Commented out until we create the corresponding .h and .cpp files in the lib/ directory
        // String timestamp   = RTC_GetTimestamp();
        // float ethylene_ppm = GasSensor_ReadPPM();
        // float ambient_temp = Climate_ReadTemp();
        // float ambient_hum  = Climate_ReadHumidity();
        // float probe_temp   = TempProbe_ReadTemp();
        // IMU_Data imu_data  = IMU_ReadMotion(); 

        // --- PHASE B: EDGE-AI INFERENCE ---
        // Pass the fresh environmental data to your anomaly model

        // Commented out until we create the corresponding .h and .cpp files in the lib/ directory
        // float anomaly_score = AnomalyModel_RunInference(
        //     ethylene_ppm, 
        //     ambient_temp, 
        //     ambient_hum, 
        //     probe_temp, 
        //     imu_data.accel_z, 
        //     imu_data.gyro_z
        // );

        // --- PHASE C: DECISION & LOGGING ---
        Serial.print("[" + timestamp + "] ");
        Serial.print("Ethylene: " + String(ethylene_ppm) + "ppm | ");
        Serial.print("Anomaly Score: " + String(anomaly_score));

        if (anomaly_score > 0.85) {
            Serial.println(" *** ANOMALY DETECTED! ***");
            // Trigger alerts, save to SD card, or transmit via radio
        } else {
            Serial.println(" - Normal");
        }
        
        // --- PHASE D: POWER MANAGEMENT ---
        // Insert sleep code here to turn off sensors and put the nRF52840 into deep sleep
    }
}
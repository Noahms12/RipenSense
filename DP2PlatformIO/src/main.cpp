#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <bluefruit.h>

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_SPIFlash.h>
#include <InternalFileSystem.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <RipenSense_inferencing.h>

// ---------------------------------------------------------------------------
// Pin / bus config
// ---------------------------------------------------------------------------
#define ONE_WIRE_BUS        5
#define UTC_OFFSET_HOURS   -4       // EDT; change to -5 for EST

// ---------------------------------------------------------------------------
// Flash config
// External: Adafruit SPI flash breakout via QSPI or SPI
// Fallback:  nRF52840 onboard flash via internal file system (LittleFS)
// ---------------------------------------------------------------------------
#define FLASH_CSV_FILENAME  "/ripensense_log.csv"
#define CSV_HEADER          "timestamp,lat,lon,temp_c,humidity_rh,ethylene_ppb," \
                            "probe_temp_c,max_g_last_60s,shock_count_last_60s," \
                            "vpd_kpa,stage,ri_cumulative,anomaly_score," \
                            "battery_pct,battery_v,model_version\n"
#define MODEL_VERSION       "A1"

// External SPI flash (Adafruit breakout, QSPI transport)
Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash             spiFlash(&flashTransport);
FatVolume                     fatfs;

// Flash state
bool     extFlashOk      = false;
bool     useOnboardFlash = false;
bool     flashFull       = false;
File32   extLogFile;                              // FatFS handle for external flash
Adafruit_LittleFS_Namespace::File intLogFile;     // LittleFS handle for onboard flash

// ---------------------------------------------------------------------------
// Shock window
// IMU is sampled at ~50Hz between log writes.
// We track max_g and shock count over the last 60 seconds.
// ---------------------------------------------------------------------------
#define SHOCK_SAMPLE_INTERVAL_MS  20    // ~50Hz IMU sampling
#define SHOCK_WINDOW_MS           60000 // 60 second window
#define SHOCK_THRESHOLD_G         10.0f
#define LOG_INTERVAL_MS           60000 // 1 minute between log writes

float    shockMaxG       = 1.0f;
int      shockCount      = 0;
uint32_t lastShockSample = 0;
uint32_t lastLogTime     = 0;

// ---------------------------------------------------------------------------
// ML inference window
// We maintain a rolling buffer of the last 30 feature rows for inference.
// 9 features x 30 timesteps = 270 floats
// ---------------------------------------------------------------------------
#define FEATURE_COUNT   9
#define WINDOW_SIZE     30

float featureWindow[WINDOW_SIZE][FEATURE_COUNT];
int   windowHead    = 0;
int   windowFilled  = 0;   // rows filled so far, caps at WINDOW_SIZE

// Feature indices (must match training column order)
#define FEAT_TEMP       0
#define FEAT_HUMIDITY   1
#define FEAT_ETHYLENE   2
#define FEAT_PROBE_TEMP 3
#define FEAT_MAX_G      4
#define FEAT_SHOCK_CNT  5
#define FEAT_VPD        6
#define FEAT_STAGE      7
#define FEAT_RI         8

// ---------------------------------------------------------------------------
// RI / VPD (mirrors datagen physics so on-device RI matches training data)
// ---------------------------------------------------------------------------
#define W_E         0.04f
#define W_T         0.08f
#define W_H         0.01f
#define TEMP_REF    13.0f
#define ETH_THRESH  100.0f   // ppb

float vpSat(float t) {
    return 0.61078f * exp(17.27f * t / (t + 237.3f));
}
float computeVPD(float t, float rh) {
    return vpSat(t) * (1.0f - rh / 100.0f);
}
float vpdOptimal = -1.0f;  // set in setup()

int getStage(float ri) {
    if (ri <  14.0f) return 1;
    if (ri <  28.0f) return 2;
    if (ri <  42.0f) return 3;
    if (ri <  56.0f) return 4;
    if (ri <  70.0f) return 5;
    if (ri <  85.0f) return 6;
    return 7;
}

float getQ10(int stage) {
    switch (stage) {
        case 1: return 2.25f;
        case 2: return 3.75f;
        case 3: return 3.00f;
        case 4: return 2.50f;
        case 5: return 2.00f;
        case 6: return 1.80f;
        default: return 1.50f;
    }
}

float riCumulative = 2.0f;  // start at stage 1

float computeRIDelta(float tempC, float rh, float ethylenePpb, float q10) {
    float eTerm = W_E * max(0.0f, ethylenePpb / ETH_THRESH);
    float tExcess = max(0.0f, (float)pow(q10, (tempC - TEMP_REF) / 10.0f) - 1.0f);
    float tTerm = W_T * tExcess;
    float vpdExcess = max(0.0f, computeVPD(tempC, rh) - vpdOptimal);
    float hTerm = W_H * vpdExcess;
    return eTerm + tTerm + hTerm;
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------
Adafruit_MPU6050  imu;
Adafruit_SHT31    sht31;
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
SFE_MAX1704X      fuelGauge(MAX1704X_MAX17048);
SFE_UBLOX_GNSS    gnss;

bool imuOk   = false;
bool sht31Ok = false;
bool gaugeOk = false;
bool gnssOk  = false;

BLEUart bleuart;

// ---------------------------------------------------------------------------
// UTC -> EDT conversion
// ---------------------------------------------------------------------------
void utcToEdt(int y, int mo, int d, int h, int mi, int s,
              int &oy, int &omo, int &od, int &oh, int &omi, int &os) {
    static const int dim[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    oh = h + UTC_OFFSET_HOURS; omi = mi; os = s;
    od = d; omo = mo; oy = y;
    if (oh < 0) {
        oh += 24; od--;
        if (od < 1) {
            omo--;
            if (omo < 1) { omo = 12; oy--; }
            bool leap = (oy%4==0 && (oy%100!=0 || oy%400==0));
            od = (omo==2 && leap) ? 29 : dim[omo];
        }
    } else if (oh >= 24) {
        oh -= 24; od++;
        bool leap = (oy%4==0 && (oy%100!=0 || oy%400==0));
        int days = (omo==2 && leap) ? 29 : dim[omo];
        if (od > days) { od = 1; omo++; if (omo > 12) { omo=1; oy++; } }
    }
}

// ---------------------------------------------------------------------------
// Flash helpers
// ---------------------------------------------------------------------------
bool initExternalFlash() {
    if (!spiFlash.begin()) {
        Serial.println("Flash: SPI flash init failed");
        return false;
    }
    if (!fatfs.begin(&spiFlash)) {
        Serial.println("Flash: FAT filesystem not found, formatting...");
        // Format via SdFat's FatFormatter
        uint8_t workbuf[512];
        FatFormatter formatter;
        if (!formatter.format(&spiFlash, workbuf, nullptr)) {
            Serial.println("Flash: format failed");
            return false;
        }
        if (!fatfs.begin(&spiFlash)) {
            Serial.println("Flash: mount after format failed");
            return false;
        }
    }
    // Open log file, write header if new
    bool isNew = !fatfs.exists(FLASH_CSV_FILENAME);
    extLogFile = fatfs.open(FLASH_CSV_FILENAME, FILE_WRITE);
    if (!extLogFile) {
        Serial.println("Flash: failed to open log file");
        return false;
    }
    if (isNew) extLogFile.print(CSV_HEADER);
    Serial.println("Flash: external SPI flash OK");
    return true;
}

void writeRowToFlash(const char* row) {
    if (flashFull) return;

    if (extFlashOk && !useOnboardFlash) {
        uint32_t used  = extLogFile.size();
        uint32_t total = spiFlash.size();
        if (used + strlen(row) + 10 > total * 95 / 100) {
            Serial.println("Flash: external full, falling back to onboard flash");
            extLogFile.close();
            useOnboardFlash = true;
            InternalFS.begin();
            intLogFile = InternalFS.open(FLASH_CSV_FILENAME,
                             Adafruit_LittleFS_Namespace::FILE_O_WRITE);
            if (!intLogFile) {
                Serial.println("Flash: onboard flash open failed -- stopping logging");
                flashFull = true;
                return;
            }
            intLogFile.print(CSV_HEADER);
        } else {
            extLogFile.print(row);
            extLogFile.flush();
        }
    } else if (useOnboardFlash) {
        if (intLogFile.size() > 150000) {
            Serial.println("Flash: onboard flash full -- logging stopped");
            intLogFile.close();
            flashFull = true;
            return;
        }
        intLogFile.print(row);
        intLogFile.flush();
    }
}

// ---------------------------------------------------------------------------
// Serial dump command handler
// Send 'd' over Serial to dump the full CSV to Serial output
// ---------------------------------------------------------------------------
void handleSerialCommands() {
    if (!Serial.available()) return;
    char cmd = Serial.read();
    if (cmd != 'd' && cmd != 'D') return;

    Serial.println("=== DUMP START ===");

    if (extFlashOk && !useOnboardFlash) {
        extLogFile.flush();
        File32 readFile = fatfs.open(FLASH_CSV_FILENAME, FILE_READ);
        if (!readFile) {
            Serial.println("ERROR: could not open external log file for reading");
            Serial.println("=== DUMP END ===");
            return;
        }
        uint8_t buf[256];
        while (readFile.available()) {
            int n = readFile.read(buf, sizeof(buf));
            if (n > 0) Serial.write(buf, n);
        }
        readFile.close();
    } else if (useOnboardFlash) {
        intLogFile.flush();
        Adafruit_LittleFS_Namespace::File readFile =
            InternalFS.open(FLASH_CSV_FILENAME, Adafruit_LittleFS_Namespace::FILE_O_READ);
        if (!readFile) {
            Serial.println("ERROR: could not open onboard log file for reading");
            Serial.println("=== DUMP END ===");
            return;
        }
        uint8_t buf[256];
        while (readFile.available()) {
            int n = readFile.read(buf, sizeof(buf));
            if (n > 0) Serial.write(buf, n);
        }
        readFile.close();
    } else {
        Serial.println("ERROR: no flash storage available");
    }

    Serial.println("\n=== DUMP END ===");
}

// ---------------------------------------------------------------------------
// Ethylene sensor (DGS-EC on Serial1)
// ---------------------------------------------------------------------------
bool readEthylene(float &ppb) {
    while (Serial1.available()) Serial1.read();
    Serial1.write('\r');
    unsigned long deadline = millis() + 300;
    char buf[80]; int pos = 0;
    while (millis() < deadline) {
        if (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                buf[pos] = '\0';
                if (pos > 0 && buf[pos-1] == '\r') buf[--pos] = '\0';
                // CSV: SN,PPB,Temp,RH,...
                char *token = strtok(buf, ",");  // SN
                token = strtok(NULL, ",");        // PPB
                if (token) { ppb = atof(token); return true; }
                return false;
            }
            if (pos < 79) buf[pos++] = c;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// EI inference
// Flattens the 30-row feature window into a signal and runs the classifier.
// Returns anomaly score 0.0-1.0, or -1.0 on error.
// ---------------------------------------------------------------------------
int ei_get_data(size_t offset, size_t length, float *out) {
    // Feature window is stored row-major: featureWindow[time][feat]
    // EI expects sensor-major (all timesteps of feat0, then feat1, ...):
    // feat0_t0, feat0_t1, ..., feat0_t29, feat1_t0, ...
    for (size_t i = 0; i < length; i++) {
        size_t feat = i / WINDOW_SIZE;
        size_t t    = i % WINDOW_SIZE;
        // Map circular buffer: oldest row first
        int row = (windowHead - windowFilled + t + WINDOW_SIZE) % WINDOW_SIZE;
        out[i] = featureWindow[row][feat];
    }
    return 0;
}

float runInference() {
    if (windowFilled < WINDOW_SIZE) return -1.0f;  // not enough data yet

    signal_t signal;
    signal.total_length = WINDOW_SIZE * FEATURE_COUNT;
    signal.get_data     = &ei_get_data;

    ei_impulse_result_t result;
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) {
        Serial.print("EI error: "); Serial.println(err);
        return -1.0f;
    }
    // Regression output is index 0
    return result.classification[0].value;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 2000) {}

    Wire.begin();
    Wire.setClock(400000);

    // VPD optimal baseline (13.5C, 92.5%RH)
    vpdOptimal = computeVPD(13.5f, 92.5f);

    // BLE
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName("RipenSense");
    bleuart.begin();
    Bluefruit.Advertising.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
    Serial.println("RipenSense -- BLE advertising as 'RipenSense'");

    // GPS
    if (gnss.begin(Wire)) {
        gnssOk = true;
        gnss.setI2COutput(COM_TYPE_UBX);
        gnss.setNavigationFrequency(1);
        gnss.setAutoPVT(false);
        Serial.println("GPS:             OK");
    } else {
        Serial.println("GPS:             FAIL");
    }

    // Ethylene
    Serial1.begin(9600);
    Serial.println("Ethylene sensor: ready on Serial1");

    // Battery
    gaugeOk = fuelGauge.begin(Wire);
    Serial.println(gaugeOk ? "Battery gauge:   OK" : "Battery gauge:   FAIL");

    // SHT31
    sht31Ok = sht31.begin(0x45);
    Serial.println(sht31Ok ? "Temp/Humidity:   OK" : "Temp/Humidity:   FAIL");

    // IMU
    imuOk = imu.begin();
    if (imuOk) {
        imu.setAccelerometerRange(MPU6050_RANGE_8_G);
        imu.setGyroRange(MPU6050_RANGE_500_DEG);
        imu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        Serial.println("Accel/Gyro:      OK");
    } else {
        Serial.println("Accel/Gyro:      FAIL");
    }

    // DS18B20
    ds18b20.begin();
    ds18b20.setResolution(9);
    Serial.print("Temp probe:      ");
    Serial.print(ds18b20.getDeviceCount());
    Serial.println(" device(s)");

    // External SPI flash
    extFlashOk = initExternalFlash();

    // Initialize feature window to zeros
    memset(featureWindow, 0, sizeof(featureWindow));

    lastLogTime    = millis();
    lastShockSample = millis();

    Serial.println("--- Setup complete ---");
    Serial.println("Send 'd' to dump log CSV over Serial.");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
    uint32_t now = millis();

    // --- High-frequency shock sampling (~50Hz) ---
    if (imuOk && now - lastShockSample >= SHOCK_SAMPLE_INTERVAL_MS) {
        lastShockSample = now;
        sensors_event_t accel, gyro, temp;
        imu.getEvent(&accel, &gyro, &temp);
        float gx = accel.acceleration.x / 9.81f;
        float gy = accel.acceleration.y / 9.81f;
        float gz = accel.acceleration.z / 9.81f;
        float mag = sqrt(gx*gx + gy*gy + gz*gz);
        if (mag > shockMaxG) shockMaxG = mag;
        if (mag > SHOCK_THRESHOLD_G) shockCount++;
    }

    // --- Serial command check ---
    handleSerialCommands();

    // --- 1-minute log write ---
    if (now - lastLogTime < LOG_INTERVAL_MS) return;
    lastLogTime = now;

    delay(50);  // I2C bus settle

    // --- Read GPS ---
    float lat = 0.0f, lon = 0.0f, alt = 0.0f;
    char  timestampBuf[32] = "0000-00-00 00:00:00 EDT";
    if (gnssOk && gnss.getPVT(1100)) {
        lat = gnss.getLatitude()  / 1e7f;
        lon = gnss.getLongitude() / 1e7f;
        alt = gnss.getAltitude()  / 1000.0f;
        if (gnss.getTimeValid() && gnss.getDateValid()) {
            int oy, omo, od, oh, omi, os;
            utcToEdt(gnss.getYear(), gnss.getMonth(),  gnss.getDay(),
                     gnss.getHour(), gnss.getMinute(), gnss.getSecond(),
                     oy, omo, od, oh, omi, os);
            snprintf(timestampBuf, sizeof(timestampBuf),
                "%04d-%02d-%02d %02d:%02d:%02d EDT",
                oy, omo, od, oh, omi, os);
        }
    }

    // --- Read SHT31 ---
    float tempC    = 13.5f;
    float humidity = 92.5f;
    if (sht31Ok) {
        float t = sht31.readTemperature();
        float h = sht31.readHumidity();
        if (!isnan(t) && !isnan(h)) { tempC = t; humidity = h; }
    }

    // --- Read DS18B20 ---
    ds18b20.requestTemperatures();
    float probeTemp = ds18b20.getTempCByIndex(0);
    if (probeTemp == DEVICE_DISCONNECTED_C) probeTemp = tempC;

    // --- Read ethylene ---
    float ethylenePpb = 0.0f;
    readEthylene(ethylenePpb);

    // --- Read battery ---
    float battPct = 0.0f, battV = 0.0f;
    if (gaugeOk) {
        battPct = fuelGauge.getSOC();
        battV   = fuelGauge.getVoltage();
    }

    // --- Derived features ---
    float vpdVal = computeVPD(tempC, humidity);
    int   stage  = getStage(riCumulative);
    float q10    = getQ10(stage);

    // Update RI
    float delta = computeRIDelta(tempC, humidity, ethylenePpb, q10);
    if (shockCount > 0) delta += shockCount * 3.0f;
    if (tempC < 11.0f)  delta += 0.02f * (11.0f - tempC);
    riCumulative = constrain(riCumulative + delta, 0.0f, 100.0f);

    float anomalyScore = riCumulative / 100.0f;

    // --- Update feature window ---
    featureWindow[windowHead][FEAT_TEMP]       = tempC;
    featureWindow[windowHead][FEAT_HUMIDITY]   = humidity;
    featureWindow[windowHead][FEAT_ETHYLENE]   = ethylenePpb;
    featureWindow[windowHead][FEAT_PROBE_TEMP] = probeTemp;
    featureWindow[windowHead][FEAT_MAX_G]      = shockMaxG;
    featureWindow[windowHead][FEAT_SHOCK_CNT]  = (float)shockCount;
    featureWindow[windowHead][FEAT_VPD]        = vpdVal;
    featureWindow[windowHead][FEAT_STAGE]      = (float)stage;
    featureWindow[windowHead][FEAT_RI]         = riCumulative;
    windowHead = (windowHead + 1) % WINDOW_SIZE;
    if (windowFilled < WINDOW_SIZE) windowFilled++;

    // --- Run inference ---
    float inferredScore = runInference();
    if (inferredScore >= 0.0f) anomalyScore = inferredScore;

    // --- Reset shock window ---
    float loggedMaxG    = shockMaxG;
    int   loggedShocks  = shockCount;
    shockMaxG   = 1.0f;
    shockCount  = 0;

    // --- Build CSV row ---
    char row[220];
    snprintf(row, sizeof(row),
        "%s,%.6f,%.6f,%.2f,%.2f,%.2f,%.2f,%.3f,%d,%.4f,%d,%.3f,%.4f,%.1f,%.3f,%s\n",
        timestampBuf, lat, lon,
        tempC, humidity, ethylenePpb, probeTemp,
        loggedMaxG, loggedShocks,
        vpdVal, stage, riCumulative, anomalyScore,
        battPct, battV, MODEL_VERSION);

    // --- Write to flash ---
    writeRowToFlash(row);

    // --- Serial debug ---
    Serial.print(row);
    if (flashFull) Serial.println("WARNING: all flash storage full, logging stopped");
}
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

using namespace Adafruit_LittleFS_Namespace;

// ---------------------------------------------------------------------------
// Pin / bus config
// ---------------------------------------------------------------------------
#define ONE_WIRE_BUS        5
#define UTC_OFFSET_HOURS   -4   // EDT

// ---------------------------------------------------------------------------
// Flash config
// ---------------------------------------------------------------------------
#define FLASH_CSV_FILENAME  "/ripensense_log.csv"
#define CSV_HEADER          "timestamp,lat,lon,temp_c,humidity_rh,ethylene_ppb," \
                            "probe_temp_c,max_g_last_60s,shock_count_last_60s," \
                            "vpd_kpa,stage,ri_cumulative,anomaly_score," \
                            "battery_pct,battery_v,model_version\n"
#define MODEL_VERSION       "A1"

// External SPI flash
Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash             spiFlash(&flashTransport);
FatVolume                     fatfs;

// Flash state
bool     extFlashOk      = false;
bool     useOnboardFlash = false;
bool     flashFull       = false;
File32   extLogFile;                                 // FatFS handle
Adafruit_LittleFS_Namespace::File* intLogFile = nullptr; // Pointer for LittleFS handle

// ---------------------------------------------------------------------------
// Physics / ML Constants
// ---------------------------------------------------------------------------
#define SHOCK_SAMPLE_INTERVAL_MS  20
#define SHOCK_WINDOW_MS           60000
#define SHOCK_THRESHOLD_G         10.0f
#define LOG_INTERVAL_MS           60000
#define FEATURE_COUNT             9
#define WINDOW_SIZE               30

float    shockMaxG       = 1.0f;
int      shockCount      = 0;
uint32_t lastShockSample = 0;
uint32_t lastLogTime     = 0;

float featureWindow[WINDOW_SIZE][FEATURE_COUNT];
int   windowHead    = 0;
int   windowFilled  = 0;

#define FEAT_TEMP    0
#define FEAT_HUMIDITY 1
#define FEAT_ETHYLENE 2
#define FEAT_PROBE_TEMP 3
#define FEAT_MAX_G    4
#define FEAT_SHOCK_CNT 5
#define FEAT_VPD      6
#define FEAT_STAGE    7
#define FEAT_RI       8

#define W_E          0.04f
#define W_T          0.08f
#define W_H          0.01f
#define TEMP_REF     13.0f
#define ETH_THRESH   100.0f

float vpSat(float t) { return 0.61078f * exp(17.27f * t / (t + 237.3f)); }
float computeVPD(float t, float rh) { return vpSat(t) * (1.0f - rh / 100.0f); }
float vpdOptimal = -1.0f;

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
    if (stage == 1) return 2.25f;
    if (stage == 2) return 3.75f;
    if (stage == 3) return 3.00f;
    if (stage == 4) return 2.50f;
    if (stage == 5) return 2.00f;
    if (stage == 6) return 1.80f;
    return 1.50f;
}

float riCumulative = 2.0f;

float computeRIDelta(float tempC, float rh, float ethylenePpb, float q10) {
    float eTerm = W_E * max(0.0f, ethylenePpb / ETH_THRESH);
    float tExcess = max(0.0f, (float)pow(q10, (tempC - TEMP_REF) / 10.0f) - 1.0f);
    float tTerm = W_T * tExcess;
    float vpdExcess = max(0.0f, computeVPD(tempC, rh) - vpdOptimal);
    float hTerm = W_H * vpdExcess;
    return eTerm + tTerm + hTerm;
}

// ---------------------------------------------------------------------------
// Sensors & BLE
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
        uint8_t workbuf[512];
        FatFormatter formatter;
        if (!formatter.format(&spiFlash, workbuf, nullptr)) {
            Serial.println("Flash: format failed");
            return false;
        }
        if (!fatfs.begin(&spiFlash)) return false;
    }
    bool isNew = !fatfs.exists(FLASH_CSV_FILENAME);
    extLogFile = fatfs.open(FLASH_CSV_FILENAME, FILE_WRITE);
    if (!extLogFile) return false;
    if (isNew) {
        extLogFile.print(CSV_HEADER);
        extLogFile.flush();
    }
    Serial.println("Flash: external SPI flash OK");
    return true;
}

void writeRowToFlash(const char* row) {
    if (flashFull) return;

    if (extFlashOk && !useOnboardFlash) {
        uint32_t used  = extLogFile.size();
        uint32_t total = spiFlash.size();
        
        // 95% full check for hand-off
        if (used + strlen(row) + 10 > total * 0.95) {
            Serial.println("Flash: external full, switching to internal backup...");
            extLogFile.close();
            useOnboardFlash = true;
            InternalFS.begin();
            
            // Initialize the File object pointer
            intLogFile = new Adafruit_LittleFS_Namespace::File(InternalFS.open(FLASH_CSV_FILENAME, FILE_O_WRITE));
            
            if (!intLogFile || !(*intLogFile)) {
                Serial.println("Flash: onboard open failed");
                flashFull = true;
                return;
            }
            intLogFile->print(CSV_HEADER);
            intLogFile->flush();
        } else {
            extLogFile.print(row);
            extLogFile.flush();
            Serial.println("Flash: wrote row to external flash");
        }
    } else if (useOnboardFlash && intLogFile) {
        // Internal emergency cap: ~150KB
        if (intLogFile->size() > 150000) {
            Serial.println("Flash: onboard full");
            intLogFile->close();
            flashFull = true;
            return;
        }
        intLogFile->print(row);
        intLogFile->flush();
    }
}

void handleSerialCommands() {
    if (!Serial.available()) return;
    char cmd = Serial.read();

    // --- COMMAND: Zero Ethylene Sensor ---
    if (cmd == 'Z') {
        Serial.println("\n!!! ETHYLENE ZERO COMMAND RECEIVED !!!");
        Serial.println("Ensure sensor is in clean air for at least 30 mins.");
        Serial.println("Sending 'Z' and unlock code to sensor...");
        
        Serial1.write('Z');
        delay(100);             // Short delay for sensor buffer
        Serial1.print("12345"); // Send factory default unlock code
        Serial1.write('\r');    // Carriage return to finalize
        
        Serial.println("Commands sent. Check next log row for updated PPB.");
        return; 
    }

    // --- COMMAND: Data Dump ---
    if (cmd == 'd' || cmd == 'D') {
        Serial.println("=== DUMP START ===");
        if (extFlashOk && !useOnboardFlash) {
            extLogFile.flush();
            File32 readFile = fatfs.open(FLASH_CSV_FILENAME, FILE_READ);
            if (readFile) {
                uint8_t buf[64];
                while (readFile.available()) {
                    int n = readFile.read(buf, sizeof(buf));
                    Serial.write(buf, n);
                }
                readFile.close();
            }
        } else if (useOnboardFlash) {
            if (intLogFile) intLogFile->flush();
            Adafruit_LittleFS_Namespace::File readFile = InternalFS.open(FLASH_CSV_FILENAME, FILE_O_READ);
            if (readFile) {
                uint8_t buf[64];
                while (readFile.available()) {
                    int n = readFile.read(buf, sizeof(buf));
                    Serial.write(buf, n);
                }
                readFile.close();
            }
        }
        Serial.println("\n=== DUMP END ===");
        return;
    }

    // --- PASSTHROUGH: Catch-all for manual sensor interaction ---
    // If you type anything else (like 'v' for version), it goes to the sensor
    Serial1.write(cmd);
}

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
                char *token = strtok(buf, ",");
                token = strtok(NULL, ",");
                if (token) { ppb = atof(token); return true; }
                return false;
            }
            if (pos < 79) buf[pos++] = c;
        }
    }
    return false;
}

int ei_get_data(size_t offset, size_t length, float *out) {
    for (size_t i = 0; i < length; i++) {
        size_t feat = i / WINDOW_SIZE;
        size_t t    = i % WINDOW_SIZE;
        int row = (windowHead - windowFilled + t + WINDOW_SIZE) % WINDOW_SIZE;
        out[i] = featureWindow[row][feat];
    }
    return 0;
}

float runInference() {
    if (windowFilled < WINDOW_SIZE) return -1.0f;
    signal_t signal;
    signal.total_length = WINDOW_SIZE * FEATURE_COUNT;
    signal.get_data     = &ei_get_data;
    ei_impulse_result_t result;
    if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) return -1.0f;
    return result.classification[0].value;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Wire.begin();
    Wire.setClock(400000);
    vpdOptimal = computeVPD(13.5f, 92.5f);

    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName("RipenSense");
    bleuart.begin();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.Advertising.start(0);

    if (gnss.begin(Wire)) {
        gnssOk = true;
        gnss.setI2COutput(COM_TYPE_UBX);
    }
    Serial1.begin(9600);
    gaugeOk = fuelGauge.begin(Wire);
    sht31Ok = sht31.begin(0x45);
    imuOk = imu.begin();
    if (imuOk) imu.setAccelerometerRange(MPU6050_RANGE_8_G);

    ds18b20.begin();
    extFlashOk = initExternalFlash();
    memset(featureWindow, 0, sizeof(featureWindow));
    
    lastLogTime = millis();
    lastShockSample = millis();
    Serial.println("--- Setup complete ---");
}

void loop() {
    uint32_t now = millis();

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

    handleSerialCommands();

    if (now - lastLogTime < LOG_INTERVAL_MS) return;
    lastLogTime = now;

    float lat = 0.0f, lon = 0.0f;
    char  timestampBuf[32] = "0000-00-00 00:00:00 EDT";
    if (gnssOk && gnss.getPVT(1100)) {
        lat = gnss.getLatitude()  / 1e7f;
        lon = gnss.getLongitude() / 1e7f;
        int oy, omo, od, oh, omi, os;
        utcToEdt(gnss.getYear(), gnss.getMonth(),  gnss.getDay(),
                 gnss.getHour(), gnss.getMinute(), gnss.getSecond(),
                 oy, omo, od, oh, omi, os);
        snprintf(timestampBuf, sizeof(timestampBuf), "%04d-%02d-%02d %02d:%02d:%02d EDT", oy, omo, od, oh, omi, os);
    }

    float tempC = 13.5f, humidity = 92.5f;
    if (sht31Ok) {
        float t = sht31.readTemperature();
        float h = sht31.readHumidity();
        if (!isnan(t)) { tempC = t; humidity = h; }
    }

    ds18b20.requestTemperatures();
    float probeTemp = ds18b20.getTempCByIndex(0);
    if (probeTemp == DEVICE_DISCONNECTED_C) probeTemp = tempC;

    float ethylenePpb = 0.0f;
    readEthylene(ethylenePpb);

    float battPct = 0.0f, battV = 0.0f;
    if (gaugeOk) { battPct = fuelGauge.getSOC(); battV = fuelGauge.getVoltage(); }

    float vpdVal = computeVPD(tempC, humidity);
    int   stage  = getStage(riCumulative);
    float delta  = computeRIDelta(tempC, humidity, ethylenePpb, getQ10(stage));
    if (shockCount > 0) delta += shockCount * 3.0f;
    riCumulative = constrain(riCumulative + delta, 0.0f, 100.0f);

    featureWindow[windowHead][FEAT_TEMP] = tempC;
    featureWindow[windowHead][FEAT_HUMIDITY] = humidity;
    featureWindow[windowHead][FEAT_ETHYLENE] = ethylenePpb;
    featureWindow[windowHead][FEAT_PROBE_TEMP] = probeTemp;
    featureWindow[windowHead][FEAT_MAX_G] = shockMaxG;
    featureWindow[windowHead][FEAT_SHOCK_CNT] = (float)shockCount;
    featureWindow[windowHead][FEAT_VPD] = vpdVal;
    featureWindow[windowHead][FEAT_STAGE] = (float)stage;
    featureWindow[windowHead][FEAT_RI] = riCumulative;
    windowHead = (windowHead + 1) % WINDOW_SIZE;
    if (windowFilled < WINDOW_SIZE) windowFilled++;

    float anomalyScore = runInference();
    if (anomalyScore < 0) anomalyScore = riCumulative / 100.0f;

    char row[220];
    snprintf(row, sizeof(row), "%s,%.6f,%.6f,%.2f,%.2f,%.2f,%.2f,%.3f,%d,%.4f,%d,%.3f,%.4f,%.1f,%.3f,%s\n",
             timestampBuf, lat, lon, tempC, humidity, ethylenePpb, probeTemp, shockMaxG, shockCount,
             vpdVal, stage, riCumulative, anomalyScore, battPct, battV, MODEL_VERSION);

    writeRowToFlash(row);
    Serial.print(row);

    shockMaxG = 1.0f;
    shockCount = 0;
}
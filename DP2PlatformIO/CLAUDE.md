# RipenSense — CLAUDE.md

Full session context. Drop this in the repo root and reference it at the start of any new session.

---

## What RipenSense Is

A supply-chain accountability device for banana shipments. It logs environmental sensor data once per minute, runs a TinyML anomaly model on-device, and writes a timestamped score alongside GPS coordinates to SPI flash. The goal is to determine **which courier, at which location and time, damaged the bananas** — producing an immutable, tamper-evident accountability record.

**V2 vision (not yet built):** NFC tag on the device stores the BLE MAC address. Warehouse worker taps it with their phone → phone gets MAC → connects via BLE → pulls the log CSV → uploads to backend. No app install needed via Web Bluetooth PWA.

**Current egress:** USB Serial. Send `d` over Serial to dump the full CSV. Run `python dump.py` to capture it automatically.

---

## Hardware

**Board:** Adafruit nRF52840 Feather Express  
**Sensors (all confirmed working):**

| Sensor | Interface | Address/Pin | Measures |
|---|---|---|---|
| u-blox GNSS | I2C | 0x42 | GPS lat/lon, date/time UTC |
| MPU-6050 | I2C | 0x68 | Accel (m/s²), gyro (rad/s) |
| SHT31 | I2C | 0x45 | Ambient temp (C), humidity (%RH) |
| MAX17048 | I2C | 0x36 | Battery SOC (%), voltage (V) |
| DS18B20 | 1-Wire | Pin 5 | Probe temp (C) — inside packaging |
| DGS-EC (ethylene) | UART Serial1 | 9600 baud | Ethylene (ppb) |
| Adafruit SPI Flash breakout | QSPI | — | Log storage, 2MB |

**GPS note:** UART completely failed. Switched to I2C with `gnss.begin(Wire)`, `setI2COutput(COM_TYPE_UBX)`, `setAutoPVT(false)`. 50ms delay at top of `loop()` before GPS call resolves I2C bus contention. Polled manually with `getPVT(1100)`.

**Time:** GPS UTC converted to EDT (UTC-4) via `utcToEdt()`. Change `UTC_OFFSET_HOURS` to -5 for EST in November.

---

## Firmware: `src/main.cpp`

### Key behaviors

**Shock windowing (60s rolling):**
IMU sampled at ~50Hz between log writes via non-blocking timer check at top of `loop()`. `shockMaxG` and `shockCount` accumulate over 60 seconds and reset after each log write. The model receives these aggregates, not raw accel readings.

```cpp
#define SHOCK_SAMPLE_INTERVAL_MS  20     // ~50Hz
#define SHOCK_THRESHOLD_G         10.0f  // above this = bruising event
#define LOG_INTERVAL_MS           60000  // 1 minute between rows
```

**RI computation (mirrors datagen physics exactly):**
```cpp
// Excess-above-optimal formulation
e_term = W_E * max(0, ethylene_ppb / 100.0)
t_term = W_T * max(0, Q10^((T - 13.0) / 10) - 1)
h_term = W_H * max(0, VPD - VPD_optimal)
ri_delta = e_term + t_term + h_term  // per minute
// W_E=0.04, W_T=0.08, W_H=0.01
// VPD_optimal computed at 13.5C, 92.5%RH
```

**ML inference window:**
30-row circular buffer of the 9 feature columns. `ei_get_data()` translates from row-major storage to sensor-major order that Edge Impulse expects. For the first 30 minutes before the window fills, anomaly score falls back to `ri / 100`.

**Flash fallback chain:**
External SPI flash (2MB, QSPI) → onboard flash (150KB budget via LittleFS/InternalFS) → stop and flag. Fallback triggers at 95% external flash capacity.

**Serial dump:**
Send `d` or `D` over Serial to stream full CSV. Handles both external FatFS and onboard LittleFS transparently.

**BLE:**
Advertising only as "RipenSense". `bleLog()` mirrors Serial output to BLE UART when a client is connected. Not used for data egress yet.

### Critical type split
External flash uses `File32` (SdFat/FatFS). Onboard flash uses `Adafruit_LittleFS_Namespace::File` (LittleFS). These are incompatible — must be separate variables:
```cpp
File32   extLogFile;
Adafruit_LittleFS_Namespace::File intLogFile;
```

### Required includes
```cpp
#include <Adafruit_SPIFlash.h>
#include <InternalFileSystem.h>   // for InternalFS / LittleFS fallback
#include <RipenSense_inferencing.h>
```

### Flash format fix
`FatVolume` has no `.format()` method. Use `FatFormatter` instead:
```cpp
uint8_t workbuf[512];
FatFormatter formatter;
formatter.format(&spiFlash, workbuf, nullptr);
```

---

## CSV Schema (locked)

Every row written to flash and dumped over Serial:

```
timestamp,lat,lon,temp_c,humidity_rh,ethylene_ppb,probe_temp_c,
max_g_last_60s,shock_count_last_60s,vpd_kpa,stage,ri_cumulative,
anomaly_score,battery_pct,battery_v,model_version
```

Example row:
```
2025-05-01 14:32:00 EDT,40.728200,-74.172600,13.52,91.80,3.21,13.78,1.243,0,0.0821,1,2.341,0.023,87.3,3.921,A1
```

**Notes:**
- `model_version` is a string, currently hardcoded `"A1"`. Increment when retraining.
- `timestamp` is human-readable EDT, not Unix epoch
- `anomaly_score` is EI model output (0-1 float); falls back to `ri/100` for first 30 minutes
- GPS columns are 0.0 if no fix yet
- `stage` is 1-7 per the ripening stage table

---

## Banana Ripening Physics

From meeting notes. Baked into both the data generator and on-device RI computation.

**Ripening Index:** RI=0 perfectly green, RI=100 overripe.

**Thresholds:**
- Ethylene: safe < 100 ppb. Point of no return at 100+ ppb sustained 12-24h
- Temp: optimal 13-14C. Below 12C = chilling injury. Above 18C = cooked fruit
- Humidity: optimal 90-95%RH. Below 85% = moisture stress
- Shock: >10-15G causes bruising → stress ethylene

**VPD (Tetens equation):**
```
VPsat = 0.61078 · e^(17.27·T / (T+237.3))
VPD   = VPsat · (1 - RH/100)
```

**Q10 by ripening stage:**

| Stage | Color | Q10 | RI range |
|---|---|---|---|
| 1 | Hard green | 2.25 | 0-14 |
| 2 | Green, trace yellow | 3.75 | 14-28 |
| 3 | More green than yellow | 3.00 | 28-42 |
| 4 | More yellow than green | 2.50 | 42-56 |
| 5 | Yellow, green tips | 2.00 | 56-70 |
| 6 | Full yellow (RIPE) | 1.80 | 70-85 |
| 7 | Yellow, brown spots | 1.50 | 85-100 |

**Sensor physics modeled in generator:**
- SHT31 thermal lag: exponential filter, tau = 7 min
- Ethylene diffusion lag after shock: 20 min lag, then exponential rise tau = 30 min, decays ~2h
- Diurnal temp variation: ~0.5C swing over 24h, peaks at noon

---

## Synthetic Data Generator: `ripensense_datagen_v2.py`

**Outputs:**
- `ripensense_train_raw.csv` — 72,000 rows, 50 runs, minute-by-minute
- `ripensense_ei_flat.csv` — 14,150 windows, 270 features + label
- `ripensense_1day_demo.csv` — 1,440 rows, demo narrative

**50 runs across 8 scenario types:**

| Scenario | Runs | Notes |
|---|---|---|
| normal | 8 | Minor road bumps only |
| near_miss | 7 | Approaches thresholds, recovers |
| temp_drift | 7 | Gradual refrigeration failure |
| temp_spike | 6 | Door left open briefly |
| rough_handling | 7 | Multiple severe shocks |
| humidity_drop | 5 | Packaging failure |
| ethylene_contamination | 5 | Neighboring pallet leaking |
| compound_failure | 5 | Everything goes wrong |

**Window:** 30 minutes, stride 5 → 14,150 EI samples  
**Features (9):** `temp_c`, `humidity_rh`, `ethylene_ppb`, `probe_temp_c`, `max_g_last_60s`, `shock_count_last_60s`, `vpd_kpa`, `stage`, `ri_cumulative`  
**Label:** `anomaly_score` = RI / 100, float 0-1  
**Calibration:** Normal 24h → RI ~9. Sustained failure 24h → RI ~100.

**Demo narrative:** Clean start (0-2h) → severe shock 3h → temp spike 5h → transfer shock 8h → ethylene contamination 9h → humidity drop 11h → final RI 72.8, stage 6, anomaly peak 0.728. GPS: Newark DC → Holland Tunnel → Manhattan stops.

---

## ML Pipeline

**Tool:** Edge Impulse  
**Task:** Regression (predict continuous anomaly 0-1)  
**Model:** Exported as Arduino library, folder `lib/ml-model/`  
**Header:** `RipenSense_inferencing.h`  
**Model version tag:** `A1`

**EI upload journey (document for future pain avoidance):**
- Time-series upload with raw CSV caused metadata conflict errors because `run_id` changed value across runs within the file
- Pre-windowed flat CSV confused EI into treating it as time-series due to `_t0..t29` column naming
- Final working approach: upload `ripensense_ei_flat.csv`, answer **No** to time-series, "each column is a reading, each row is a sample", label = `anomaly_score`, no processing block, Regression learning block only

**Inference call pattern:**
```cpp
signal_t signal;
signal.total_length = WINDOW_SIZE * FEATURE_COUNT;  // 30 * 9 = 270
signal.get_data     = &ei_get_data;
ei_impulse_result_t result;
run_classifier(&signal, &result, false);
float score = result.classification[0].value;
```

**Feature order in EI signal (sensor-major):**
`temp_c t0..t29, humidity_rh t0..t29, ...`
`ei_get_data()` translates from row-major circular buffer to this order.

---

## Serial Dump Script: `dump.py`

```bash
pip install pyserial
python dump.py                        # auto-detects port
python dump.py --port /dev/ttyACM0    # specify port
python dump.py --out shipment.csv     # custom output filename
```

Sends `d`, waits for `=== DUMP START ===` / `=== DUMP END ===` markers, saves CSV. Progress printed every 100 rows. 60 second timeout.

---

## Frontend

Teammate is building. Expectations:
- Drag-and-drop CSV ingestion
- Map component for GPS track (lat/lon in every row)
- Anomaly score timeline
- Accountability windows: which time periods and locations had high scores

Hand them `ripensense_1day_demo.csv` for development.

---

## `platformio.ini`

```ini
[env:adafruit_feather_nrf52840]
platform = nordicnrf52
board = adafruit_feather_nrf52840
framework = arduino
test_framework = unity
lib_deps =
    adafruit/Adafruit MPU6050
    adafruit/Adafruit SHT31 Library
    adafruit/Adafruit BusIO
    adafruit/Adafruit SPIFlash
    paulstoffregen/OneWire
    milesburton/DallasTemperature
    sparkfun/SparkFun MAX1704x Fuel Gauge Arduino Library
    sparkfun/SparkFun u-blox GNSS Arduino Library
```

`unity.h` squiggles in VS Code are IntelliSense-only. Fix with `Ctrl+Shift+P → PlatformIO: Rebuild IntelliSense Index`.

EI library in `lib/ml-model/` is auto-discovered. If include errors persist add:
```ini
lib_extra_dirs = lib/ml-model
```

---

## Known Issues / Next Steps

1. Confirm `FatFormatter` formats correctly on first boot with blank flash chip
2. Confirm `InternalFS` / LittleFS fallback compiles on your exact Adafruit nRF52 core version
3. End-to-end test: boot, log a few minutes, send `d`, confirm clean CSV output
4. Run `dump.py` and confirm CSV captures correctly
5. Hand `ripensense_1day_demo.csv` to frontend teammate
6. Plan NFC + BLE egress for v2
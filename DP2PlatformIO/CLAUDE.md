# RipenSense — CLAUDE.md

Full session context for the RipenSense project. Use this to resume work without re-explaining the system.

---

## What RipenSense Is

A supply-chain accountability device for banana shipments. It logs environmental sensor data in real time, runs a TinyML model on-device to compute an anomaly score (0-1) every minute, and logs that score alongside GPS coordinates and a timestamp. The goal is to determine **which courier, at which location and time, damaged the bananas** — producing an immutable, timestamped accountability record.

---

## Hardware

**Board:** Adafruit nRF52840 Feather  
**Sensors (all working):**

| Sensor | Interface | Address/Pin | Measures |
|---|---|---|---|
| u-blox GNSS | I2C | 0x42 | GPS lat/lon, date/time UTC |
| MPU-6050 | I2C | 0x68 | Accel (m/s²), gyro (rad/s) |
| SHT31 | I2C | 0x45 | Ambient temp (C), humidity (%RH) |
| MAX17048 | I2C | 0x36 | Battery SOC (%), voltage (V) |
| DS18B20 | 1-Wire | Pin 5 | Probe temp (C) — inside packaging |
| DGS-EC (ethylene) | UART Serial1 | 9600 baud | Ethylene (ppb) |
| SPI Flash (Adafruit) | SPI | TBD | Log storage — not yet implemented |

**GPS note:** UART did not work reliably. Switched to I2C with `gnss.begin(Wire)`, `setI2COutput(COM_TYPE_UBX)`, `setAutoPVT(false)`. Polled manually with `getPVT(1100)`. A 50ms delay at the top of `loop()` before the GPS call resolved I2C bus contention with other sensors.

**Time:** GPS UTC converted to EDT (UTC-4) via `utcToEdt()`. Hardcoded offset — change to -5 for EST in November.

**Not yet implemented in firmware:**
- SPI flash logging
- Shock windowing (max_g and shock_count over 60s rolling window — needed for model inference)
- TFLite Micro inference call

---

## Firmware File

`ripensense_main.cpp` (current working version). Key behaviors:
- BLE advertising as "RipenSense" via `bleuart`
- `bleLog()` sends output to both Serial and BLE UART
- All sensors initialized with `bool *Ok` flags; failures are non-fatal
- Ethylene sensor polled via `readEthylene()` helper — sends `\r`, parses CSV response, extracts PPB field (index 1)
- Loop runs every ~2s + GPS poll time

---

## Banana Ripening Physics

From meeting notes. All of this is baked into the synthetic data generator.

**Ripening Index (RI):** 0 = perfectly green, 100 = overripe.

```
RI = ∫[W_E·(C₂H₄)_rate + W_T·Q10^((T-Tref)/10) + W_H·VPD] dt + ΣShock_impact
```

**Calibrated weights (excess-above-optimal formulation):**
- `W_E = 0.04` — ethylene (highest weight)
- `W_T = 0.08` — temperature
- `W_H = 0.01` — VPD (lowest weight)

Temperature and VPD terms use excess above optimal so that holding at goldilocks conditions contributes near-zero RI. Only deviations accumulate damage.

**Thresholds:**
- Ethylene: safe < 100 ppb (0.1 ppm). Point of no return at 100+ ppb sustained 12-24h
- Temp: optimal 13-14C. Below 12C = chilling injury. Above 18C = cooked fruit
- Humidity: optimal 90-95%RH. Below 85% = moisture loss
- Shock: > 10-15G causes bruising → stress ethylene

**VPD (Tetens equation):**
```
VPsat = 0.61078 · e^(17.27·T / (T+237.3))
VPD   = VPsat · (1 - RH/100)
```

**Q10 by ripening stage:**

| Stage | Color | Q10 | RI range |
|---|---|---|---|
| 1 | Hard green | 2.0-2.5 | 0-14 |
| 2 | Green, trace yellow | 3.5-4.0 | 14-28 |
| 3 | More green than yellow | 3.0 | 28-42 |
| 4 | More yellow than green | 2.5 | 42-56 |
| 5 | Yellow, green tips | 2.0 | 56-70 |
| 6 | Full yellow (RIPE) | 1.8 | 70-85 |
| 7 | Yellow, brown spots | 1.5 | 85-100 |

---

## Sensor Physics Modeled in Data Generator

- **SHT31 thermal lag:** Exponential filter, tau = 7 minutes. Sensor reading follows true temp with delay — a sudden spike takes ~7 min to show up fully.
- **Ethylene diffusion lag after shock:** 20-minute lag before bruise ethylene appears at sensor, then exponential rise with tau = 30 min, decays over ~2 hours unless climacteric threshold crossed.
- **Shock time scale:** Sub-second impulses. Logged as `max_g_last_60s` and `shock_count_last_60s` — rolling 60s window aggregates. Not yet implemented in firmware.
- **Diurnal variation:** ~0.5C temp swing over 24h, peaks at noon.

---

## Synthetic Data Generator

**File:** `ripensense_datagen_v2.py`

**Outputs:**
- `ripensense_train_raw.csv` — 72,000 rows, 50 runs, full minute-by-minute
- `ripensense_ei_flat.csv` — 14,150 windows, 270 features + label, for Edge Impulse
- `ripensense_1day_demo.csv` — 1,440 rows, demo narrative run

**50 runs across 8 scenario types:**

| Scenario | Runs | Anomaly mean | Anomaly max |
|---|---|---|---|
| normal | 8 | 0.091 | 0.221 |
| near_miss | 7 | 0.120 | 0.246 |
| temp_drift | 7 | 0.239 | 0.431 |
| temp_spike | 6 | 0.198 | 0.348 |
| rough_handling | 7 | 0.298 | 0.747 |
| humidity_drop | 5 | 0.109 | 0.154 |
| ethylene_contamination | 5 | 0.190 | 0.296 |
| compound_failure | 5 | 0.568 | 0.878 |

**Window:** 30 minutes, stride 5 minutes → 14,150 EI samples  
**Features (9):** `temp_c`, `humidity_rh`, `ethylene_ppb`, `probe_temp_c`, `max_g_last_60s`, `shock_count_last_60s`, `vpd_kpa`, `stage`, `ri_cumulative`  
**Label:** `anomaly_score` = RI / 100, float 0-1

**Demo run narrative:** Clean start (0-2h) → severe shock at 3h → temp spike 5h → transfer shock 8h → ethylene contamination 9h → humidity drop 11h → final RI 72.8, stage 6, anomaly peak 0.728

**GPS:** Simulated Newark DC → Holland Tunnel → Manhattan stops. GPS is metadata only, not a model feature.

---

## ML Pipeline

**Tool:** Edge Impulse  
**Task:** Regression (predict continuous anomaly score 0-1)  
**Model target:** nRF52840 via TFLite Micro / Edge Impulse Arduino library export

**EI upload format for `ripensense_ei_flat.csv`:**
- Time-series → **No**
- Format → "Each column contains a reading, each row contains a full sample"
- Label → `anomaly_score`
- Impulse: no processing block, just Regression learning block
- Training: 100 cycles, lr 0.0005, 20% validation
- Target MAE < 0.05

**Why flat CSV not raw time-series:** EI's time-series pipeline re-windows data itself, which caused conflicts with our multi-run CSV structure. Pre-windowing ourselves gives full control and avoids EI's metadata conflict errors.

---

## On-Device Architecture (planned)

Every minute, the device:
1. Reads all sensors
2. Updates rolling 60s shock window (max_g, shock_count)
3. Computes VPD and stage from current RI
4. Runs TFLite Micro inference on 30-minute feature window
5. Writes to SPI flash: `timestamp, lat, lon, temp, humidity, ethylene, accel, probe_temp, battery, ri_cumulative, anomaly_score, model_version`
6. Syncs to backend at handoff points via BLE

**Why on-device inference:** Immutable timestamped record. No connectivity dependency. Cannot be disputed after handoff. Model version logged alongside every reading for post-hoc reanalysis if model is retrained.

---

## Key Decisions Log

- **GPS on I2C not UART** — UART was unreliable; I2C resolved with `setAutoPVT(false)` and 50ms bus settle delay
- **Ethylene on UART Serial1** — DGS-EC polled at 9600 baud with `\r` command
- **Anomaly score as 0-1 float** — continuous, derived directly from RI so label is physically grounded
- **On-device inference** — immutability and connectivity independence outweigh inability to push model updates mid-shipment; version field handles reanalysis
- **1-minute logging interval** — matches sensor update rate; 7-day shipment = ~10k rows
- **SPI flash for storage** — ~30k rows expected; not yet wired up
- **Shock windowing not yet in firmware** — needs 60s rolling max_g and count between log writes

---

## Next Steps

1. Finish Edge Impulse training, check MAE
2. Export Arduino library from EI deployment tab
3. Implement SPI flash logging in firmware
4. Implement 60s shock window aggregation in firmware
5. Wire in TFLite Micro inference call
6. End-to-end test: device logs sensor data + anomaly score to flash, reads back cleanly
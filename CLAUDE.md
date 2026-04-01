# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RipenSense is an edge-AI anomaly detection system for banana ripening monitoring. An **Adafruit Feather nRF52840** (Nordic nRF52840) reads environmental and motion sensors, runs an on-device anomaly model, and logs/alerts when conditions exceed a threshold.

## Commands

### Firmware (PlatformIO) — work from `DP2PlatformIO/`
```bash
pio run                        # Build
pio run --target upload        # Flash to device
pio device monitor             # Open serial monitor
pio run --target clean         # Clean build artifacts
pio test                       # Run unit tests
```

### Python scripts — work from `scripts/`
```bash
python3 -m venv .venv
source .venv/bin/activate
pip3 install -r requirements.txt
python3 synthetic_data.py      # Generate training dataset + visualization
```

## Architecture

### Firmware pipeline (`DP2PlatformIO/src/main.cpp`)

The `loop()` runs a 4-phase pipeline every `INFERENCE_INTERVAL_MS` (10 s):

1. **Phase A – Acquire**: Read all sensors into a `SensorFeatures` struct (timestamp, ethylene, temp, humidity, probe temp, accel, gyro).
2. **Phase B – Infer**: Pass features to the edge-AI anomaly model → `anomaly_score` (0.0–1.0).
3. **Phase C – Decide & Log**: Serial-log readings; if `anomaly_score > 0.85` emit "ANOMALY DETECTED".
4. **Phase D – Power**: Placeholder for deep-sleep.

### Hardware abstraction (`DP2PlatformIO/lib/`)

| Library | Sensor | Bus | Address |
|---|---|---|---|
| `sht31_climate` | SHT31 – temperature + humidity | I2C | 0x45 |
| `mpu6050_imu` | MPU6050 – 6-axis motion | I2C | 0x68 |

Each library exposes `_Init()` and `_Read*()` functions; vendor libraries are pulled via `lib_deps` in `platformio.ini`.

### Stub/planned sensors (not yet wired in)
- **RV-1805** RTC (I2C timestamps)
- **DGS2** ethylene gas sensor (UART)
- **DS18B20** probe temperature (1-Wire)

### Synthetic data (`scripts/synthetic_data.py`)

Simulates 10 bananas over 7 days using a Q10 temperature-coefficient ripening model. Outputs `synthetic_banana_data.csv` (columns: `banana_id, hour, temp_C, humidity_percent, shock_G, ethylene_ppm, RI`) and `banana_simulation.png`. This CSV is the intended training set for the edge-AI anomaly model.

**Weighting**: ethylene 50 %, temperature/Q10 30 %, VPD/humidity 10 %, shock 10 %.

## Key Implementation Notes

- The edge-AI inference call in `main.cpp` is a **stub** — the actual model integration is the primary open work item.
- SD card logging, radio transmission, and deep-sleep are all commented-out placeholders.
- `.gitignore` excludes generated `.csv`, `.png`, and `.venv` artifacts — regenerate locally as needed.

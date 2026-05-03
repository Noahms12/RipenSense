# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Firmware Commands (PlatformIO)

```bash
pio run                        # compile
pio run --target upload        # compile and flash to Feather nRF52840
pio device monitor             # open serial monitor at 115200 baud
pio run --target upload && pio device monitor   # flash and monitor in one step
```

## ML Pipeline Commands

Run from `../scripts/` (repo root `scripts/` directory):

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt        # numpy, pandas, matplotlib
pip install tensorflow scikit-learn    # required by train_tinyml_model.py

python3 synthetic_data.py              # generates synthetic_banana_data.csv
python3 train_tinyml_model.py          # trains autoencoder → model.tflite + model_data.h
```

After training, copy `model_data.h` into the firmware before building.

## Architecture

### Hardware Target
**Adafruit Feather nRF52840** — runs Arduino framework via PlatformIO (`nordicnrf52` platform). BLE stack is Adafruit's `bluefruit` library.

### Sensor Bus Layout

| Sensor | Interface | Address/Pin | Library |
|---|---|---|---|
| MAX1704x fuel gauge | I2C | 0x36 | SparkFun MAX1704x |
| MAX-M10S GPS | I2C | 0x42 | SparkFun u-blox GNSS |
| SHT31 temp+humidity | I2C | 0x45 | Adafruit SHT31 / DFRobot SHT3x |
| MPU6050 gyro/accel | I2C | 0x68 | Adafruit MPU6050 |
| RV-1805 RTC | I2C | 0x69 | SparkFun RV-1805 |
| DGS2 ethylene | UART Serial1 | 9600 baud | custom driver |
| DS18B20 temp probe | 1-Wire | pin 5 | DallasTemperature |
| W25Q16 SPI flash | SPI | CS pin 10 | Adafruit SPIFlash |

No I2C address conflicts. Always call `Wire.begin()` and `SPI.begin()` before initializing any peripheral.

### Driver Structure
Drivers live in `src/` alongside `main.cpp`. There are two naming conventions in use:
- **Free functions**: `IMU_Init()` / `IMU_ReadMotion()` (mpu6050_imu), `Climate_Init()` / `Climate_ReadTemp()` / `Climate_ReadHumidity()` (sht31_climate)
- **Classes**: `DS18B20Sensor`, `DGS2Gas`, `RTC_RV1805`, `PowerSys`, `GPS_MAXM10S`, `StorageW25Q16`

### BLE Setup Pattern
The nRF52840 BLE stack requires this exact initialization order in `setup()`:
1. `Bluefruit.begin()` + `setTxPower()` + `setName()`
2. `bleuart.begin()` (starts the UART GATT service)
3. `Bluefruit.Advertising.clearData()` → add flags → `addTxPower()` → `addService(bleuart)`
4. `Bluefruit.ScanResponse.addName()`
5. `Bluefruit.Advertising.setInterval(32, 244)` + `setFastTimeout(30)` + `start(0)`

Check `bleuart.notifyEnabled()` before sending data to avoid buffering when no central is connected.

### DGS2 Ethylene Sensor Protocol
The SPEC DGS sensor requires sending `'\r'` over UART to trigger a measurement. Wait ~100 ms, then read the response string (format: `SN=XXXXXX, PPB=XXXX, T=XX.XX`). The driver's `requestReading()` sends the trigger byte; `readData()` reads available bytes.

### Ripeness Model (ML)
- **Output**: Ripeness Index (RI) 0–100 via autoencoder reconstruction error (anomaly score)
- **Features**: `temp_C`, `humidity_percent`, `shock_G`, `ethylene_ppm` (15-timestep windows)
- **Weights**: ethylene 50%, temperature Q10 30%, humidity/VPD 10%, shock 10%
- **Reference temp**: T_ref = 13°C (banana cold-chain target)
- Training produces `model_data.h` — a C array (`g_model_data[]`) to embed in firmware via TFLite Micro

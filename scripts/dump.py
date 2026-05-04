import serial
import time
import glob
from pathlib import Path

# ---------------- CONFIG ----------------
BAUD = 115200
OUTPUT_DIR = Path("outputs")
OUTPUT_DIR.mkdir(exist_ok=True)

ROW_SIZE = 256

# ---------------- PORT DISCOVERY ----------------
def find_port():
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        raise RuntimeError("No ttyACM devices found")
    return ports[-1]

# ---------------- RAW DUMP ----------------
def read_flash_dump(ser):
    data = bytearray()
    ser.timeout = 0.2

    print("Reading flash stream...")

    while True:
        chunk = ser.read(256)
        if not chunk:
            break

        data.extend(chunk)

        if len(chunk) < 256:
            break

    return data

# ---------------- PARSER ----------------
def parse_rows(raw):
    rows = []

    for i in range(0, len(raw), ROW_SIZE):
        block = raw[i:i+ROW_SIZE]

        if not block:
            continue

        text = block.split(b'\x00', 1)[0]

        try:
            decoded = text.decode("utf-8", errors="ignore").strip()
            if decoded:
                rows.append(decoded)
        except:
            continue

    return rows

# ---------------- SAVE CSV ----------------
def save_csv(rows):
    ts = time.strftime("%Y%m%d_%H%M%S")
    out_file = OUTPUT_DIR / f"log_{ts}.csv"

    with open(out_file, "w", encoding="utf-8") as f:
        for r in rows:
            f.write(r + "\n")

    print(f"Saved CSV: {out_file}")

# ---------------- SAVE RAW ----------------
def save_raw(raw):
    ts = time.strftime("%Y%m%d_%H%M%S")
    out_file = OUTPUT_DIR / f"raw_dump_{ts}.bin"

    with open(out_file, "wb") as f:
        f.write(raw)

    print(f"Saved RAW dump: {out_file}")

# ---------------- MAIN ----------------
def main():
    port = find_port()
    print(f"Using port: {port}")

    with serial.Serial(port, BAUD, timeout=2) as ser:
        time.sleep(2)

        ser.reset_input_buffer()

        ser.write(b'D')
        ser.flush()

        time.sleep(1.0)

        raw = read_flash_dump(ser)

    print(f"Raw bytes: {len(raw)}")

    rows = parse_rows(raw)

    print(f"Valid rows: {len(rows)}")

    if len(rows) == 0:
        print("No valid rows detected, saving raw dump")
        save_raw(raw)
    else:
        save_csv(rows)

if __name__ == "__main__":
    main()
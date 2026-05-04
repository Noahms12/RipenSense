"""
RipenSense Serial Dump
======================
Sends 'd' to the device over Serial, reads the CSV dump, and saves it to a file.

Usage:
    python dump.py                        # auto-detects port
    python dump.py --port /dev/ttyUSB0    # specify port
    python dump.py --port COM3            # Windows
    python dump.py --out my_shipment.csv  # custom output filename
"""

import argparse
import glob
import sys
import time
import serial

BAUD         = 115200
DUMP_START   = "=== DUMP START ==="
DUMP_END     = "=== DUMP END ==="
TIMEOUT_S    = 60   # max seconds to wait for full dump

def find_port() -> str:
    """Best-effort auto-detect of nRF52840 serial port."""
    candidates = (
        glob.glob("/dev/ttyACM*") +
        glob.glob("/dev/ttyUSB*") +
        glob.glob("/dev/cu.usbmodem*") +
        glob.glob("COM[0-9]*")
    )
    if not candidates:
        print("ERROR: No serial ports found. Use --port to specify manually.")
        sys.exit(1)
    if len(candidates) > 1:
        print(f"Multiple ports found: {candidates}")
        print(f"Using {candidates[0]} -- use --port to override.")
    return candidates[0]

def dump(port: str, outfile: str):
    print(f"Connecting to {port} at {BAUD} baud...")
    try:
        ser = serial.Serial(port, BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"ERROR: Could not open port: {e}")
        sys.exit(1)

    time.sleep(1.5)  # let the device settle after serial open

    # Flush any pending output
    ser.reset_input_buffer()

    print("Sending dump command 'd'...")
    ser.write(b'd')

    lines      = []
    in_dump    = False
    start_time = time.time()

    print("Waiting for dump...")
    while time.time() - start_time < TIMEOUT_S:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue

        if DUMP_START in line:
            in_dump = True
            print("Dump started.")
            continue

        if DUMP_END in line:
            print("Dump complete.")
            break

        if in_dump:
            lines.append(line)
            # Progress indicator every 100 rows
            if len(lines) % 100 == 0:
                print(f"  {len(lines)} rows received...")

    ser.close()

    if not lines:
        print("ERROR: No data received. Is the device running and logging?")
        sys.exit(1)

    # First line should be the CSV header
    if not lines[0].startswith("timestamp"):
        print("WARNING: First line doesn't look like a CSV header.")

    with open(outfile, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"\nSaved {len(lines) - 1} rows to {outfile}")  # -1 for header
    print(f"File: {outfile}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="RipenSense serial CSV dump")
    parser.add_argument("--port",  default=None,               help="Serial port")
    parser.add_argument("--out",   default="outputs/shipment_dump.csv", help="Output CSV filename")
    args = parser.parse_args()

    port = args.port or find_port()
    dump(port, args.out)
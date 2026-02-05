#!/usr/bin/env python3
import argparse
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"

parser = argparse.ArgumentParser(description="Flash firmware to ESP32-C3")
parser.add_argument(
    "--port",
    default=os.environ.get("POOFER_SERIAL_PORT"),
    help="Serial port, e.g. /dev/cu.usbmodemXXXX",
)
args = parser.parse_args()

idf_py = os.environ.get("POOFER_IDF_PY", "idf.py")
cmd = [idf_py]
if args.port:
    cmd += ["-p", args.port]
cmd += ["flash"]

subprocess.run(cmd, cwd=FIRMWARE, check=True)

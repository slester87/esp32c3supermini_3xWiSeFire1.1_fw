#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"

parser = argparse.ArgumentParser(description="Monitor serial output")
parser.add_argument("--port", help="Serial port, e.g. /dev/cu.usbmodemXXXX")
args = parser.parse_args()

cmd = ["idf.py"]
if args.port:
    cmd += ["-p", args.port]
cmd += ["monitor"]

subprocess.run(cmd, cwd=FIRMWARE, check=True)

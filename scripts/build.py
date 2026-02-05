#!/usr/bin/env python3
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"

subprocess.run(["idf.py", "build"], cwd=FIRMWARE, check=True)

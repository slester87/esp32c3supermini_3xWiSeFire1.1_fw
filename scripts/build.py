#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"

idf_py = os.environ.get("POOFER_IDF_PY", "idf.py")
subprocess.run([idf_py, "build"], cwd=FIRMWARE, check=True)

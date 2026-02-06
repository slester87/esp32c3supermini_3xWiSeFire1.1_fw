#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "firmware" / "build"
SPIFFS_BIN = BUILD_DIR / "spiffs.bin"
FLASHER_ARGS = BUILD_DIR / "flasher_args.json"


def _fail(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def _load_spiffs_offset() -> int:
    if not FLASHER_ARGS.exists():
        _fail(f"Missing {FLASHER_ARGS}")
    data = json.loads(FLASHER_ARGS.read_text())
    for item in data.get("flash_files", []):
        if str(item.get("path", "")).endswith("spiffs.bin"):
            return int(item.get("offset", "0"), 0)
    _fail("spiffs.bin offset not found in flasher_args.json")



def main() -> None:
    if not SPIFFS_BIN.exists():
        _fail(f"Missing {SPIFFS_BIN}")

    offset = _load_spiffs_offset()
    if offset <= 0:
        _fail("Invalid SPIFFS offset")

    # spiffs.bin should start with magic bytes 0xE5 0xE5
    with SPIFFS_BIN.open("rb") as f:
        header = f.read(2)
    if header != b"\xE5\xE5":
        _fail("spiffs.bin magic bytes missing (expected 0xE5 0xE5)")

    # Basic sanity: size should be non-trivial
    size = SPIFFS_BIN.stat().st_size
    if size < 4096:
        _fail("spiffs.bin too small to contain UI assets")

    print("SPIFFS bin looks valid.")


if __name__ == "__main__":
    main()

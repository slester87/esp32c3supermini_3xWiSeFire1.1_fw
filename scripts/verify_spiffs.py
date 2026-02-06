#!/usr/bin/env python3
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "firmware" / "build"
SPIFFS_BIN = BUILD_DIR / "spiffs.bin"
FLASHER_ARGS = BUILD_DIR / "flasher_args.json"


def _fail(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def _has_spiffs_entry() -> bool:
    if not FLASHER_ARGS.exists():
        _fail(f"Missing {FLASHER_ARGS}")
    data = json.loads(FLASHER_ARGS.read_text())
    flash_files = data.get("flash_files", {})

    if isinstance(flash_files, dict):
        return any(str(path).endswith("spiffs.bin") for path in flash_files.values())

    if isinstance(flash_files, list):
        for item in flash_files:
            if isinstance(item, dict) and str(item.get("path", "")).endswith("spiffs.bin"):
                return True
        return False

    return False


def main() -> None:
    if not SPIFFS_BIN.exists():
        _fail(f"Missing {SPIFFS_BIN}")

    if not _has_spiffs_entry():
        _fail("spiffs.bin not listed in flasher_args.json")

    # Basic sanity: size should be non-trivial
    size = SPIFFS_BIN.stat().st_size
    if size < 4096:
        _fail("spiffs.bin too small to contain UI assets")

    print("SPIFFS bin looks valid.")


if __name__ == "__main__":
    main()

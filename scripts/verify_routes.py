#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN_C = ROOT / "firmware" / "main" / "main.c"
INDEX_HTML = ROOT / "firmware" / "spiffs" / "index.html"
WIFI_HTML = ROOT / "firmware" / "spiffs" / "wifi.html"


def _fail(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    if not MAIN_C.exists():
        _fail(f"Missing {MAIN_C}")
    if not INDEX_HTML.exists():
        _fail(f"Missing {INDEX_HTML}")
    if not WIFI_HTML.exists():
        _fail(f"Missing {WIFI_HTML}")

    src = MAIN_C.read_text()

    required = [
        '"/"',
        '"/wifi"',
        'index_handler',
        'wifi_get_handler',
    ]

    for token in required:
        if token not in src:
            _fail(f"Expected token not found in main.c: {token}")

    print("Routes and UI assets look present.")


if __name__ == "__main__":
    main()

# Install

## Docker (no local toolchain required)

If you have Docker installed, you can build, lint, and develop the UI without installing ESP-IDF or clang tools locally.

```bash
make build     # Build firmware
make lint      # Run all linters
make verify    # Run verification scripts
make shell     # Interactive shell in build container
make ui        # Start mock UI server on localhost:8080
make clean     # Remove build caches
```

The mock UI server at `http://localhost:8080` simulates the firmware's WebSocket behavior, so you can develop and test the control interface without hardware.

See `Makefile` for all available targets.

## Local Setup

This section walks through a full local setup for building, flashing, and monitoring the Poofer firmware.

## Prerequisites

- macOS, Linux, or WSL2
- Python 3.11+
- ESP-IDF 5.5.x (this repo uses 5.5.2)
- USB-C data cable
- The ESP32-C3 Super Mini board
- A 12V supply for the solenoid driver PCB

Optional but recommended development tools:

- `clang-format`
- `clang-tidy`
- `pre-commit`

## Install ESP-IDF

Follow Espressif's official install guide for ESP-IDF 5.5.x.

Make sure `idf.py` is available on your PATH. You should be able to run:

```bash
idf.py --version
```

Set your ESP-IDF path once per shell:

```bash
export ESP_IDF_PATH=/path/to/esp-idf
```

If `idf.py` is not on your PATH in a new shell session, source the ESP-IDF environment:

```bash
source "$ESP_IDF_PATH/export.sh"
```

You can also set local defaults by copying `.env.example` to `.env` and editing it.

## Project Setup

```bash
cd /path/to/esp32c3supermini_3xWiSeFire1.1_fw
```

Optional but recommended: install git hooks for local linting.

```bash
python3 -m pip install --user pre-commit
pre-commit install
```

If you want linting to run on push as well:

```bash
pre-commit install --hook-type pre-push
```

## Build

```bash
python3 scripts/build.py
```

## Flash

```bash
python3 scripts/flash.py --port /dev/cu.usbmodemXXXX
```

## Monitor

```bash
python3 scripts/monitor.py --port /dev/cu.usbmodemXXXX
```

## Lint

```bash
scripts/lint.sh
```

Notes:

- `clang-tidy` requires `firmware/build/compile_commands.json`.
- You can generate it with an IDF build. If it is missing, the lint script will skip `clang-tidy`.

## Wi-Fi And UI

- AP SSID: `Poofer-AP`
- AP password: `FlameoHotMan`
- Wi-Fi setup page: `http://192.168.4.1/wifi`
- Control UI: `http://192.168.4.1/`
- mDNS after STA join: `http://poofer.local/`

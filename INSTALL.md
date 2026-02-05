# Install

This guide walks through a full local setup for building, flashing, and monitoring the Poofer firmware.

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

- AP SSID: `Poofer-built-by-Skip`
- AP password: `FlameoHotMan`
- Wi-Fi setup page: `http://192.168.4.1/wifi`
- Control UI: `http://192.168.4.1/`
- mDNS after STA join: `http://poofer.local/`

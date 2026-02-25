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

## Dependencies

This project depends on **ESP-IDF 5.5.x** (tested with **5.5.2**) and Python 3.

If `idf.py` is missing in your shell, build/flash/monitor will fail. Use this section as the recovery checklist.

### Required

- Python 3.11+ (`python3 --version`)
- ESP-IDF 5.5.2 checkout (example: `/Users/skippo/Development/Codex/esp-idf-v5.5.2`)

### One-Time ESP-IDF Install

```bash
cd /Users/skippo/Development/Codex/esp-idf-v5.5.2
./install.sh
```

### Per-Shell Activation (Critical)

Run this in every new terminal before using project scripts:

```bash
source /Users/skippo/Development/Codex/esp-idf-v5.5.2/export.sh
```

Verify:

```bash
which idf.py
idf.py --version
```

Expected output should include `ESP-IDF v5.5.2`.

### Optional Shell Helper

Add to `~/.zshrc`:

```bash
idf55() {
  source /Users/skippo/Development/Codex/esp-idf-v5.5.2/export.sh
}
```

Then run `idf55` in new shells.

### Project Script Breadcrumbs

- `scripts/build.py`, `scripts/flash.py`, and `scripts/monitor.py` call `idf.py`.
- Error `No such file or directory: 'idf.py'` means ESP-IDF was not activated in that shell.
- Optional `.env` settings:
  - `POOFER_IDF_PATH`
  - `POOFER_IDF_PY`
  - `POOFER_SERIAL_PORT`

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

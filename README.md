# Poofer Firmware (ESP32-C3 Super Mini)

![CI](https://github.com/slester87/esp32c3supermini_3xWiSeFire1.1_fw/actions/workflows/ci.yml/badge.svg?branch=main)

Firmware and UI for a 3-channel WiSeFire 1.1-driven poofer controller built on an ESP32-C3 Super Mini.

## Quick Start

For a full setup guide, see `INSTALL.md`.

Condensed steps:

- Install ESP-IDF 5.5.x and ensure `idf.py` is on your PATH.
- Optional: copy `.env.example` to `.env` to set defaults like `POOFER_SERIAL_PORT`.
- Build: `python3 scripts/build.py`
- Flash: `python3 scripts/flash.py --port /dev/cu.usbmodemXXXX`
- Monitor: `python3 scripts/monitor.py --port /dev/cu.usbmodemXXXX`
- Connect to AP `Poofer-AP` and open `http://192.168.4.1/`

## Hardware

- ESP32-C3 Super Mini dev board: https://www.amazon.com/dp/B0D4QK5V74
- Solenoid: https://www.amazon.com/dp/B00DQ1J4H0
- Power supply (12V): https://www.amazon.com/dp/B0D9D5L3B5
- Custom PCB with 3x WiSeFire 1.1 connections
- Full Bill of Materials: `BOM.md`

## Wiring And LED Mapping

GPIO4 drives a 3-pixel WS2812 chain.

- Pixel 0 is a physical on-board LED soldered to the ESP32 board.
- Pixel 1 is a virtual pixel used for solenoid control via the custom PCB.
- Pixel 2 is a virtual pixel used as a firing indicator.
This mapping is intentional and should be preserved. The firmware treats the chain as three pixels.
Currently, the firmware drives Pixel 1 and Pixel 2 as white (`R=G=B`). On the WiSeFire board, Pixel 1 white
fires solenoids 1 and 2, and Pixel 2 white fires solenoid 3.

## Architecture

- AP + STA Wi-Fi mode
- SPIFFS for UI assets
- HTTP server for UI pages and Wi-Fi form
- WebSocket control channel
- NVS storage for STA credentials
- mDNS hostname `poofer`

## Web UI

- Control UI: `http://192.168.4.1/`
- Wi-Fi setup: `http://192.168.4.1/wifi`
- mDNS after STA join: `http://poofer.local/`

### UI Screenshots

Ready state:

![Poofer Control UI ready state](EC702A98-FD31-49A0-B811-E277FAC205C0_4_5005_c.jpeg)

Firing state:

![Poofer Control UI firing state](11089010-D734-4728-B3B4-9B94340ECF5F_4_5005_c.jpeg)

## WebSocket Protocol

Messages from client to device:

- `DOWN` starts a press
- `UP` ends a press
- `PING` requests a state update

State messages from device to client:

```json
{
  "ready": true,
  "firing": false,
  "error": false,
  "connected": true,
  "elapsed_ms": 0,
  "last_hold_ms": 250
}
```

## Configuration

Defaults are defined in `firmware/main/main.c`.

- AP SSID and password
- GPIO pin for the LED/solenoid chain
- Minimum and maximum hold times (0.25s min, 3s max)
- mDNS hostname and HTTP routes
- Status LED colors: green = ready, orange = firing, blue = disconnected, red = fault

## Development

- Linting entry point: `scripts/lint.sh`
- Git hooks: `pre-commit install`

## Releases

Firmware artifacts are built in CI for tags matching `fw-*`.

The workflow uploads these files as artifacts:

- `bootloader.bin`
- `partition-table.bin`
- `poofer.bin`
- `spiffs.bin`
- `flasher_args.json` and `*_flash_args` helpers

Create a release tag:

```bash
scripts/release_tag.sh 1.0.0
```

## Config

Optional local config is supported via `.env`.

- Copy `.env.example` to `.env`
- Set `POOFER_IDF_PATH`, `POOFER_SERIAL_PORT`, and any optional overrides

## Safety

TODO: Add explicit safety guidelines and operational constraints.

## License

MIT. See `LICENSE`.

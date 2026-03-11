# Hardware-In-The-Loop Testing

HIL observation in this repo is constrained to a single electrical point: the existing WS2812 data waveform on `GPIO4`.

There are no separate debug marker pins in the supported design. If you attach a logic analyzer for HIL work, you observe the raw waveform already driven on `GPIO4`.

## Current HIL Pieces

- A deterministic websocket scenario runner:
  - `scripts/hil/run_scenario.py`
- A repeatable sigrok capture wrapper:
  - `scripts/hil/capture_sigrok.py`
- Seed scenarios:
  - `scripts/hil/scenarios/deadman_hold_stop.json`
  - `scripts/hil/scenarios/deadman_socket_close.json`

## Host Bring-Up Checklist

Before assuming a logic analyzer is connected and usable, establish each layer explicitly:

1. Confirm USB enumeration on the host.
2. Confirm the analyzer model by unplug/replug delta, not by guesswork.
3. Confirm analyzer tooling is installed.
4. Confirm the tool can enumerate the analyzer.
5. Confirm a short capture can be taken and exported.

Useful host commands:

```bash
system_profiler SPUSBDataType
ls /dev/cu.* /dev/tty.*
which sigrok-cli
which pulseview
```

## Current Bring-Up Notes For This Mac

Observed during this session:

- `sigrok-cli` was not installed.
- `pulseview` was not installed.
- The ESP32-C3 board was positively identified as:
  - `USB JTAG/serial debug unit`
  - vendor `0x303a`
  - product `0x1001`
  - serial port `/dev/cu.usbmodem11201`
- Another USB device is present as:
  - `Vendor-Specific Device`
  - vendor `0x0925`
  - product `0x3881`
  - no serial node exposed

That vendor-specific USB device is only a candidate until confirmed by unplug/replug delta against the analyzer itself.

Confirmed after installing `sigrok-cli` and probing USB directly:

- The connected analyzer is detected by sigrok as:
  - driver: `fx2lafw`
  - display name: `Saleae Logic with 8 channels`
  - connection id on this host: `1.13`
- Supported samplerates reported locally:
  - `20 kHz`
  - `25 kHz`
  - `50 kHz`
  - `100 kHz`
  - `200 kHz`
  - `250 kHz`
  - `500 kHz`
  - `1 MHz`
  - `2 MHz`
  - `3 MHz`
  - `4 MHz`
  - `6 MHz`
  - `8 MHz`
  - `12 MHz`
  - `16 MHz`
  - `24 MHz`
  - `48 MHz`

## Host Dependencies

For a scriptable HIL path on macOS, the minimum recommended dependency is:

- `sigrok-cli`

Optional but useful:

- `pulseview` for manual inspection

The repo-side HIL scripts do not require extra Python packages beyond the system Python already used here.

Install command used successfully on this machine:

```bash
brew install sigrok-cli
```

Verification commands:

```bash
sigrok-cli --scan
sigrok-cli -d fx2lafw --show
```

## Wiring Model

- Connect logic analyzer ground to DUT ground.
- Connect one analyzer channel to `GPIO4` on the ESP32-C3.
- `GPIO4` is already the NeoPixel output used by the firmware.

The observable signal is the raw WS2812 waveform itself. This is intentional and should not be replaced with extra HIL-only pins.

## Recommended Logic Analyzer Setup

- Analyzer channel count needed: `1`
- Signal to probe: `GPIO4`
- Sample rate:
  - minimum: `10 MHz`
  - recommended: `12 MHz` or higher

Lower rates can miss or badly distort the WS2812 bitstream.

## Running A Scenario

Example against the device AP:

```bash
python3 scripts/hil/run_scenario.py deadman_hold_stop.json --host 192.168.4.1 --log-json artifacts/hil/deadman_hold_stop/run.json
```

This records host-side send timing for the websocket scenario. It does not replace analyzer measurements; it complements them.

## Capturing With Sigrok

This repo includes a thin wrapper for repeatable captures:

```bash
python3 scripts/hil/capture_sigrok.py artifacts/hil/raw/deadman_hold_stop.sr \
  --driver fx2lafw \
  --conn 1.13 \
  --samplerate 12m \
  --time-ms 1500 \
  --channels D0
```

Suggested channel mapping:

- `D0`: DUT `GPIO4`

If you physically clip a different analyzer input to `GPIO4`, change `--channels` to match that analyzer channel name.

You can also inspect capabilities directly:

```bash
sigrok-cli -d fx2lafw --show
```

## Interpreting Captures

`GPIO4` carries the WS2812 serial waveform for the three-pixel chain documented in [README.md](/Users/skippo/Development/Codex/Poofer/README.md#L124):

- Pixel 0: board status LED
- Pixel 1: virtual solenoid-control pixel
- Pixel 2: virtual firing-indicator pixel

For HIL work, correlate:

- host-side websocket event timing from `scripts/hil/run_scenario.py`
- analyzer timing on the `GPIO4` waveform
- the firmware's known state transitions and `led_strip_refresh()` calls

There is not currently a repo-side decoder that turns the raw `GPIO4` waveform into higher-level dead-man timing verdicts. Any automated evaluator must operate on the WS2812 waveform model, not on nonexistent debug pins.

## CI Path

The intended CI path is:

1. Dedicated hardware runner flashes the DUT.
2. Runner starts logic analyzer capture.
3. Runner executes `scripts/hil/run_scenario.py`.
4. Runner archives raw `GPIO4` captures plus scenario timing logs.
5. Reports and raw captures are uploaded as artifacts.

This repo does not yet include waveform decoding or analyzer-vendor capture automation. That should be added once the lab stack is chosen:

- Saleae automation API, or
- `sigrok-cli`

## Next Bring-Up Milestones

1. Validate a real `GPIO4` capture with `scripts/hil/capture_sigrok.py`.
2. Document the expected WS2812 waveform patterns for ready, firing, release, and dead-man timeout cases.
3. Add a waveform-aware decoder/export step under `scripts/hil/`.
4. Add analyzer-specific automation once capture/export is reproducible.
5. Add a hardware-runner CI workflow once waveform interpretation is stable.

# Project Functionality

Last updated: 2026-02-25

## Purpose (Current Understanding)
- `Poofer` is ESP32-C3 firmware + web UI for a 3-channel WiSeFire-based solenoid/flame controller.

## Functional Model
- WebSocket messages:
  - `DOWN`: begin firing press
  - `HOLD`: keep firing liveness asserted during active press
  - `UP`: end firing press
  - `PING`: state sync/heartbeat
- Firing guardrails:
  - Minimum hold: 250 ms
  - Maximum hold: 3000 ms
  - Dead-man liveness timeout while firing: 100 ms

## Current Safety-Relevant Behavior
- Solenoid is forced off if `HOLD` heartbeats stop for >100 ms while firing.
- UI sends `HOLD` every 50 ms only during active press.
- Existing max-hold and websocket-disconnect protections remain in place.

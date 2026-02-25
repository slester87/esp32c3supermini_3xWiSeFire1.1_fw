# Unified Control State Machine

## Summary
Replace distributed control logic with a single explicit event-driven state machine so timing and safety behavior are defined once and executed consistently.

## Why This Is Needed
The current implementation enforces key control behavior in multiple locations:
- Timer callback path for max-hold cutoff.
- Polling loop path for max-hold and websocket timeout behavior.

This increases the chance of drift and edge-case divergence. In a solenoid/flame-control system, that is unacceptable because control semantics must be deterministic and auditable.

## Current Evidence In Code
- Max-hold timer cutoff logic: `firmware/main/main.c` around `max_hold_timer_cb`.
- Polling duplicate max-hold cutoff logic: `firmware/main/main.c` around `status_task`.
- Release/min-hold handling split across timer callback + press handler.

## Target Design
Create a pure state machine module with:
- State enum and runtime model.
- Event enum (`EV_PRESS_DOWN`, `EV_PRESS_UP`, `EV_TICK`, `EV_WS_RX`, `EV_WS_TIMEOUT`, `EV_MAX_HOLD_EXPIRED`, etc.).
- Transition function:
  - Input: current state + event + timestamp/context.
  - Output: next state + side-effect commands.

Side effects are not executed inside transition logic; they are returned as commands and applied by the caller (solenoid set, LED update, ws publish, timer arm/disarm).

## Core Principles
- Single source of truth for transition rules.
- No duplicated control policy.
- Predictable transitions with explicit guards and invariants.
- Make invalid events no-ops, never implicit behavior.

## Proposed File Layout
- `firmware/main/control_state.h`
- `firmware/main/control_state.c`
- `firmware/main/control_types.h` (optional)

## Data Model Sketch
- `control_state_t` (BOOT, READY, FIRING, DISCONNECTED, ERROR)
- `control_runtime_t`
  - `state`
  - `press_active`
  - `press_start_us`
  - `last_hold_ms`
  - `ws_connected`
  - `last_ws_rx_us`
  - `press_ignore_until_release`
  - `release_pending`
- `control_event_t`
- `control_actions_t`
  - `set_solenoid_level`
  - `set_status_led`
  - `schedule_max_hold`
  - `schedule_min_hold`
  - `cancel_timers`
  - `publish_state`

## Implementation Plan
1. Add module with transition function and unit-testable pure logic.
2. Move press/timer/ws timeout decisions from `main.c` into the module.
3. Keep existing hardware/web plumbing but make it consume `control_actions_t`.
4. Remove duplicated max-hold logic from polling loop.
5. Reduce polling loop to heartbeat/timeout event producer only (or remove if timers/events fully cover needs).

## Acceptance Criteria
- Exactly one code path defines max-hold and min-hold policy.
- For every event, transition decision is in `control_state.c`.
- `main.c` no longer contains policy conditionals for press timing.
- Existing behavior preserved for normal DOWN/UP/PING flows.
- Solenoid always transitions to safe level on terminal/timeout/error paths.

## Testing Strategy
- Unit tests for transition table:
  - DOWN from READY starts FIRING.
  - UP before min hold schedules/persists until min reached.
  - Max hold expires forces READY and disallows immediate re-fire until release.
  - WS timeout during firing forces stop + DISCONNECTED.
- Regression tests for emitted state payload fields (`ready`, `firing`, etc.).

## Risks
- Behavioral regressions during extraction if side effects are still mixed.
- Timer/event ordering assumptions may become visible during cleanup.

## Risk Mitigations
- Add golden transition tests before refactor completion.
- Migrate incrementally with temporary assertions for invariant checks.

## Dependencies
- Works best when paired with the “Single State Owner via Event Queue” backlog item.

# Modular Firmware Architecture

## Summary
Break up `firmware/main/main.c` into cohesive modules with narrow responsibilities. Keep `app_main()` as assembly/composition code.

## Why This Is Needed
Current file scope combines:
- Control policy and state transitions.
- HTTP routing and websocket protocol.
- Wi-Fi provisioning and credential persistence.
- LED/solenoid hardware output.
- Timer and task lifecycle.

This inflates cognitive load and makes changes high-risk because unrelated concerns are physically entangled.

## Goals
- Improve readability and maintainability.
- Enable focused testing per concern.
- Reduce merge conflicts and accidental regressions.
- Allow more precise error handling and instrumentation.

## Proposed Module Boundaries
- `control_state.*`:
  - domain policy and transitions only.
- `transport_ws.*`:
  - websocket frame parsing/serialization and callbacks.
- `http_routes.*`:
  - route registration and static file handlers.
- `wifi_provisioning.*`:
  - AP+STA init, event handling, NVS credentials.
- `led_output.*`:
  - status/firing pixel operations and hardware abstraction.
- `spiffs_assets.*`:
  - mount + asset serving helpers.
- `app_main.c`:
  - wiring, startup sequencing, and task creation.

## Coding Standards For Split
- Each module exposes a small public header with opaque/internal structs where possible.
- Cross-module dependencies must flow inward (transport/hardware call into control decisions, not vice versa).
- No module should directly mutate global shared runtime except owner module.

## Incremental Migration Plan
1. Extract pure helpers first (`url_decode`, form parsing, file senders).
2. Extract Wi-Fi/NVS provisioning next.
3. Extract control logic and LED/solenoid mapping.
4. Extract websocket handler/serialization.
5. Reduce `main.c` to orchestration and keep temporary adapter functions until complete.

## Acceptance Criteria
- `main.c` is substantially reduced and mostly startup wiring.
- No module exceeds a reasonable size target (e.g., ~300 LOC soft limit; justify exceptions).
- Control policy code is isolated from HTTP transport code.
- Equivalent runtime behavior demonstrated by existing manual flow + automated tests.

## Testing Plan
- Existing CI checks remain green.
- Add module-level tests where logic is pure (parsers and state transitions).
- Add smoke test script for route registration and websocket protocol handling.

## Risks
- Initial churn may increase short-term instability.
- Link-time issues from cyclic dependencies.

## Mitigations
- Do module extraction in small PR-sized steps.
- Enforce strict include dependencies and minimal public interfaces.

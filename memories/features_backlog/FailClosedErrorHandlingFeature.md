# Fail-Closed Error Handling and Initialization Guards

## Summary
Add systematic return-code checks and centralized failure handling across ESP-IDF initialization, timers, Wi-Fi, mDNS, SPIFFS, and HTTP startup. Any critical failure must fail closed (solenoid off, safe status).

## Why This Is Needed
Many current calls do not verify `esp_err_t`, making startup and runtime behavior ambiguous when failures occur. For a hazardous actuator controller, unknown partial-init states are unacceptable.

## Key Gaps To Address
- Initialization calls with unchecked return status.
- Timer creation/start/stop errors not surfaced.
- SPIFFS mount failures not explicitly handled.
- mDNS and route registration errors not handled consistently.
- Lack of a single “safe fault mode” execution path.

## Design Requirements
- Introduce a helper macro/function for checked ESP-IDF calls.
- Centralize error transition path:
  - set solenoid output off
  - set state/error LED
  - log error context
  - prevent further firing actions
- Distinguish recoverable vs fatal errors.

## Suggested Patterns
- `CHECK_ESP(err, label, "context")` style macro.
- `enter_fault_state(reason_code)` function callable from any context.
- Structured logs with subsystem tags and reason codes.

## Implementation Plan
1. Enumerate all critical API calls and classify as fatal/recoverable.
2. Add checked wrappers and replace direct unchecked calls.
3. Implement centralized fault-state function.
4. Update startup sequence to abort gracefully on fatal failures.
5. Add tests/smoke checks for simulated failures where possible.

## Acceptance Criteria
- All critical init/start/register APIs have explicit error handling.
- Fault path always forces safe output state.
- Logs clearly indicate failing subsystem and error code.
- No silent startup failures.

## Verification
- Introduce failure injection points in host tests for parser/state paths.
- Manual negative tests:
  - SPIFFS mount fail simulation.
  - HTTP server start fail simulation.
  - Wi-Fi init fail simulation.

## Risks
- More verbose code if checks are naive.
- Overly aggressive fatal handling can reduce availability.

## Mitigation
- Use compact wrappers to keep code readable.
- Document recoverability policy per subsystem.

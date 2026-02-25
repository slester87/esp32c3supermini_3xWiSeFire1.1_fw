# Parser Correctness and Test Depth Expansion

## Summary
Replace brittle ad-hoc parsing with robust parsing routines and expand automated tests from presence checks to behavior checks for control logic, timing rules, and input handling.

## Why This Is Needed
Current checks are useful but shallow:
- CI validates route/assets existence and SPIFFS artifact inclusion.
- Missing deeper validation for protocol/state behavior.
- Form parsing currently depends on substring search and fixed temp buffers.

## Goals
- Make parser behavior correct and bounded for all expected inputs.
- Increase confidence in refactors through deterministic tests.
- Detect regressions before hardware testing.

## Parser Improvements
- Replace `strstr` key lookup with exact key-value tokenization for URL-encoded bodies.
- Handle repeated keys, missing values, malformed escapes, and boundary lengths explicitly.
- Remove unnecessary temporary buffer assumptions where possible.
- Return parse status codes to callers for clearer validation behavior.

## Test Expansion Plan
1. Unit tests (host-side)
- `url_decode` behavior matrix:
  - valid `%XX`, malformed `%`, plus-to-space, unicode bytes passthrough behavior
- form parser cases:
  - exact key matching
  - prefix collisions (`ssid` vs `myssid`)
  - truncation and bounds behavior

2. Control state tests
- Event-driven tests for DOWN/UP/PING + timer expirations.
- Timeout behavior and disconnect transitions.
- Min/max hold invariants.

3. Integration smoke checks
- Route registration and static file serving still valid.
- Websocket message acceptance/rejection behavior.

## CI Enhancements
- Add host-test job (fast, no hardware).
- Keep current lint/build jobs.
- Fail CI on parser/state test regressions.

## Acceptance Criteria
- Parser has deterministic behavior for malformed input.
- State-machine tests cover all major transitions and edge timing boundaries.
- CI includes behavior tests in addition to lint/build/presence checks.

## Risks
- Initial setup overhead for host-side C tests.
- Potential mismatch between host and target behavior if abstractions are poor.

## Mitigation
- Keep pure logic target-agnostic and test it on host.
- Isolate ESP-IDF dependencies behind thin adapters.

# Digital Logic Analyzer Hardware-in-the-Loop (HIL) Safety Test Plan

## Summary
Establish repeatable hardware-in-the-loop tests using a digital logic analyzer to verify that the poofer solenoid control signal always de-asserts within strict safety bounds when websocket liveness is lost.

Primary objective:
- Prove the dead-man behavior in real hardware, not just code inspection.
- Measure and enforce timing with objective pass/fail criteria.

## Why This Is Urgent
The most dangerous forbidden case is sustained solenoid activation after control-path loss. We have implemented dead-man liveness in firmware (100 ms timeout) and browser hold beats (50 ms). This plan validates the real-world behavior under disconnect and fault conditions.

## Scope
In-scope:
- Timing and correctness of solenoid ON/OFF signal behavior during normal and faulted websocket sessions.
- Loss-of-liveness cases (browser close, network drop, AP disconnect, CPU load).
- Verification against hard latency limits.

Out-of-scope (for this phase):
- Flame performance and gas dynamics.
- Thermal behavior and long-duration burn testing.
- EMI immunity certification.

## Safety Requirements (Tested)
Required invariants:
1. Solenoid line must go LOW (closed/off) after liveness loss within bounded time.
2. Maximum OFF latency target after last valid HOLD frame correspondence: <= 100 ms nominal, <= 150 ms stressed, and never > 250 ms.
3. Solenoid must never remain ON indefinitely after client disconnect.
4. Recovery must require intentional re-press, not stale state replay.

## Hardware Setup
Required equipment:
- Digital logic analyzer (>= 2 channels, >= 1 MHz sample rate minimum; 10 MHz preferred).
- Test fixture access points for:
  - Solenoid control digital signal (the second virtual LED data result / downstream control gate signal).
  - Optional sync pin from firmware for event markers.
- ESP32-C3 target running test firmware.
- Host machine running browser client + test harness.

Recommended channel mapping:
- CH0: Solenoid control line (or closest digital gate equivalent).
- CH1: Optional firmware debug GPIO pulse marker.
- CH2 (optional): Wi-Fi disconnect trigger marker if externally controlled.

## Instrumentation Strategy
To tighten timing attribution, add temporary debug markers in firmware:
- `DBG_PIN_LAST_HOLD_RX`: pulse when `HOLD` message is processed.
- `DBG_PIN_DEADMAN_EXPIRE`: pulse when hold-liveness timeout callback fires.
- `DBG_PIN_SOLENOID_OFF`: pulse immediately before solenoid line de-assertion call.

If only one debug pin is available, encode with pulse counts or widths.

## Test Architecture
### Components
1. Device Under Test (DUT)
- ESP32 firmware with dead-man logic enabled.

2. Traffic Driver
- Scripted browser/client that can:
  - press and hold (`DOWN` + periodic `HOLD`)
  - abruptly stop `HOLD`
  - disconnect socket
  - simulate jitter and packet loss patterns

3. Capture Controller
- Logic analyzer capture config + trigger conditions.
- Automated export of timestamps and transitions.

4. Result Evaluator
- Parser that computes latencies and verdicts.
- Produces machine-readable report (`json`) + human summary (`md`).

## Test Matrix
### A. Baseline control behavior
1. Normal hold and release
- Action: send DOWN, periodic HOLD, then UP.
- Expectation: line ON during hold, OFF immediately after UP path constraints.

2. Max hold cutoff
- Action: maintain hold beyond max-hold threshold.
- Expectation: forced OFF at configured max hold boundary.

### B. Dead-man liveness behavior (critical)
1. HOLD stream stops, websocket still open
- Action: DOWN + HOLD for 1 s, then stop HOLD without UP.
- Expectation: OFF within timeout budget.

2. Abrupt websocket close while held
- Action: DOWN + HOLD, then socket close.
- Expectation: OFF within timeout budget.

3. Browser tab/process kill while held
- Action: DOWN + HOLD, force-kill client process/tab.
- Expectation: OFF within timeout budget.

4. AP drop / network cut while held
- Action: DOWN + HOLD, remove network path.
- Expectation: OFF within timeout budget.

### C. Stress and robustness
1. CPU and scheduler stress
- Action: apply background load on DUT while repeating dead-man tests.
- Expectation: still within stressed bounds.

2. Packet jitter/loss
- Action: vary HOLD intervals and inject delayed frames.
- Expectation: no false sustained ON condition.

3. Burst reconnect attempts
- Action: repeated connect/disconnect cycles with rapid presses.
- Expectation: no stale press state survives disconnect.

## Metrics and Pass/Fail
Measured latencies:
- `T_hold_last_to_off`: last observed HOLD-associated marker to solenoid OFF edge.
- `T_disconnect_to_off`: disconnect event proxy to OFF edge.
- `T_deadman_cb_to_off`: timeout callback marker to OFF edge.

Pass criteria:
- P0 safety gate:
  - 0 occurrences of OFF latency > 250 ms.
  - 0 occurrences of stuck ON after disconnect scenarios.
- P1 performance target:
  - 99th percentile `T_hold_last_to_off` <= 150 ms under stress.
  - Median near configured timeout (around 100 ms).

## Execution Cadence
- Per-commit (fast subset):
  - dead-man test case B1 and B2, 10 iterations each.
- Nightly full suite:
  - full matrix with stress cases, >= 100 iterations per critical scenario.
- Pre-release gate:
  - full suite pass required with artifact retention.

## Data and Artifacts
Store per run:
- Raw analyzer capture files.
- Exported transition CSV/JSON.
- Computed latency report.
- Rendered summary markdown with pass/fail and percentile table.

Suggested artifact path:
- `artifacts/hil/<date-time>/<scenario>/...`

## Automation Plan
Phase 1 (manual-assisted):
- Operator starts capture and runs scripted scenario commands.
- Script parses export and prints verdict.

Phase 2 (semi-automated):
- CLI controls scenario playback + capture start/stop.
- Single command runs chosen scenario set and emits report.

Phase 3 (CI-connected lab runner):
- Dedicated hardware runner executes suite on schedule or on tagged commits.
- Results uploaded as CI artifacts and used as release gate.

## Tooling Options
Analyzer stack (choose one):
- Option A: Saleae Logic + automation API.
- Option B: Sigrok (`sigrok-cli`) + PulseView-compatible capture/export.

Client driver options:
- Browser automation (Playwright) for realistic UI interaction.
- Raw websocket harness for deterministic protocol-level scenarios.

Recommendation:
- Use websocket harness for deterministic dead-man timing, then one Playwright smoke test for UI-path realism.

## Implementation Tasks
1. Add debug GPIO instrumentation flags in firmware (build-time toggle).
2. Create `scripts/hil/` with:
- scenario runner
- capture wrapper
- report generator
3. Define scenario YAML/JSON files for matrix coverage.
4. Add runbook for lab setup and calibration.
5. Add pre-release checklist entry requiring HIL pass report.

## Risks and Mitigations
Risk: Measurement skew due to missing synchronized event markers.
- Mitigation: use dedicated debug GPIO pulses.

Risk: False confidence from too-low sample rate.
- Mitigation: standardize analyzer rate at >= 10 MHz where possible.

Risk: Manual setup drift between operators.
- Mitigation: fixture photos, wiring map, and scripted preflight checks.

Risk: Flaky network test reproducibility.
- Mitigation: run larger iteration counts and report distributions, not single samples.

## Open Decisions
1. Analyzer platform selection (Saleae vs sigrok).
2. Exact stressed pass threshold (150 ms recommended).
3. Whether to keep debug markers in production firmware behind compile flag.

## Done Definition
This feature is complete when:
- Critical dead-man scenarios are automated and repeatable.
- Reports prove no stuck-ON condition and no >250 ms OFF latency excursions.
- Pre-release process enforces passing HIL safety report.

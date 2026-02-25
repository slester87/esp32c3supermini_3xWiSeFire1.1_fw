# Single State Owner via Event Queue

## Summary
Move from lock-based shared mutable state to a single-owner control task that processes events from a queue. Other contexts (HTTP callbacks, timers, Wi-Fi events) publish events only.

## Why This Is Needed
Current concurrency model:
- Multiple callbacks/tasks take a mutex and mutate shared runtime directly.
- This works but scales poorly as the number of events grows.
- Safety-critical behavior is easier to verify with ownership than distributed locking.

## Objectives
- Eliminate most explicit lock/unlock logic.
- Guarantee serialized state transitions.
- Simplify reasoning about timing and ordering.

## Proposed Model
- Create `control_task` as the only writer of runtime state.
- Define `control_event_t` queue.
- Producers:
  - WS message handler pushes DOWN/UP/PING events.
  - Timer callbacks push timer-expired events.
  - Wi-Fi/connection watchers push connectivity events.
- Consumer:
  - `control_task` runs transition function and executes output actions.

## Event Queue Details
- Use FreeRTOS queue sized for burst handling (tunable; start with conservative depth).
- Event payload includes timestamp and optional metadata.
- On queue full:
  - Drop only idempotent/low-priority telemetry events.
  - Never drop stop/safety events.

## Implementation Steps
1. Define event structs and queue in a new module.
2. Convert one producer path first (websocket) to validate flow.
3. Convert timer callbacks to enqueue-only stubs.
4. Move direct runtime mutation out of callback contexts.
5. Remove obsolete mutex where no longer needed.

## Safety Invariants
- Any stop condition (`timeout`, `max hold`, `error`) must preempt firing.
- Solenoid off command must be idempotent and always available.
- Queue overflow must not prevent stop event processing.

## Acceptance Criteria
- Runtime state is mutated only by `control_task`.
- Mutex use for control state is eliminated or minimal/justified.
- Event ordering is explicit and unit-testable.
- No observed deadlocks or lock-timeouts under stress.

## Test Strategy
- Stress test event bursts (rapid DOWN/UP/PING).
- Simulate queue pressure and verify safety events still apply.
- Validate no regressions in hold timing behavior.

## Risks
- Queue pressure edge cases.
- Event ordering surprises during migration.

## Mitigation
- Introduce event priority classes or separate safety queue if needed.
- Add trace logging for event enqueue/dequeue during rollout.

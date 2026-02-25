# Context Summary

Last updated: 2026-02-25

## Current Session Snapshot
- Created the `memories/` directory for persistent project notes.
- Established three memory files:
  - `context_summary.md`
  - `project_functionality.md`
  - `project_health.md`

## Working Agreement
- This file will be updated periodically during future work sessions to summarize active context, decisions, and recent changes.
- Updates are best-effort within active chat turns (I cannot run background tasks between turns).

## 2026-02-25 Follow-up Snapshot
- Reviewed repository layout, CI workflow, helper scripts, and `firmware/main/main.c`.
- Current architecture: single `main.c` owning state machine, Wi-Fi/AP+STA, HTTP/WebSocket, SPIFFS serving, timer-driven solenoid control.
- CI currently runs lint + route/SPIFFS verification and builds firmware.

## 2026-02-25 Dead-Man Safety Update
- Implemented websocket hold-heartbeat dead-man behavior.
- Firmware now enforces a dedicated firing liveness timeout (`100 ms`) via `hold_liveness_timer`.
- Web UI now sends `HOLD` frames every `50 ms` while the FIRE button is depressed.
- If hold heartbeats stop (disconnect/tab loss/network stall), firing is forced off and state transitions to disconnected.

## 2026-02-25 Build Verification (ESP-IDF)
- Verified ESP-IDF activation from `../esp-idf-v5.5.2/export.sh`.
- Confirmed `idf.py --version` reports `ESP-IDF v5.5.2`.
- Ran full `idf.py build` successfully for firmware after dead-man changes.
- Note: sandbox-restricted build path failed due macOS `sysctl` permission in component manager; unrestricted run succeeded.

## 2026-02-25 Hardware Status Note
- Hardware flash/monitor attempts were paused because the ESP32 device is not currently connected.
- Added explicit README `Dependencies` section with ESP-IDF 5.5.2 install/activation breadcrumbs and troubleshooting for missing `idf.py`.

## 2026-02-25 Docs Consistency Update
- Added a matching `Dependencies` section to `INSTALL.md` with explicit ESP-IDF 5.5.2 install/activation troubleshooting breadcrumbs.
- README and INSTALL now both document the same recovery path for missing `idf.py`.

# Secure and Safe Defaults for Networked Actuation

## Summary
Replace insecure hardcoded configuration and ambiguous safety posture with explicit, safe-by-default settings and documented operational constraints.

## Why This Is Needed
Current defaults expose avoidable risk:
- AP credentials embedded in source.
- Credential handling/provisioning policy is minimal.
- Safety section in docs is incomplete.

This project controls hazardous hardware; both cybersecurity and operational safety must be first-class.

## Security Objectives
- Remove hardcoded secrets from source.
- Use configuration channels appropriate for firmware (`Kconfig`, build-time config, NVS provisioning).
- Constrain network control surface.
- Improve auditability of provisioning and runtime modes.

## Safety Objectives
- Ensure startup defaults to non-firing safe state.
- Document operator constraints and failure modes clearly.
- Add explicit lockout/arming model if required by intended use.

## Recommended Changes
1. Configuration management
- Move AP SSID/password defaults to config system.
- Require explicit user-set credential for non-development builds.
- Add build profile concept: `dev` vs `production`.

2. Provisioning hardening
- Validate SSID/pass inputs strictly.
- Limit provisioning attempts/rate if abuse risk is meaningful.
- Optionally expire open provisioning window after boot.

3. Control-plane hardening
- Add simple session token/challenge for websocket control (minimum viable auth).
- Optionally gate firing behind explicit arming command and timeout.

4. Documentation hardening
- Fill Safety section with concrete operational rules.
- Add warnings around unattended operation and required physical safeguards.

## Acceptance Criteria
- No sensitive credential literals in firmware source.
- Safety documentation includes concrete do/do-not constraints.
- Production build path enforces stronger defaults than development.
- Control endpoint has at least baseline auth/arming protection strategy documented and implemented (phase-based).

## Testing
- Build-time tests for config presence/constraints.
- Runtime tests for provisioning validation behavior.
- Manual security smoke tests for unauthenticated control attempts.

## Risks
- Added friction for quick local setup.
- Potential backward compatibility impact for current workflow.

## Mitigation
- Keep development profile easy with explicit opt-in insecure mode.
- Document migration path clearly in `INSTALL.md`.

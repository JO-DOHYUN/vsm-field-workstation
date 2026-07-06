# 2026-07-06 Passive Product Completion Target

## Decision

The current product direction is fixed as a 2-bus ACK-capable observe-only
Passive-Safe evidence workstation. VSM keeps the 2+1 process model:
`vsm-ui.exe`, `vsm-capture-core.exe`, and optional `vsm-debug-tap.exe`.

## Expected Gain

- Remove stale gateway/control/full-instrumented assumptions from current docs.
- Make ACK-observe distinct from host TX/control.
- Make capture finalization and debug tap behavior part of the product contract.
- Prevent future agents from treating one-bus passive mode or build success as
  vehicle-impact-free acceptance.

## Rollback Rule

Only rollback if a newer product constitution replaces the 2-bus Passive-Safe
2+1 architecture and updates VSM/CSM acceptance criteria together.

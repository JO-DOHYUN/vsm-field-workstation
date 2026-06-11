# 2026-05-12 VMS-CSM Contract-First Realignment

## Decision

Adopt `VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md` as the latest integration architecture decision, but merge it into the existing harness instead of replacing `AGENTS.md` or `BRIEF.md`.

## Expected Gain

- VMS live path becomes typed evidence only.
- Legacy 20-byte remains replay/import compatibility instead of live production transport.
- COM open, `CAPABILITY`, `CONTROL_ACK`, `CAN_TX_RAW`, and feedback are separated before large runtime changes.
- Shared protocol documents become available in the repository instead of only inside CSM handoff ZIPs.

## Rollback Rule

Rollback this document/harness alignment if Codex starts missing current work targets, if protocol docs diverge from VMS/CSM constants, or if the document routing causes routine build/replay/graph tasks to load too much context.

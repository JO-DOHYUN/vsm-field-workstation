---
name: typed-evidence
description: Use when working on board typed stream parsing, typed capture storage, typed replay parity, CAN RX/TX audit, voltage raw samples, board health/events, capability records, or control safety gates. Do not use for legacy-only replay or UI styling.
---

# typed-evidence

## Invariants
- Legacy 20-byte packet path remains compatibility mode.
- Production truth is the typed board stream original bytes.
- Passive Product VSM must be a read-only typed evidence consumer: no serial write, no RTS touch, no host TX/control, no COM-owning gateway. DTR may be asserted only as a CSM-declared Arduino CDC session gate and must not enable downlink/control/reset behavior.
- Voltage raw, board health/event, CAN RX, CAN TX audit, and control ack remain separate evidence types.
- Voltage samples must not be represented as fake CAN frames.
- Live production stream is typed evidence only; legacy 20-byte remains replay/import compatibility.
- COM open is not board alive; valid `CAPABILITY` is required.
- Host-requested TX and `CONTROL_ACK` are not successful CAN TX until board emits matching `CAN_TX_RAW`.
- Lab control UI may exist for bring-up, but production success display stays gated by capability, health, safety, and CAN TX audit.
- Performance work must not change factual timing/value/alarm/DLC/control evidence.
- UI projection/drop/sampling may reduce displayed raw rows only; truth analysis must consume every accepted `CAN_RX_RAW` or report `truth_loss`.
- Live truth state keys must include bus identity, at minimum `bus + canId + ext + rtr`.
- Debug gateway capture is opt-in and external to the normal app hot loop; it can preserve raw serial evidence, but it does not replace VSM final capture validation.
- COM-owning debug gateway is Full/Instrumented lab-only and cannot be used as passive field acceptance.
- Passive Product diagnostics uses `vsm-debug-tap.exe`, which subscribes to Core IPC views without owning COM or altering typed evidence flow.
- `capture.stream/index` is the authoritative typed evidence truth; UI/raw tail/graph/analysis rows are bounded derived views.
- Hardware evidence fields in CSM `CAPABILITY` are claims/references only. `verified_passive` requires matching external analyzer/scope/DTC artifact verification.
- The product target is two-bus RX-only. A one-bus or missing-bus capability is a blocking mismatch diagnostic, not an accepted passive SKU.
- USB attach quarantine is CDC/uplink/session cleanup only. CAN front-end passive drain must continue.

## Workflow
1. Read `BRIEF.md`, `docs/architecture/PROJECT_CONSTITUTION_KO.md`, and `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`.
2. Identify record type and ownership: parser, storage, replay, analysis bridge, or control gate.
3. Preserve append-only raw capture before adding projections.
4. Add parity tests for record count, sequence, offsets, type counts, and first/last `mono_us`.
5. Keep legacy path tests green.

## Red Flags
- typed parser drops unknown records without diagnostic accounting
- partial file write becomes final capture
- control ack is treated as hardware TX confirmation
- board health degradation is hidden from operator summary
- timing/value/alarm/DLC/control evidence is computed from sampled or coalesced UI projection
- analysis input queue grows without a hard bound or overflow diagnostic
- display drop is reported as parser/storage/CSM CAN drop
- debug gateway raw capture is used to claim PASS when VSM `capture.stream` is missing, stale, or corrupt
- passive product profile allows host TX/control/gateway execution or non-session-only serial line control
- UI consumes raw typed evidence directly instead of querying Core materialized views
- capability hardware claims are treated as proof without external artifact verification
- bus-count mismatch diagnostics are removed because the product is two-bus only

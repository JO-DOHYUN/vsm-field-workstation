# Board + Qt Final Architecture Decision

Date: 2026-04-22

## Decision

The final product direction is **Evidence-first Control Workstation**.

Qt is not just a CAN viewer, and the board is not just a USB-CAN bridge. The system is a paired evidence and control platform:

- The board captures raw evidence from CAN, voltage, health, and later control/encoder lanes.
- Qt stores the original board stream as immutable truth.
- Decode, voltage scaling, graphs, operator summaries, and control views are derived sidecars.
- Direct control is allowed only after the safety/audit chain is implemented and verified.

The legacy 20-byte CAN packet remains a replay/import compatibility path. It is not the live production transport.

## Non-Negotiable Invariants

- Raw evidence is append-only and never overwritten by decoded values.
- VMS live production transport is typed evidence stream only.
- CAN RX, CAN TX audit, voltage raw, board health, board events, and control acknowledgements remain distinct record types.
- Voltage samples must never be disguised as CAN frames.
- A requested CAN transmit is not a successful transmit until the board emits `CAN_TX_RAW`.
- COM open is not board alive; valid `CAPABILITY` with compatible protocol/profile is required.
- Every record keeps source identity: bus, lane, source id, monotonic timestamp, sequence, and drop/fault counters where applicable.
- Replay must reconstruct the same ordered evidence stream before any derived analysis is trusted.
- Control must start in `MONITOR_ONLY` and must not auto-arm after reconnect.
- Graph truth, peak preservation, fixed-axis meaning, and replay/live separation remain unchanged.

## Final Board Architecture

The production board should be treated as a carrier-backed Portenta evidence controller.

Core lanes:

- `CanRxLane`: receives raw CAN from one or more physical buses and emits `CAN_RX_RAW`.
- `CanTxAuditLane`: emits `CAN_TX_RAW` only after a hardware CAN write succeeds.
- `VoltageRawLane`: emits `ADC_SAMPLE` with raw ADC counts and source/channel ids.
- `HealthMonitor`: emits `BOARD_HEALTH` and `BOARD_EVENT` with queue, drop, fault, and capability state.
- `UplinkScheduler`: frames all records into one ordered typed USB stream.
- `ControlLane`: validates host commands, safety state, lease, heartbeat, and target profile before attempting TX.
- `SafetySupervisor`: owns `MONITOR_ONLY`, `CONTROL_STANDBY`, `CONTROL_ARMED`, `FAULT`, and `ESTOP`.

Final hardware direction:

- Use isolated CAN transceivers and explicit termination options on the carrier board.
- Support simultaneous system and drive CAN observation through capability-described bus roles, not hardcoded UI assumptions.
- For drive-side control, the drive bus is the only bus allowed to arm TX by default.
- Keep CAN RX evidence alive during fault/estop when physically safe; block control TX instead.
- Use isolated/protected ADC front-end for production voltage evidence; Portenta ADC is lab fallback only.
- Keep hard safety gates outside firmware: estop input, CAN TX enable gate, watchdog toggle, field power ok.

## Typed Transport Contract

Qt must implement the board typed transport as a first-class runtime:

- SOF: `0xA5 0x5A`
- Header: `version u8`, `record_type u8`, `flags u8`, `seq u16_le`, `payload_len u16_le`
- Payload: record-specific little-endian bytes
- Trailer: `crc16_ccitt_le`
- CRC range: version through final payload byte, excluding SOF and trailer
- Recovery: scan SOF, validate length, validate CRC, dispatch or record loss

Priority records for Qt:

- `1 CAN_RX_RAW`
- `2 CAN_TX_RAW`
- `5 ADC_SAMPLE`
- `6 CONTROL_ACK`
- `7 BOARD_EVENT`
- `8 BOARD_HEALTH`
- `9 CAPABILITY`

Encoder direct records remain part of the contract but are not the first Qt implementation target. Wheel/encoder feedback should first be interpreted from VCU CAN messages.

## Qt Runtime Architecture

Qt runtime ownership is split as follows:

- `TransportRuntime`: serial read, typed live transport, SOF resync, CRC validation, typed dispatch.
- `StorageRuntime`: append exact framed bytes to `capture.stream.part`, write sparse index, finalize atomically.
- `ReplayRuntime`: auto-detect typed sessions and legacy 20-byte captures without mixing semantics.
- `AnalysisRuntime`: decode CAN DB/model values, VCU encoder feedback, voltage calibration, timing/value/alarm states.
- `EvidenceRuntime`: expose capability, health, drop/fault counters, CRC loss, bus roles, and audit mismatches.
- `ControlRuntime`: host intent, command id, lease, heartbeat, board accept/reject, actual TX audit, neutral fallback.
- `GraphRuntime`: render derived graph projections only; raw evidence remains owned by storage/replay.
- `OperatorRuntime`: summarize current state without mutating raw truth.

`AppController` remains the QML facade while these runtimes are extracted behind it.

## Storage Format

Minimum typed session set:

- `capture.stream.part`: original typed framed bytes, append-only.
- `capture.index.part`: sparse index with record offset, type, source, sequence, and `mono_us`.
- `session.meta.json`: board capability, firmware/build id, profile hashes, model pack, voltage calibration, start/stop wall time.
- `events.jsonl`: host-side operator notes, control intents, UI state changes, and export events.

Final names drop `.part` only after all buffers are flushed. Derived data may be cached, but cache loss must not invalidate the original stream.

Legacy session support:

- Existing 20-byte `.bin` replay/logging remains supported.
- Legacy 20-byte records must be labeled legacy in metadata and UI.
- Legacy captures must not be silently upgraded into typed sessions without preserving the original bytes.

## Control Audit Chain

Production control success display is disabled until the chain below is implemented and tested. Lab bring-up controls may exist if they remain visibly audit-gated:

1. Qt emits host intent with command id, target profile, lease id, and timestamp.
2. Board validates command, arm state, lease, heartbeat, health, bus role, and limits.
3. Board emits `CONTROL_ACK` as accept/reject with reason.
4. Board attempts actual CAN TX only if accepted and armed.
5. Board emits `CAN_TX_RAW` only after hardware write succeeds.
6. Qt correlates host intent, ack, actual TX, and feedback CAN.
7. Timeout, heartbeat loss, estop, reconnect, or health fault forces neutral/output-off policy.

Qt UI must show request, acceptance, actual TX, feedback, and safety/fault state separately.

## Implementation Phases

### Phase A: Contract Lock

- Done: legacy 20-byte parser and tests remain untouched.
- Done: typed transport parser and CRC16 tests were added.
- Done: typed record structs were added without connecting them to UI yet.
- Partial: capability/profile metadata can be decoded as typed payload; UI/runtime model binding remains future work.

Acceptance:

- Random garbage + valid frames resync correctly.
- CRC/length failures are counted and surfaced.
- Legacy tests remain green.

### Phase B: Typed Storage and Replay

- Done foundation: add `StorageRuntime` skeleton for `capture.stream.part`, `capture.index.part`, `session.meta.json`, and `events.jsonl`.
- Add typed replay reader that dispatches records in original order.
- Keep legacy `.bin` replay path separate.

Acceptance:

- Capture/replay parity holds for record count, sequence, offsets, type counts, and first/last `mono_us`.
- Corrupt stream replay reports exact fault location and continues only when resync is safe.

### Phase C: Evidence UI

- Add board capability/health/event panel.
- Display source roles: system CAN, drive CAN, TX audit, ADC source.
- Surface CRC loss, board drop, queue overflow, fault bits, and capability/profile mismatch.

Acceptance:

- Operator can tell whether data came from CAN RX, CAN TX audit, voltage ADC, or board health.
- No board-side error is hidden behind generic status text.

### Phase D: Analysis Integration

- Feed `CAN_RX_RAW` and `CAN_TX_RAW` into existing timing/value/alarm analysis with source identity preserved.
- Add voltage raw table and graph lane.
- Add voltage calibration profile as sidecar.

Acceptance:

- Voltage raw and calibrated voltage are visible as separate projections.
- Graphs can render CAN and voltage without converting voltage into fake CAN.
- Existing graph truth invariants still hold.

### Phase E: Safe Control

- Implement Qt command model, ControlRuntime, command journal, and board command transport.
- Implement board `ControlLane`, `CONTROL_ACK`, lease/heartbeat, arm/disarm, neutral fallback, and estop behavior.
- Only then expose operator control UI.

Acceptance:

- Reconnect never auto-arms.
- No heartbeat means neutral/output-off.
- A rejected command never appears as actual TX.
- A requested command is successful only when correlated with `CAN_TX_RAW`.

## Workstreams

- Board Architecture: carrier role, bus roles, safety gates, typed lane firmware.
- Qt Transport/Storage: parser, storage, replay, parity tests.
- Qt Analysis/Evidence: decode bridge, voltage scaling, health/capability UI.
- Control Safety: command protocol, ack, lease, heartbeat, arm/disarm, estop, audit correlation.
- Verification/Traceability: fixtures, HIL runbook, requirements matrix, release gates.

## Immediate Next Engineering Step

Do not expand VMS control runtime before the shared protocol and evidence contracts are locked.

The next code tranche should be:

1. Keep live production transport typed-only and move legacy 20-byte to replay/import compatibility.
2. Lock `CONTROL_ACK`, expanded `BOARD_HEALTH`, expanded `CAPABILITY`, and `HOST_CAN_TX_REQUEST` payload contracts.
3. Add capture/replay parity tests for record count, sequence, offsets, type counts, and first/last `mono_us`.
4. Add capability/health/event surfacing before production control success display.

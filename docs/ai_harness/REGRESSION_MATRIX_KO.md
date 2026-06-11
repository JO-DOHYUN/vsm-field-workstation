# REGRESSION_MATRIX_KO

이 문서는 VMS-CSM typed evidence 전환 중 회귀를 막기 위한 최소 검증 매트릭스다.
Protocol details are in [[../architecture/TYPED_STREAM_PROTOCOL_V1_KO]].

## Document Phase

- Obsidian links resolve to real markdown files.
- `BRIEF.md` does not preserve stale live legacy guidance.
- `AGENTS.md` stays short and routes to canonical docs.
- zip-internal documents are no longer the only source of truth.

## Protocol Phase

- VMS and CSM record IDs match `shared/protocol/typed_record_ids.h`.
- Typed frame SOF, CRC range, sequence, payload length, and endian rules match the docs.
- Unknown typed records are diagnosable.
- `CONTROL_ACK` is not documented or displayed as final success.

## VMS Live Phase

- COM open without `CAPABILITY` is not board alive.
- Live production stream is typed-only.
- Legacy 20-byte remains replay/import compatible.
- Serial parsing and blocking writes are not performed on the UI thread.

## Evidence Phase

- `CAN_RX_RAW`, `CAN_TX_RAW`, `ADC_SAMPLE`, `BOARD_HEALTH`, `BOARD_EVENT`, `CAPABILITY`, `CONTROL_ACK` remain separate.
- ADC/voltage/health/event are not converted to fake CAN frames.
- Health stale, drops, queue overflow, CRC failures, and seq gaps are visible.

## Control Phase

- Qt request, serial write, `CONTROL_ACK`, `CAN_TX_RAW`, and feedback are separate UI/model states.
- Missing `CAN_TX_RAW` prevents actual sent/success display.
- Missing feedback after `CAN_TX_RAW` is shown as sent but feedback not observed.
- Fault, estop, heartbeat timeout, or stale health blocks production control success.

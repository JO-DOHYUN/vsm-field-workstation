# VMS_ARCHITECTURE_KO

이 문서는 VMS Qt 앱의 typed evidence 중심 런타임 분리 목표다.
제품 원칙은 [[PROJECT_CONSTITUTION_KO]], 프로토콜은 [[TYPED_STREAM_PROTOCOL_V1_KO]], 제어 증거 의미는 [[CONTROL_EVIDENCE_CONTRACT_KO]]를 따른다.

## Current Baseline

현재 VMS에는 `TypedTransportParser`, `TypedRecords`, `StorageRuntime`, `ReplayRuntime`, `EvidenceRuntime`, `ControlAuditModel`, `TypedReplayReader`, `SerialWorker`, `TransportRuntime`, `TypedIngressRuntime`, `LegacyIngressRuntime`, `HostTxRuntime`, `ControlCycleRuntime`, `ControlCommandEncoder`, `ControlPage` foundation이 존재한다.
`AppController`는 QML facade와 orchestration을 유지하지만, 저장/replay 경로, board alive/control-capable state, control audit row/stage/counter state, `SerialWorker` thread lifecycle, queued live serial/log/control 호출, transport diagnostics는 런타임/model로 이동했다. `SerialWorker` 내부의 parser/logging/host-TX/control-cycle 상태도 runtime으로 분리했다. 아직 graph/control workflow 일부 UI 상태가 남아 있다.

## Target Runtime Boundaries

- `TransportRuntime`: owns `SerialWorker` creation, worker-thread lifecycle, typed-only live mode enforcement, and queued serial/log/control operations.
- `SerialWorker`: COM open/read/write, timer orchestration, and signal bridging only.
- `TypedIngressRuntime`: owns typed parser/storage/status/progress batching.
- `LegacyIngressRuntime`: owns legacy 20B parser/recorder/progress compatibility path.
- `HostTxRuntime`: owns host TX FIFO/backpressure counters.
- `ControlCycleRuntime`: owns heartbeat/session renew, slew-limited command burst generation, and frame-gap pacing state.
- `TransportSession`: operator-facing live transport diagnostics for typed parser faults, host TX queue/backpressure counters, and live delay.
- `TypedTransportParser`: SOF resync, length/CRC validation, seq gap accounting.
- `TypedRecordDecoder`: record payload decode only.
- `EvidenceRuntime`: owns `BoardConnectionState` and exposes `SerialOpen`, `TypedDetected`, `BoardAlive`, `ControlCapable`, stale/fault state.
- `BoardHealthModel`: `BOARD_HEALTH` and `BOARD_EVENT` counters, drops, queue depth, bus health.
- `ControlIntentModel`: operator target, profile, rate limits, neutral/fault fallback command plan.
- `ControlAuditModel`: requested, serial written, accepted, sent audit, feedback, fault/block timeline.
- `StorageRuntime`: append-only typed stream storage and index.
- `ReplayRuntime`: typed session replay and legacy `.bin` compatibility replay without mixing meanings.
- `AppController`: QML facade and forwarding layer.

## Live Transport Rule

Live production transport is typed-only.
Legacy 20-byte parser stays for legacy replay/import compatibility and tests.
The live UI must not ask the operator to choose legacy versus typed stream.

## Threading Rule

Serial parsing, transport resync, storage append, and blocking or heavy IO work must not run on the UI thread.
VMS UI receives model updates through bounded batches or dirty-state notifications.

## Control Rule

Control command generation must move toward profiles and evidence correlation.
The current lab `ControlPage` can remain for bring-up, but production success display requires matching `CAN_TX_RAW`.

## Migration Order

1. Lock shared protocol and docs.
2. Add protocol parity tests and fixtures.
3. Make live transport typed-only.
4. Add `BoardConnectionState`/`EvidenceRuntime` and board health capability gates.
5. Add `ControlAuditModel`.
6. Split replay/storage and keep legacy `.bin` compatibility.
7. Expand `TransportRuntime` ownership. 완료: `SerialWorker` thread lifecycle and queued serial/log/control operations moved out of `AppController`.
8. `TransportSession` diagnostic split and `ControlRuntime` checklist/verdict are in place; `SerialWorker` residual parser/logging/host-TX/control-cycle ownership has been extracted.

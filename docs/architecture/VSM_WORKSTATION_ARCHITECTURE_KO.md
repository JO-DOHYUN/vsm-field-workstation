# VSM Workstation Architecture

이 문서는 [[../../history/changes/2026-05-18-vsm-architect-synthesis-codex-prompt]]의 설계 의도를 현재 기준으로 정리한 VSM canonical architecture note다.

## Product Bar

VSM은 기존 Qt CAN monitor가 아니라 CSM typed evidence stream을 truth로 삼는 evidence-first CAN control workstation이다.

항상 유지할 기준:

- live production stream은 typed evidence 전용이다.
- legacy 20-byte는 replay/import compatibility 경로로만 유지한다.
- COM open은 board alive가 아니다.
- board alive는 valid `CAPABILITY`와 fresh `BOARD_HEALTH`를 기준으로 판단한다.
- `CONTROL_ACK`는 command acceptance evidence일 뿐이다.
- 실제 CAN 송신 성공은 matching `CAN_TX_RAW` audit evidence만 기준으로 한다.
- ADC, health, event, control ack는 fake CAN frame으로 만들지 않는다.
- bus number는 role이 아니다. role은 capability, model rule, observed fingerprint, operator override로 resolution한다.

## Runtime Spine

```text
TransportRuntime
  -> EvidenceRuntime
  -> StorageRuntime
  -> ReplayRuntime
  -> AnalysisRuntime / GraphRuntime
  -> ControlRuntime
  -> QML facade
```

`AppController`는 이 spine을 QML에 노출하는 facade로 축소한다. Serial parsing, storage write, replay decode, graph rebuild, control safety state machine은 AppController 내부에 직접 누적하지 않는다.

저장 경로, replay load/cache, last-session cache, typed session path 결정은 `StorageRuntime`/`ReplayRuntime` 쪽으로 이동했다.
`SerialWorker` 생성, thread lifecycle, live serial/log/control queued 호출은 `TransportRuntime` 쪽으로 이동했다.
ControlPage는 ready/block, Qt write, `CONTROL_ACK`, `CAN_TX_RAW`, feedback, fault를 분리해 표시하며 실제 성공 표시는 `CAN_TX_RAW` authority만 기준으로 한다.
ControlPage와 control gate는 모델팩 `control_policy`를 읽어 target role, allowed roles, bus role fingerprints, rpm/steering limits를 표시하고 적용한다.
Typed replay는 session state, timeline, meta/index/events sidecars, type counts, seq gaps, partial/corrupt capture faults, CAN RX projection, operator verdict, DLC preservation verdict를 분리해 진단한다.
QML smoke는 graph checkbox-click scroll/color, wrapper toggle stability, ControlPage evidence authority, Replay typed diagnostic hooks, Live page field state hooks를 state-probe로 확인한다.

## Current Implementation Step

이번 단계에서 고정한 첫 기반:

- `TypedRecords`에 `CONTROL_ACK` payload decode를 추가했다.
- `BoardConnectionState`를 추가해 serial open, capability, health, protocol, alive, control capable을 분리했다.
- `EvidenceRuntime`이 `BoardConnectionState`를 소유하고 AppController는 QML facade로만 board alive/control-capable 상태를 노출한다.
- `BusRoleResolver`를 추가해 bus number hardcoding 없이 role resolution과 TX allow 판단을 시작했다.
- `ModelPack`이 optional `control_policy`를 파싱하고, `BusRoleResolver` model rules와 ControlPage policy checklist/gate에 연결한다.
- `TransportRuntime`이 `SerialWorker` 생성과 thread lifecycle을 소유하고, live serial/log/control 호출을 queued operation으로 처리한다.
- `TypedIngressRuntime`이 typed parser, typed storage append, status/progress batching을 소유한다.
- `LegacyIngressRuntime`이 legacy 20B parser, recorder append, legacy logging progress를 소유한다.
- `HostTxRuntime`이 host command TX FIFO/backpressure counter를 소유하고, `SerialWorker`는 실제 `QSerialPort::write()`만 수행한다.
- `ControlCycleRuntime`이 heartbeat/session renew, slew-limited control burst generation, frame-gap pacing state를 소유한다.
- `TransportSession`이 typed parser fault, host TX queue/backpressure, live delay를 operator-facing diagnostics로 묶고 LivePage에서 TRANSPORT row로 노출한다.
- `ControlAuditModel`을 추가해 host request, Qt write, `CONTROL_ACK`, matching `CAN_TX_RAW`, feedback, fault/block audit state를 AppController 밖으로 분리했다.
- AppController control arm/send path는 board evidence gate를 통과하지 못하면 막히도록 1차 연결했다.
- ControlPage는 operator ready/block summary, `CAN_TX_RAW` 확인 badge, fault/block badge, evidence stage authority role을 표시하고 QML state probe로 회귀를 막는다.
- TypedReplayReader는 stream뿐 아니라 session sidecar integrity를 읽고 timeline/meta/index/events/fault summary를 AppController replay diagnostics로 노출한다.
- ReplayPage는 typed operator verdict와 DLC preservation verdict를 상단에 보여 replay 길이/DLC 뭉개짐 의심을 즉시 확인하게 한다.
- Live/Replay/Control/Graph 주요 field UX 상태는 QML state probe로 최소 회귀 방지선을 가진다.

## Next Implementation Step

실차/HIL을 제외한 코드/기능 큰축은 Windows VSM Field RC 기준으로 닫힌 상태로 본다.
다음 단계의 우선순위는 새 기능 추가가 아니라 release closure다.

1. 최신 Release build로 portable Field RC를 재생성하고 manifest hash/package id를 갱신한다.
2. synthetic/recorded typed smoke로 bus0/bus1, DLC, replay diagnostics, graph selection stability, live transport diagnostics를 재확인한다.
3. fresh HIL/vehicle control-policy validation과 결과 아카이브는 field-only gate로 분리한다.
4. Android USB-OTG/wireless transport는 Windows RC 이후 별도 feasibility/implementation 축으로 진행한다.
5. 더 깊은 parser/transport 분리는 새 field reliability 이슈가 확인될 때만 별도 slice로 진행한다.

## Verification Baseline

현재 기록된 local acceptance:

- Current workspace Release configure/build 통과.
- 전체 `ctest` 22/22 통과.
- Release exe startup smoke 5초 통과.
- Latest portable Field RC `portable_field_rc_20260604` 생성 및 portable startup smoke 통과.
- CSM `portenta_h7_m7_mid_mcp2515_j4_dual_csm` PlatformIO build 통과.

관련 문서:

- [[docs/architecture/PROJECT_CONSTITUTION_KO]]
- [[docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO]]
- [[docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO]]
- [[shared/protocol/typed_stream_v1]]

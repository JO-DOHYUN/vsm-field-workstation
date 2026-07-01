# VSM Passive-Safe 2+1 Product Architecture

## Purpose

VSM의 field/product 기본 실행은 실차 CAN에 영향을 주지 않는 passive evidence
workstation이다. 기본 프로세스는 `vsm-ui.exe + vsm-capture-core.exe`이고,
debug가 필요할 때만 `vsm-debug-tap.exe`를 추가하는 2+1 구조다.

## Runtime Profiles

### Passive Product

기본 제품 모드다.

- Serial open: read-only.
- DTR: CSM `CAPABILITY`가 `usb_cdc_dtr_session_only=1`을 선언한 경우에만
  Arduino CDC session gate 목적으로 허용.
- RTS: no-touch.
- Host TX/control/control cycle/gateway TCP: disabled.
- Debug: `vsm-debug-tap.exe` non-owning Core IPC sidecar만 허용.
- Vehicle PASS: external analyzer/scope/DTC artifact 검증 전에는
  `verified_passive` 표시 금지.

### Full Instrumented

bench/lab 전용 모드다.

- Serial read/write, host TX, control, lab gateway가 가능하다.
- 실차 Passive Product acceptance에 사용할 수 없다.
- UI는 이 모드를 product PASS로 표시하면 안 된다.

### Bench ACK Test

Kvaser/PCAN 단독 송신 테스트용 lab profile이다. Passive monitor는 ACK하지
않으므로 Kvaser 단독 송신 상대가 될 수 없다. ACK/TX가 필요한 시험은 이 모드에서
명시적으로 수행한다.

## Process Responsibilities

### Capture Core Data Plane

`vsm-capture-core.exe`가 COM/USB를 단독 소유한다.

- typed byte drain
- typed parser validation
- `capture.stream/index` append-only storage
- diagnostics and lifecycle evidence
- bounded materialized view creation
- RuntimeProfile gate for host/control/lab-only paths

Core는 QML model, AppController UI state, graph model, raw UI table을 소유하지
않는다.

### View Query Plane

`vsm-ui.exe`는 CoreClient다.

- Core가 `ViewChanged` cheap notification을 보낸다.
- UI는 `GetView(view_name, since_seq, limit)`로 필요한 view만 query한다.
- UI는 raw typed stream, full `TypedRecordList`, storage `frameBytes`를 직접 받지
  않는다.
- AppController는 QML facade와 bounded model apply만 담당한다.

### Optional Debug Tap Plane

`vsm-debug-tap.exe`는 기본 OFF다.

- Core IPC view만 구독한다.
- COM/USB를 열지 않는다.
- host TX/control/Core mutation을 하지 않는다.
- slow tap은 debug drop counter만 증가시키고 Core capture를 막지 않는다.
- debug artifact는 production capture truth나 vehicle PASS의 대체물이 아니다.

## Truth And View

- Authoritative truth: `capture.stream` + `capture.index`.
- CAN frame truth: accepted typed evidence에서 복원한 CAN frame.
- Display view: `live_latest`, `decoded_can_tail`, `analysis_snapshot`,
  `graph_bucket`, `transport_summary`, `profile_status`.
- Hardware passive proof: external analyzer/scope/DTC artifacts.
- Debug evidence: tap artifact.

## Passive Gates

VSM은 다음 상태를 분리한다.

- `configured_passive`: capability상 TX/control/downlink가 없다.
- `runtime_passive`: runtime 중 CAN_TX_RAW, passive violation, TXREQ violation이 0.
- `hardware_evidence_claimed`: CSM capability가 evidence reference를 제공한다.
- `external_artifact_verified`: analyzer/scope/DTC artifact가 VSM에서 검증됐다.
- `verified_passive`: 위 조건이 모두 통과했다.

`verified_passive`는 capability claim만으로 true가 될 수 없다.

## Two-Bus Product Rule

제품은 2-bus RX-only passive monitor다. 1-bus product/acceptance는 없다. 그러나
1-bus 또는 missing-bus capability mismatch 경고는 반드시 유지한다. 이 경고를
제거하면 잘못된 firmware upload 또는 wiring 오류를 숨긴다.

## USB Attach Quarantine

`USB_ATTACH_QUARANTINE`은 CDC/uplink/session payload cleanup이다. CAN front-end
drain을 정지하거나 MCP/transceiver mode를 바꾸는 상태가 아니다.

## Forbidden Boundaries

- RuntimeProfile 없이 serial read/write open.
- Passive Product에서 RTS assert, host TX, control cycle, gateway TCP 실행.
- `TypedRecordList`를 UI/analysis/projection/capture 공용 live object로 fanout.
- AppController가 transport raw state 또는 queue ownership state를 조립.
- diagnostics payload가 runtime state owner가 됨.
- UI projection을 timing/value/alarm/CAN loss truth로 사용.
- storage `frameBytes`를 live view path로 전달.

## Completion Criteria

- UI 실행 시 child `vsm-capture-core.exe --profile passive_product`가 뜬다.
- Debug Tap ON 시 세 번째 `vsm-debug-tap.exe`가 뜨고 COM/USB를 소유하지 않는다.
- Transport detail은 `passive_safety_profile`, `passive_usb_lifecycle`,
  passive violation/TXREQ violation, hardware evidence gate를 표시한다.
- `scripts/check_vsm_boundary_rules.py --mode strict`가 forbidden boundary를
  재발 방지한다.
- Release build, targeted/full ctest, startup smoke가 통과한다.

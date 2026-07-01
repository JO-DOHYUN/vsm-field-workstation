# VSM Passive-Safe 2+1 Product Architecture

## Purpose

VSM의 field/product 기본 실행은 실차 CAN을 흔들지 않는 evidence workstation이다.
기본 프로세스는 `vsm-ui.exe + vsm-capture-core.exe`, debug가 필요할 때만
`vsm-debug-tap.exe`를 붙이는 2+1 구조다.

## Runtime Profiles

### Passive Product

- Serial open: RuntimeProfile을 통과한 Core 단독 open.
- DTR: CSM `CAPABILITY`가 `usb_cdc_dtr_session_only=1`을 선언한 경우에만
  Arduino CDC session gate 목적으로 허용.
- RTS: no-touch.
- Host TX/control/control cycle/gateway TCP: disabled.
- CAN behavior: CSM session 안정 후 ACK-capable observe-only. ACK는
  host-originated CAN TX가 아니다.
- Debug: `vsm-debug-tap.exe` non-owning Core IPC sidecar만 허용.
- Vehicle PASS: external analyzer/scope/DTC artifact 검증 전 `verified_passive`
  표시 금지.

### Pre-session Safe

USB 물리 연결, DTR 미assert, Core 미연결, session quarantine 중의 상태다.

- CSM은 CAN payload를 다음 session으로 replay하지 않는다.
- CSM은 host TX/control/downlink를 실행하지 않는다.
- CSM은 firmware가 제어 가능한 pin/mode를 safe receive 상태로 먼저 둔다.
- 이 상태의 no-ACK는 제품 TX 실패가 아니라 session-before-observe 상태다.

### Full Instrumented

bench/lab 전용 모드다. Serial read/write, host TX, control, lab gateway가 가능할
수 있지만 실차 Passive Product acceptance에 쓰면 실패다.

### Listen-only Diagnostic

no-ACK 물리 진단용 모드다. Kvaser/PCAN 단독 송신 counterpart가 아니며 제품 기본
모드가 아니다.

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

- Core는 `ViewChanged` cheap notification만 보낸다.
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
- debug artifact는 production capture truth나 vehicle PASS를 대체하지 않는다.

## Passive Gates

VSM은 다음 상태를 분리한다.

- `configured_observe`: capability상 two-bus RX, no host TX/control/downlink.
- `runtime_observe`: CAN_TX_RAW 0, unexpected MCP mode violation 0, TXREQ violation 0.
- `hardware_evidence_claimed`: CSM capability가 evidence reference를 제공한다.
- `external_artifact_verified`: analyzer/scope/DTC artifact가 VSM에서 검증된다.
- `verified_passive`: 위 조건과 hardware proof가 모두 통과한다.

`verified_passive`는 capability claim만으로 true가 될 수 없다.

## Two-Bus Product Rule

제품은 2-bus observe-only monitor다. 1-bus product/acceptance는 없다. 그러나
1-bus 또는 missing-bus capability mismatch 경고는 반드시 유지한다. 이 경고를
제거하면 잘못된 firmware upload 또는 wiring 오류를 숨긴다.

## USB Attach Quarantine

`USB_ATTACH_QUARANTINE` is CDC/uplink/session payload cleanup. Current CSM
passive firmware defers CAN front-end initialization through USB power-up and
trusts CAN_RX_SEGMENT evidence only after `CAN_FRONTEND_SESSION_READY`.

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
- Transport detail은 ACK-observe와 host TX/control disabled를 분리 표시한다.
- Transport detail은 `passive_usb_lifecycle`, unexpected mode/TXREQ violation,
  hardware evidence gate를 표시한다.
- `scripts/check_vsm_boundary_rules.py --mode strict`가 forbidden boundary 재발을
  차단한다.
- Release build, targeted/full ctest, startup smoke가 통과한다.

## Current Additive Contract: Deferred CAN Front-End Init

Current CSM Passive Product firmware defers MCP/built-in CAN initialization
through USB power-up. `USB_ATTACH_QUARANTINE` remains CDC/uplink/session cleanup,
but trusted CAN evidence now starts only after stable CDC/DTR session, stale
payload clear, `CAN_FRONTEND_PRESESSION_HOLD`, configured quiet window, CAN
front-end initialization success, `CAN_FRONTEND_SESSION_READY`, and ACK-observe
armed with host TX/control/downlink still disabled.

VSM passive diagnostics must show board events 29..38 as lifecycle evidence.

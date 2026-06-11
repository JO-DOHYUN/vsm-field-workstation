# VSM 03. 확장추론 / 최종 구상안

## 0. 문서 목적

이 문서는 HAMT2-platform VSM을 Codex가 바로 구현할 수 있도록 만들기 위한 최종 설계 기준이다. 현재 목표는 기존 Qt 화면을 부분 보수하는 것이 아니라, CSM typed evidence gateway와 결합되는 Windows Qt evidence-first CAN workstation의 뼈대를 확정하는 것이다.

VSM은 단순 CAN 모니터가 아니다. VSM은 다음 역할을 동시에 수행해야 한다.

- CSM typed stream 수신기
- 원본 evidence append-only recorder
- legacy 20-byte BIN 호환 replay 도구
- typed capture/replay parity 검증기
- CAN DB/model pack 기반 decode/diagnostic workstation
- period/value/alarm/graph 분석기
- CAN control intent generator
- board `CONTROL_ACK`/`CAN_TX_RAW` audit chain 검증기
- operator용 안전 제어 콘솔

---

## 1. 최종 제품 정의

### 1-1. 프로젝트 이름

- HAMT2-platform VSM evidence-first CAN control workstation

### 1-2. 한 문장 목적성

CSM typed evidence stream을 Windows Qt에서 원본 truth로 저장·재생·분석하고, capability/health/safety/audit chain이 모두 만족될 때만 CAN 제어를 허용하는 차량 검증용 워크스테이션을 만든다.

### 1-3. 최종 사용자 장면

사용자는 차량 CAN 개발/시험자다. 사용자는 VSM으로 다음을 수행한다.

1. CSM 보드를 USB CDC로 연결한다.
2. VSM은 COM open만으로 연결 성공을 판단하지 않고, `CAPABILITY`와 `BOARD_HEALTH` 수신 후 board alive를 확정한다.
3. VSM은 CAN RX, CAN TX audit, control ack, board health, board event, ADC sample을 서로 다른 evidence로 저장한다.
4. VSM은 model pack을 통해 차량별 CAN 값, 에러코드, period, ttl, value threshold를 해석한다.
5. VSM은 live와 replay에서 같은 ordered evidence stream을 기준으로 analysis를 재현한다.
6. VSM은 제어 전 `MONITOR_ONLY`로 시작하며, operator arm/host heartbeat/board safety/target bus role/allowed id whitelist가 모두 만족될 때만 TX intent를 만든다.
7. VSM은 `CONTROL_ACK`를 송신 성공으로 표시하지 않는다. 실제 성공은 board가 올린 `CAN_TX_RAW` audit으로만 표시한다.

---

## 2. 최상위 아키텍처 결정

### 2-1. 채택

- Evidence-first architecture
- Typed stream production truth
- Legacy 20-byte compatibility path
- Append-only raw storage
- Derived sidecar analysis
- C++ backend + QML thin UI
- Capability/health based board alive gate
- Control audit chain
- Model pack based decode/error-code/diagnostic interpretation
- Portable deploy first
- Build/test/replay/deploy smoke as acceptance gate

### 2-2. 보류

- WiX/MSI 정식 인스톨러
- Qt minor version upgrade
- 외부 CAN viewer 프로젝트 직접 이식
- QtSerialBus 중심 구조 전환
- Python 분석기를 본체 runtime으로 승격

### 2-3. 폐기

- Live production에서 legacy 20-byte만 계속 확장하는 구조
- Board health/ADC/control ack를 fake CAN frame으로 변환하는 구조
- `CONTROL_ACK accepted`를 실제 CAN 송신 성공으로 표시하는 구조
- COM open을 board alive로 보는 구조
- bus0/bus1 숫자를 System/Drive로 하드코딩하는 구조
- replay 시작 시 alarm/count를 0부터 새로 누적하는 구조
- UI thread에서 serial parsing, decode, graph, alarm까지 수행하는 구조
- raw log를 decode 결과로 덮어쓰는 구조

---

## 3. 시스템 경계

```text
[Vehicle CAN buses]
    <-> [CSM board: Portenta H7 + Mid Carrier + MCP2515/J4 CAN]
    <-> USB CDC typed stream
    <-> [VSM TransportRuntime]
    <-> [EvidenceRuntime + StorageRuntime]
    <-> [ReplayRuntime]
    <-> [AnalysisRuntime / ModelPackRuntime / GraphRuntime / AlarmRuntime]
    <-> [ControlRuntime]
    <-> [QML Operator UI]
```

VSM은 차량 CAN에 직접 접근하지 않는다. VSM의 원본 truth는 CSM board가 올린 typed stream이다. CSM이 올린 evidence를 훼손하지 않고 저장한 뒤, UI와 분석은 전부 projection으로 만든다.

---

## 4. 하드웨어/CSM 계약에서 VSM이 요구해야 할 사항

### 4-1. 현재 하드웨어 기준

- Board: Portenta H7 M7 + Portenta Mid Carrier ASX00055
- bus0: MCP2515/TJA1050 외부 모듈
- MCP2515: 5V, 8MHz crystal, Classic CAN 500kbps
- SPI/INT: level shifter 사용
- bus1: Mid Carrier J4 CAN1 onboard PHY
- CAN speed: Classic CAN 2.0 500kbps
- 최종 env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm`

### 4-2. VSM에서 절대 하드코딩하지 말 것

- bus0 = Drive 고정 금지
- bus1 = System 고정 금지
- 특정 terminal 연결 위치로 role 판단 금지

### 4-3. VSM role resolver

VSM은 bus role을 다음 순서로 결정한다.

1. CSM `CAPABILITY`의 physical bus descriptor
2. model pack의 `bus_role_rules`
3. live 관측 ID fingerprint
4. operator override
5. unresolved 상태 유지

role이 unresolved면 control TX는 금지하고 monitoring만 허용한다.

---

## 5. Typed transport contract

### 5-1. Frame header

```text
SOF0              u8  0xA5
SOF1              u8  0x5A
version           u8
record_type       u8
flags             u8
seq               u16_le
payload_len       u16_le
payload           bytes[payload_len]
crc16_ccitt_le    u16_le
```

CRC 범위는 `version`부터 payload 끝까지다. SOF와 CRC trailer는 제외한다.

### 5-2. Required record types

```text
1 CAN_RX_RAW
2 CAN_TX_RAW
5 ADC_SAMPLE
6 CONTROL_ACK
7 BOARD_EVENT
8 BOARD_HEALTH
9 CAPABILITY
```

### 5-3. Parser requirements

- byte stream parser여야 한다.
- `readyRead()` 단위가 frame 단위라고 가정하면 안 된다.
- SOF scan / length validation / CRC validation / seq gap detection / resync가 있어야 한다.
- CRC fail, length fail, unknown type, seq gap, overflow는 모두 counter로 남기고 UI에 표시한다.
- malformed frame은 raw stream 파일에는 남되, dispatch는 하지 않는다.

---

## 6. VSM runtime 구조

### 6-1. 최종 runtime 분리

```text
AppController
  - QML facade only
  - runtime orchestration
  - operator command routing

TransportRuntime
  - SerialWorker ownership
  - legacy/typed explicit mode
  - byte ring buffer
  - TypedTransportParser
  - PacketParser legacy path
  - CRC/resync/drop statistics

EvidenceRuntime
  - normalized evidence dispatch
  - source identity preservation
  - capability/health/event state
  - board alive state
  - bus role resolver

StorageRuntime
  - append-only typed framed bytes
  - sparse index
  - session meta
  - events jsonl
  - atomic finalize

ReplayRuntime
  - typed replay reader
  - legacy replay reader
  - original order dispatch
  - seek/play/pause/speed
  - replay parity validation

AnalysisRuntime
  - timing evaluator
  - signal decoder
  - model pack application
  - error-code decode
  - TTL/value/period state
  - alarm/period separation

GraphRuntime
  - recent-window graph
  - full-range graph
  - fixed-axis truth
  - downsample/cache
  - micro-zoom projection

ControlRuntime
  - control intent journal
  - safety lease
  - heartbeat
  - arm/disarm
  - command encoder
  - ack correlation
  - tx audit correlation
  - neutral/failsafe state

UiStateStore
  - stable QML models
  - table sort/filter state
  - selected replay interval
  - scroll stability state
```

### 6-2. AppController 축소 원칙

`AppController`는 현재 기능을 유지하되 장기적으로 QML facade로 축소한다. `AppController` 안에 serial parsing, storage write, replay decode, graph rebuild, control state machine이 직접 섞이면 안 된다.

---

## 7. Storage format

### 7-1. Typed session directory

```text
capture_YYYYMMDD_HHMMSS.typed/
  capture.stream        # finalized raw typed framed bytes
  capture.index         # sparse index
  session.meta.json     # board/app/model/session metadata
  events.jsonl          # operator/control/ui/session events
  derived/
    analysis.cache      # optional, rebuildable
    graph.cache         # optional, rebuildable
    alarm.cache         # optional, rebuildable
```

During recording:

```text
capture.stream.part
capture.index.part
session.meta.json.part
```

Finalize only after flush and metadata write success. If finalize fails, `.part` files remain recoverable.

### 7-2. `capture.index` minimum fields

```text
record_no
stream_offset
frame_len
record_type
seq
mono_us
source_bus
source_lane
flags
payload_len
crc_ok
```

### 7-3. `session.meta.json` minimum fields

```json
{
  "format": "hamt2.vsm.typed_session",
  "format_version": 1,
  "vsm_build": {},
  "csm_capability": {},
  "model_pack": {
    "path": "",
    "schema_version": "",
    "hash_sha256": ""
  },
  "time": {
    "start_wall": "",
    "stop_wall": "",
    "first_mono_us": 0,
    "last_mono_us": 0
  },
  "capture_stats": {},
  "operator_notes": []
}
```

### 7-4. Legacy compatibility

Legacy 20-byte `.bin` path remains separate. Legacy files must be labeled as legacy in UI and metadata. Legacy replay must not be silently upgraded into typed session unless original bytes are preserved.

---

## 8. Evidence model

### 8-1. Evidence classes

```text
CanRxEvidence
CanTxAuditEvidence
AdcSampleEvidence
BoardHealthEvidence
BoardEventEvidence
ControlAckEvidence
HostControlIntentEvidence
OperatorEventEvidence
```

### 8-2. Common fields

```text
session_id
record_no
record_type
seq
mono_us
source_kind
source_bus
source_lane
flags
raw_offset
raw_len
validity
```

### 8-3. Truth rule

Raw evidence는 불변이다. Decoded signal, alarm, graph point, diagnostic message는 모두 projection이다.

---

## 9. Model pack / rules architecture

### 9-1. 최종 역할

Model pack은 단순 signal decode JSON이 아니라 차량 진단 모델이다. 다음을 포함해야 한다.

- vehicle/model identity
- schema version
- bus role rules
- CAN ID definitions
- signal decode
- period/TTL expectations
- value thresholds
- error-code decode
- diagnostic severity
- control whitelist
- control frame definitions
- graph presets
- alarm display policy

### 9-2. 권장 schema outline

```json
{
  "schema_version": "vsm.modelpack.v2",
  "model_id": "HAMT2_R13",
  "display_name": "HAMT2 R13 merged System/Drive",
  "bus_roles": [
    {
      "role": "drive",
      "fingerprints": ["0x503", "0x510", "0x511", "0x512", "0x513"],
      "tx_allowed": true
    },
    {
      "role": "system",
      "fingerprints": [],
      "tx_allowed": false
    }
  ],
  "messages": [],
  "diagnostics": [],
  "control": {
    "default_mode": "monitor_only",
    "allowed_ids": [],
    "neutral_frames": []
  }
}
```

### 9-3. Error-code model

각 diagnostic은 다음 필드를 갖는다.

```json
{
  "id": "drive_controller_fault_code",
  "can_id": "0x520",
  "bus_role": "drive",
  "signal": "fault_code",
  "code_map": {
    "0x00": {"level": "ok", "text_ko": "정상"},
    "0x01": {"level": "warn", "text_ko": "경고"},
    "0x02": {"level": "error", "text_ko": "오류"}
  },
  "latch_policy": "until_clear_or_timeout",
  "clear_condition": "signal_zero_for_3_frames"
}
```

### 9-4. Alarm과 Period 분리

- Alarm 탭: 차량/제어/진단 의미의 active fault/warn 중심
- Period 탭: 주기/TTL/timing anomaly 전용
- Period anomaly를 Alarm 탭에 recovery event로 섞지 않는다.
- Alarm row는 ID/diagnostic key 기준 안정 정렬한다.
- cleared state는 같은 row에서 회색/비활성으로 표시한다.

---

## 10. Control architecture

### 10-1. Default state

VSM은 항상 `MONITOR_ONLY`로 시작한다. Reconnect 후 auto-arm은 금지한다.

### 10-2. Control state machine

```text
MONITOR_ONLY
  -> CONTROL_STANDBY        # board alive, model valid, role resolved
  -> CONTROL_ARM_REQUESTED  # operator manual arm
  -> CONTROL_ARMED          # board ack + heartbeat active + safety ok
  -> CONTROL_FAULT          # health fault, timeout, mismatch, estop
  -> MONITOR_ONLY           # manual reset/disarm/reconnect
```

### 10-3. TX command lifecycle

```text
HostControlIntent
  -> downlink frame to CSM
  -> CONTROL_ACK accepted/rejected
  -> board hardware CAN write
  -> CAN_TX_RAW audit
  -> optional feedback CAN_RX_RAW correlation
```

### 10-4. Success definition

- `CONTROL_ACK accepted`: 요청이 board 검증을 통과했다는 뜻
- `CAN_TX_RAW`: 실제 hardware write 성공 evidence
- VSM에서 “송신 성공” 표시는 `CAN_TX_RAW`가 있어야만 가능

### 10-5. Safety gate

Control command는 아래 조건이 모두 참이어야 생성된다.

```text
board_alive == true
capability.protocol_compatible == true
board_health.safety_state == OK or STANDBY
model_pack.valid == true
target_bus_role.resolved == true
target_bus_role.tx_allowed == true
operator_armed == true
host_heartbeat.active == true
command_id in model_pack.control.allowed_ids
payload within model_pack.control.limits
no_estop == true
no_recent_crc_or_seq_fault_above_threshold == true
```

### 10-6. Neutral/failsafe 정책

권장 정책:

- host heartbeat timeout: board는 neutral burst 후 TX disable 또는 neutral hold 상태
- VSM disconnect: reconnect 후 monitor only
- estop: RX evidence는 유지 가능하면 유지, control TX는 즉시 차단
- health fault: ControlRuntime은 armed 해제, operator에게 reason 표시
- neutral VCU frame은 model pack control section에 명시하고 코드 하드코딩을 최소화

현재 알려진 neutral 기준:

```text
0x510 / 0x512 / 0x511 / 0x513 = 40 00 00 00 00 00 00 00
```

---

## 11. UI architecture

### 11-1. UI 원칙

QML은 화면과 interaction만 담당한다. 대용량 table, filtering, replay, decode, graph build, alarm state machine은 C++ model/runtime에서 처리한다.

### 11-2. 필수 화면

```text
Live
Replay
Overview
Period
Value Decode
Alarm
Graph
Control
Board Health
Model Pack
Session/Export
```

### 11-3. Top transport bar

전역 상단 transport bar를 둔다.

- live/replay mode
- connected board identity
- capability state
- recording state
- replay time/slider
- play/pause/seek
- model pack name/hash
- control state

### 11-4. Board Health panel

반드시 표시:

- firmware version
- protocol version
- bus count
- physical bus descriptors
- bitrate
- MCP clock/error flags
- rx/tx/drop/fail counters
- usb queue depth
- host crc fail
- seq gap
- safety state
- last fault/event

### 11-5. Control UI

Control UI는 버튼만 있는 화면이면 안 된다. 다음을 분리 표시해야 한다.

- operator intent
- encoded CAN frames
- target bus role
- board ack
- actual CAN_TX_RAW audit
- feedback CAN_RX_RAW
- heartbeat
- lease
- neutral/failsafe status
- reject reason

---

## 12. Performance architecture

### 12-1. Thread policy

```text
UI thread
  - QML render
  - operator interaction
  - lightweight model notifications only

Serial thread
  - QSerialPort read
  - byte buffer append
  - parser feed

Storage thread
  - append stream/index
  - flush/finalize

Analysis worker
  - decode, period, alarm, graph preprocess

Replay worker
  - indexed read, seek, dispatch
```

### 12-2. Table policy

- UI에 전체 로그를 직접 바인딩하지 않는다.
- virtual window model 사용
- sort/filter는 C++ proxy/model에서 처리
- refresh로 scroll이 튀면 안 된다.
- alarm row identity는 stable key로 유지한다.

### 12-3. Graph policy

- recent-window graph와 full-range graph 분리
- fixed-axis truth 유지
- peak preservation downsample
- micro-zoom은 projection이며 원본 축/값을 변형하지 않는다.
- replay seek 후 graph rebuild는 selected interval만 우선, full-range는 cache 기반

---

## 13. Test / acceptance

### 13-1. Unit tests

필수:

- TypedTransportParser CRC/resync/length/seq tests
- TypedRecords payload decode tests
- StorageRuntime append/finalize/recover tests
- TypedReplayReader parity tests
- ModelPackValidator schema/error-code/control whitelist tests
- ControlCommandEncoder tests
- ControlRuntime state machine tests
- AlarmManager stable row/clear state tests
- TimingEvaluator period-only anomaly tests
- Graph downsample peak preservation tests

### 13-2. Component tests

- live typed ingest fixture -> storage -> replay parity
- legacy replay remains green
- board capability missing -> board not alive
- control ack without tx audit -> not success
- rejected command -> no success
- heartbeat timeout -> control disabled
- bus unresolved -> TX denied
- model pack changed -> derived cache invalidated

### 13-3. Manual acceptance

- COM open alone does not enable board alive
- capability/health appears within expected time
- recording creates `.typed` directory and finalizes correctly
- corrupt typed stream shows exact fault location
- replay seek is stable
- live/replay analysis parity holds
- control UI starts disabled
- reconnect never auto-arms
- deploy folder exe runs directly outside Visual Studio

---

## 14. Folder structure proposal

기존 구조를 유지하되 runtime을 분리한다.

```text
src/
  backend/
    app/
      AppController.*
    transport/
      SerialWorker.*
      TransportRuntime.*
      TypedTransportParser.*
      PacketParser.*
    evidence/
      EvidenceTypes.*
      EvidenceRuntime.*
      BoardStateStore.*
      BusRoleResolver.*
    storage/
      StorageRuntime.*
      TypedSessionWriter.*
      TypedSessionIndex.*
      TypedReplayReader.*
      LegacyReplayReader.*
    analysis/
      AnalysisRuntime.*
      SignalDecoder.*
      TimingEvaluator.*
      AlarmManager.*
      DiagnosticRuntime.*
    graph/
      GraphRuntime.*
      GraphViewportItem.*
      Downsample.*
    control/
      ControlRuntime.*
      ControlCommandEncoder.*
      ControlJournal.*
      SafetyGate.*
    model/
      ModelPack.*
      ModelPackValidator.*
      DiagnosticSchema.*
    ui/
      UiStateStore.*
      FrameListModel.*
      DetailListModel.*
      BoardHealthModel.*
      ControlAuditModel.*
      AlarmTableModel.*
qml/
  Main.qml
  components/
  pages/
    LivePage.qml
    ReplayPage.qml
    OverviewPage.qml
    PeriodPage.qml
    ValuePage.qml
    AlarmPage.qml
    GraphPage.qml
    ControlPage.qml
    BoardHealthPage.qml
    ModelPackPage.qml
```

Codex는 한 번에 전체 이동을 하지 말고 단계별로 thin wrapper를 만든 뒤 기존 클래스를 점진 이동해야 한다.

---

## 15. Codex 작업 순서

### Stage 1. Contract lock

목표: 기존 기능 유지 + typed contract 명확화

작업:

1. `docs/interfaces/HW_SW_DATA_CONTRACT_KO.md`를 VSM 기준으로 갱신한다.
2. `TypedRecords`에 모든 required record type field를 명시한다.
3. `TypedTransportParser` fault counters를 구조화한다.
4. `CAPABILITY`, `BOARD_HEALTH`, `CONTROL_ACK`, `CAN_TX_RAW` helper decode를 테스트한다.

완료 조건:

- 기존 tests green
- typed parser tests green
- legacy parser tests green

### Stage 2. Typed live ingest

목표: 실제 serial live path를 typed mode로 받을 수 있게 한다.

작업:

1. `TransportRuntime` 추가
2. `SerialWorker`를 byte source로 축소
3. legacy/typed explicit mode 추가
4. typed frame dispatch를 `EvidenceRuntime`으로 연결
5. CRC/seq/drop counters UI store에 노출

완료 조건:

- typed fixture live ingest test
- malformed stream resync test
- COM open without capability -> not board alive

### Stage 3. Typed storage/replay parity

목표: typed 원본 저장과 replay parity 확정

작업:

1. `TypedSessionWriter` 구현
2. sparse index 구현
3. typed replay reader 구현
4. parity report 추가
5. corrupt stream recovery report 추가

완료 조건:

- record count parity
- type count parity
- seq parity
- offset parity
- first/last mono_us parity

### Stage 4. Capability/health/evidence UI

목표: operator가 board와 stream 상태를 바로 볼 수 있게 한다.

작업:

1. BoardHealthPage 추가
2. capability model 추가
3. bus role resolver 표시
4. fault counters 표시
5. source identity 표시

완료 조건:

- CAN_RX_RAW/CAN_TX_RAW/ADC/HEALTH/ACK가 같은 row type으로 뭉개지지 않음
- board fault가 generic status로 숨지 않음

### Stage 5. Model pack v2 / diagnostics

목표: 차량별 error-code/value/period/rule 구조 확장

작업:

1. model pack schema v2 추가
2. v1 compatibility loader 유지
3. error-code map 추가
4. diagnostic severity/clear/latch policy 추가
5. bus role rules/control whitelist 추가
6. validator 강화

완료 조건:

- 기존 model pack load 유지
- invalid control whitelist reject
- duplicate/collision bus role warning
- diagnostic decode test

### Stage 6. Control audit chain

목표: 실차 제어 전 audit chain 완성

작업:

1. ControlRuntime 추가
2. SafetyGate 추가
3. HostControlIntent journal 추가
4. Control ACK correlation 추가
5. CAN_TX_RAW audit correlation 추가
6. heartbeat/lease/timeout 구현
7. monitor-only/reconnect policy 구현

완료 조건:

- ack only != success
- tx audit success correlation test
- timeout disables control
- reconnect monitor-only
- unresolved bus denies tx

### Stage 7. UI performance hardening

목표: 대용량 replay/live에서 UI 안정화

작업:

1. table virtualization/windowing
2. stable alarm row model
3. graph cache/downsample
4. replay seek cache
5. UI refresh throttling

완료 조건:

- seek 후 scroll/sort 유지
- alarm clear가 row reorder를 유발하지 않음
- graph peak preservation test

### Stage 8. Release gate

목표: Codex 완료 판단을 build/test/deploy smoke로 고정

작업:

1. build script 정리
2. ctest all green
3. deploy_release.bat smoke
4. clean deploy runbook
5. release checklist 갱신

완료 조건:

- Release build
- Debug build if practical
- ctest pass
- deployed exe direct launch
- required QML/plugins/data/model included

---

## 16. Codex에게 줄 최종 작업 프롬프트

```text
너는 HAMT2-platform VSM의 수석 구현 에이전트다.
목표는 기존 Qt/QML CAN monitor를 evidence-first CAN control workstation 구조로 재정렬하는 것이다.

반드시 지킬 것:
- legacy 20-byte BIN/replay 호환을 깨지 마라.
- typed stream은 production truth로 취급하라.
- CAN_RX_RAW, CAN_TX_RAW, ADC_SAMPLE, BOARD_HEALTH, BOARD_EVENT, CAPABILITY, CONTROL_ACK를 fake CAN으로 합치지 마라.
- COM open만으로 board alive로 보지 마라. CAPABILITY + BOARD_HEALTH가 필요하다.
- CONTROL_ACK accepted를 CAN 송신 성공으로 보지 마라. 실제 성공은 CAN_TX_RAW audit이다.
- control UI는 MONITOR_ONLY로 시작하고 reconnect 후 auto-arm 금지다.
- raw evidence는 append-only이며 decode/alarm/graph는 derived sidecar다.
- bus0/bus1 숫자를 System/Drive로 하드코딩하지 마라. capability + model pack + observed fingerprint + operator override로 role을 결정하라.
- UI thread에서 serial parsing, storage, replay analysis, graph rebuild를 직접 수행하지 마라.

우선순위:
1. Contract lock
2. Typed live ingest
3. Typed storage/replay parity
4. Capability/health/evidence UI
5. Model pack v2 diagnostics
6. Control audit chain
7. UI performance hardening
8. Build/test/deploy release gate

첫 작업 범위:
- TransportRuntime, EvidenceRuntime의 최소 skeleton을 추가하라.
- SerialWorker를 byte source로 다루고 typed parser dispatch를 연결하라.
- CAPABILITY/BOARD_HEALTH 수신 전에는 board alive가 false여야 한다.
- StorageRuntime typed session writer/replay parity test를 준비하라.
- 기존 tests와 legacy replay path는 반드시 green으로 유지하라.

완료 조건:
- CMake Release configure/build 성공
- ctest 성공
- typed parser/resync/CRC/seq tests 성공
- legacy packet/replay tests 성공
- deploy_release.bat 또는 기존 portable deploy smoke 성공
- 변경사항과 남은 위험을 BRIEF.md에 짧게 갱신
```

---

## 17. 사용자 확인 필요 항목

- 실제 실차 control UI를 어느 단계에서 개방할지
- estop/field power/watchdog 입력을 VSM UI에서 어떻게 표시할지
- neutral hold와 TX disable 중 기본 failsafe 정책을 무엇으로 할지
- model pack v2를 기존 v1 JSON과 병행할지, 변환 스크립트를 먼저 만들지
- CSM capability payload field를 VSM 요구에 맞게 얼마나 확장할지

---

## 18. 최종 결론

VSM의 다음 작업은 화면 기능 추가가 아니라 runtime spine 확정이다. 핵심 spine은 `TransportRuntime -> EvidenceRuntime -> StorageRuntime -> ReplayRuntime -> AnalysisRuntime -> ControlRuntime -> QML`이다. 이 spine이 고정되면 CSM 변경, model pack 확장, 진단기 역할, 제어 audit, replay truth를 모두 같은 구조 안에서 다룰 수 있다.

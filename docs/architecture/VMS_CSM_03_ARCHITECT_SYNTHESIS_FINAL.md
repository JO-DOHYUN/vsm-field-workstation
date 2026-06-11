# VMS-CSM 통합 CAN 모니터링·제어 시스템 최종 아키텍처 구상안

작성 기준일: 2026-05-12
대상 코드: `J_ArdP7_AM2_CSM.zip`, `turn81_full_buildfix2 (8).zip`, `CSM변경안.txt`, `development_planning_prompt_v2.zip`
용도: Codex에 투입할 최종 설계 기준 + 작업 프롬프트

현재 구현명 참고: 이 문서의 `ControlEvidenceModel` 설계 책임은 현재 코드에서 `ControlAuditModel`로 구현 중이며, `BoardConnectionState` 소유 facade는 `EvidenceRuntime`이다.

---

## A. 최종 목적성 한 문장

Portenta H7 기반 CSM 보드와 Qt/C++ 기반 VMS 프로그램을 단순 CAN 송수신 도구가 아니라, **명령 의도·보드 수락·실제 CAN 송신·외부 CAN 수신·전압/엔코더 원신호·보드 건강상태를 분리 증거로 남기는 산업형 CAN 모니터링·제어 시스템**으로 재구성한다.

---

## B. 사용자의 아이디어에서 반드시 유지할 핵심

1. **제어 성공과 typed evidence 구조는 둘 다 1순위다.**
   제어가 실제로 성공해야 하고, 동시에 그 성공 여부를 증거 기반으로 확인할 수 있어야 한다.

2. **Qt가 명령을 보냈다는 사실은 성공이 아니다.**
   성공 판단은 `CONTROL_ACK`가 아니라, 보드가 실제 CAN write 성공 후 발행한 `CAN_TX_RAW` audit을 기준으로 한다.

3. **보드는 USB-CAN 브릿지가 아니라 typed evidence + control gateway다.**
   CSM은 `CAN_RX_RAW`, `CAN_TX_RAW`, `ADC_SAMPLE`, `BOARD_EVENT`, `BOARD_HEALTH`, `CAPABILITY`, `CONTROL_ACK`를 typed frame으로 올리는 장치여야 한다.

4. **제어 명령은 즉발 raw write가 아니라 profile/trajectory 개념을 가져야 한다.**
   조향을 0도에서 90도로 즉시 보내는 식은 금지한다. 목표값, 상승률, 주기, 제한, neutral/fault fallback, 실제 송신 audit이 분리되어야 한다.

5. **송수신 불안정은 숨기지 말고 UI/로그에서 보이게 해야 한다.**
   CAN drop, FIFO overflow, CRC fail, host parser fail, MCP error, built-in CAN fail, safety state, queue depth는 `BOARD_HEALTH`/`BOARD_EVENT`로 올라와야 한다.

6. **ADC/전압/엔코더는 fake CAN frame으로 위장하지 않는다.**
   전압은 `ADC_SAMPLE`, 엔코더 원신호는 `ENC_EDGE_RAW`, 파생값은 `ENC_DERIVED`로 분리한다.

7. **AI 하네스는 단순 문서 뭉치가 아니라 개발 운용 구조다.**
   Codex가 읽어야 할 문서, 수정 범위, 빌드 명령, 테스트 명령, 금지사항, 회귀 방지 기준이 폴더별로 분리되어야 한다.

---

## C. 심층리서치에서 채택할 항목

### C-1. 채택

| 항목 | 채택 이유 |
|---|---|
| typed transport v1 고정 | CSM과 VMS 모두 이미 뼈대가 존재하고, 디버깅/로그/리플레이 기준으로 적합하다. |
| live path에서 legacy 20-byte 제거 | typed stream과 legacy stream을 섞으면 Qt parser와 진단 기준이 흔들린다. |
| `CAPABILITY` handshake | COM open만으로 연결 성공 판단하면 보드 종류/펌웨어/버스능력 불일치를 잡을 수 없다. |
| `BOARD_HEALTH` 주기 송신 | 수신 카운트만으로는 Qt/보드/CAN/MCP/배선 원인을 분리할 수 없다. |
| `CONTROL_ACK`와 `CAN_TX_RAW` 분리 | host command accepted와 actual CAN sent는 다른 사건이다. |
| host command parser + TX queue | Qt가 20ms 주기로 여러 제어 ID를 보낼 수 있으므로 blocking 즉시 송신 구조만으로는 위험하다. |
| safety state machine | control armed, timeout, fault, estop, neutral fallback은 제어 시스템의 필수 구조다. |
| evidence UI | Qt에서 desired/scheduled/accepted/sent/feedback을 같은 값처럼 보이면 사용자가 원인을 오판한다. |
| root AGENTS + CSM/VMS 하위 AGENTS + docs 구조 | 단일 대형 MD보다 Codex 운용 안정성이 높다. |

### C-2. 조건부 채택

| 항목 | 조건 |
|---|---|
| 엔코더 raw/derived lane | 실제 회로/핀/타이머가 확정된 범위에서만 구현한다. 직접 24V HTL을 GPIO에 넣는 설계는 금지한다. |
| Qt 제어 그래프 | 처음부터 복잡한 폐루프 그래프가 아니라 desired/profile/sent/audit/feedback 분리부터 구현한다. |
| board-local `mono64` | 이미 코드에 `mono64_us()`가 있으므로 유지하되 payload 문서와 Qt decode가 일치해야 한다. |
| built-in CAN error 상세화 | Arduino_CAN API가 노출하는 범위까지만 구현하고, 미노출 상태는 `unknown/not exposed`로 명시한다. |

---

## D. 웹 최신성 검증에서 반영할 항목

1. **Qt 기준**
   현재 프로젝트는 Qt 6.10.2/MSVC 2022 기준으로 안정화되어 있으므로 이번 작업에서는 Qt 6.11 업그레이드를 섞지 않는다. Qt 6.10.3/6.11은 존재하지만, 구조 개편과 프레임워크 업그레이드를 동시에 진행하면 원인 분리가 어려워진다.

2. **QSerialPort 기준**
   serial parser와 blocking/대량 ingest는 GUI thread에 두지 않는다. `SerialWorker`/transport layer가 UI와 분리되어야 한다.

3. **QML 기준**
   `qt_add_qml_module` 구조는 유지한다. QML 파일을 무작위 resource 방식으로 돌리지 말고 현재 CMake QML module 구조를 유지한다.

4. **Portenta H7 기준**
   `portenta_h7_m7`, Arduino framework, M7 기준은 유지한다. M4 분산은 이번 단계에서 보류한다.

5. **MCP2515 기준**
   MCP2515는 Classic CAN 2.0B이며 CAN FD가 아니다. 외부 MCP lane과 Portenta built-in CAN lane의 capability를 동일하게 취급하지 않는다.

6. **MCP2515 라이브러리 기준**
   GitHub HEAD 추적은 금지한다. 버전 또는 commit을 고정한다.

7. **AI 하네스 기준**
   AGENTS.md는 agent용 README 역할로 쓰고, 상세 설계는 docs로 분리한다. Claude/Copilot 호환이 필요하면 얇은 wrapper만 둔다.

---

## E. 보류하거나 폐기할 항목

### 보류

| 항목 | 이유 |
|---|---|
| Qt 6.11 업그레이드 | 최신이지만 구조 개편과 동시에 진행하면 빌드/배포/QML 문제 원인 분리가 어렵다. |
| M4 코어 분산 | 지금은 M7 단일 펌웨어 안정화가 우선이다. 듀얼코어는 디버깅 비용이 크다. |
| CAN FD frame/DLC>8 | 현재 MCP2515와 제어 ID는 Classic CAN 중심이다. |
| 완전한 closed-loop control | feedback ID/스케일/실제 기구 응답 모델 확정 전까지는 profiled open-loop + evidence feedback으로 둔다. |
| 대규모 UI 개편 | 먼저 데이터/상태/evidence 모델을 확정한 뒤 UI를 정리해야 한다. |

### 폐기

| 항목 | 이유 |
|---|---|
| 단일 거대 MD 하나로 운용 | Codex가 작업 범위와 금지사항을 혼동한다. |
| CSM/VMS 두 개 문서만 두는 구조 | 공통 protocol/control 계약이 중복되거나 서로 어긋난다. |
| zip 안에 문서 넣고 “참고하라” | agent가 안정적으로 자동 참조하기 어렵다. 문서는 프로젝트 폴더에 풀려 있어야 한다. |
| live legacy 20-byte 유지 | typed live와 충돌한다. replay compatibility only로 격하한다. |
| Qt write 성공 = CAN 성공 | 실제 bus 송신 실패를 숨긴다. |
| ADC/전압 fake CAN ID | 분석 truth를 오염시킨다. |
| MCP INT falling-edge 단발 gate | 기존 실패 경험이 있고, level hint + bounded polling 방식이 맞다. |
| `src/main.cpp`와 `AppController` 계속 비대화 | 유지보수와 테스트가 무너진다. |

---

## F. 이번 Codex 작업의 개발 범위

이번 Codex 작업은 “한 번에 모든 기능 완성”이 아니라 **구조를 깨끗하게 고정하고, 다음 구현이 실패하지 않도록 토대를 재배치하는 작업**으로 정의한다.

### 반드시 할 것

1. 문서 구조 정리
   - 루트 `AGENTS.md` 정비
   - `docs/architecture/` 생성/정비
   - `docs/ai_harness/` 생성/정비
   - `firmware_csm/AGENTS.md` 또는 현재 `board/AGENTS.md` 정비
   - `qt_vms/AGENTS.md` 또는 현재 `qt/AGENTS.md` 정비

2. 공통 계약 고정
   - typed transport v1 문서화
   - record type ID 고정
   - payload size/field endian/seq/crc/mono64 고정
   - host command와 board uplink frame의 동일/상이 부분 명시

3. CSM 구조 분리 시작
   - `src/main.cpp`에서 protocol encode/decode, records, health, capability, control, safety, can lane을 분리할 설계와 1차 코드 분리
   - 기존 빌드 env가 깨지지 않게 단계적으로 분리
   - live default는 typed stream 유지
   - host command parser와 `CONTROL_ACK`/`CAN_TX_RAW` 의미 정리

4. VMS 구조 반영 설계
   - 현재 `TypedTransportParser`, `TypedRecords`, `SerialWorker`는 유지
   - `AppController` 과밀 해소 계획 수립
   - `BoardConnectionState`, `BoardEvidenceModel`, `ControlEvidenceModel`, `BoardHealthModel` 분리 설계
   - UI에는 desired/accepted/sent/feedback을 분리 표시하도록 설계

5. AI 하네스 운영 기준
   - Codex가 작업 전 읽을 파일 목록
   - 수정 금지/주의 파일 목록
   - 빌드/테스트 명령
   - 실패 시 보고 형식
   - 회귀 방지 체크리스트

### 이번에 하지 말 것

- Qt 6.11 업그레이드
- Portenta M4 코어 사용
- CAN FD 지원 추가
- 실제 차량 제어 안전값 임의 확정
- feedback ID/스케일을 근거 없이 창작
- UI 전체 갈아엎기
- legacy replay 포맷 삭제
- 하드웨어 HIL 성공을 코드만 보고 주장하기

---

## G. 최종 권장 아키텍처

### G-1. 문서 아키텍처 최종판정

**최소 3파일 구조가 아니라, 계층형 문서 구조를 채택한다.**

단일 MD: 폐기
CSM/VMS 2개 MD: 폐기
공통 + CSM + VMS 3개 MD: 최소안으로는 가능하지만 부족
**권장안: root agent + shared contract + CSM/VMS 개별 + AI harness + acceptance matrix**

권장 구조:

```text
repo/
  AGENTS.md
  BRIEF.md
  docs/
    architecture/
      PROJECT_CONSTITUTION_KO.md
      TYPED_STREAM_PROTOCOL_V1_KO.md
      CONTROL_EVIDENCE_CONTRACT_KO.md
      CSM_ARCHITECTURE_KO.md
      VMS_ARCHITECTURE_KO.md
    ai_harness/
      CODEX_WORKFLOW_KO.md
      BUILD_VERIFY_POLICY_KO.md
      REGRESSION_MATRIX_KO.md
      FILE_CLEANUP_POLICY_KO.md
  firmware_csm/ 또는 board/
    AGENTS.md
    BRIEF.md
  qt_vms/ 또는 qt/
    AGENTS.md
    BRIEF.md
  shared/
    protocol/
      typed_stream_v1.md
      typed_record_ids.h
```

핵심은 다음이다.

- `AGENTS.md`: Codex가 항상 읽는 짧은 헌법. 빌드/테스트/금지사항 중심.
- `docs/architecture/*`: 사람이 읽고 Codex가 필요시 참조할 상세 설계.
- `shared/protocol/*`: CSM과 VMS가 반드시 같이 맞춰야 하는 binary contract.
- `board/AGENTS.md`, `qt/AGENTS.md`: 하위 폴더 작업 시 적용되는 로컬 지침.

### G-2. 런타임 아키텍처

```text
[Qt UI]
  └─ AppController facade
      ├─ SerialWorker / TransportSession
      ├─ TypedTransportParser
      ├─ TypedRecordDecoder
      ├─ BoardConnectionState
      ├─ BoardHealthModel
      ├─ ControlIntentModel
      ├─ ControlEvidenceModel
      ├─ Replay/Storage
      └─ Graph/Timing/Value/Alarm domain models

[USB CDC typed frame]
  SOF + version + record_type + flags + seq + len + payload + crc16

[CSM Firmware]
  ├─ HostDownlinkParser
  ├─ ControlLane
  ├─ SafetySupervisor
  ├─ CanTxAuditLane
  ├─ CanRxLane_MCP2515
  ├─ CanRxLane_Builtin
  ├─ AnalogSampleLane
  ├─ EncoderLane
  ├─ HealthMonitor
  ├─ CapabilityPublisher
  └─ UplinkScheduler
```

---

## H. 파일/폴더 구조

### H-1. 현재 구조를 완전 새 repo로 갈아엎지 말 것

현재 CSM zip에는 이미 `board/`, `qt/`, `shared/`, `src/`, `include/`, `.agents/skills/` 구조가 섞여 있다. Qt zip에는 `AGENTS.md`, `BRIEF.md`, `docs/`, `src/backend/`, `qml/`, `tests/`가 존재한다.

따라서 Codex에게 “새 구조로 전부 이동”을 시키면 빌드가 깨질 가능성이 높다. 1차 작업은 **문서상 canonical 구조를 고정하고, 실제 파일 이동은 최소화**한다.

### H-2. CSM 권장 구조

```text
board/
  AGENTS.md
  BRIEF.md
  docs/
    CSM_ARCHITECTURE_KO.md
    SAFETY_STATE_MACHINE_KO.md
    HIL_RUNBOOK_KO.md

include/
  BoardPins.h
  protocol/TypedFrame.h
  protocol/TypedRecords.h
  can/CanLane.h
  can/Mcp2515Lane.h
  can/BuiltinCanLane.h
  control/ControlLane.h
  safety/SafetySupervisor.h
  health/HealthMonitor.h
  adc/AnalogSampleLane.h
  encoder/EncoderLane.h

src/
  main.cpp
  protocol/TypedFrame.cpp
  protocol/TypedRecords.cpp
  can/Mcp2515Lane.cpp
  can/BuiltinCanLane.cpp
  control/ControlLane.cpp
  safety/SafetySupervisor.cpp
  health/HealthMonitor.cpp
  adc/AnalogSampleLane.cpp
  encoder/EncoderLane.cpp
  diag/*.cpp
```

1차 리팩토링에서는 `main.cpp`를 완전히 비우지 말고, setup/loop orchestration만 남기는 방향으로 간다.

### H-3. VMS 권장 구조

```text
src/backend/
  app/
    AppController.h/.cpp
    AppFacadeBindings.h/.cpp
  transport/
    SerialWorker.h/.cpp
    TypedTransportParser.h/.cpp
    TransportSession.h/.cpp
  protocol/
    TypedRecords.h/.cpp
    ControlCommandEncoder.h/.cpp
  domain/
    BoardConnectionState.h/.cpp
    BoardHealthModel.h/.cpp
    ControlIntentModel.h/.cpp
    ControlEvidenceModel.h/.cpp
    TimingEvaluator.h/.cpp
    SignalDecoder.h/.cpp
    AlarmManager.h/.cpp
  storage/
    Recorder.h/.cpp
    TypedReplayReader.h/.cpp
    SessionManager.h/.cpp
  ui_models/
    FrameListModel.h/.cpp
    StableMapListModel.h/.cpp
    DetailListModel.h/.cpp
```

현재 `AppController.h`가 1000줄을 넘으므로, 새 기능을 여기에 계속 추가하지 말고 facade 역할로 축소하는 것이 목표다.

---

## I. 모듈별 책임

### I-1. CSM

| 모듈 | 담당 | 담당하지 말 것 |
|---|---|---|
| `TypedFrame` | SOF, version, type, flags, seq, len, crc encode/decode | CAN/ADC 의미 해석 |
| `TypedRecords` | payload struct, endian write/read, record ID | transport buffering |
| `HostDownlinkParser` | Qt→보드 command frame 수신, CRC/length 검증 | 실제 CAN write |
| `ControlLane` | command validation, allowlist, tx queue, ack 발행 | UI 판단, 차량 모델 해석 |
| `CanTxAuditLane` | 실제 CAN write 성공 후 `CAN_TX_RAW` 발행 | host intent를 성공으로 간주 |
| `CanRxLane` | MCP/built-in CAN RX raw capture | signal decode |
| `AnalogSampleLane` | ADC raw sample 발행 | 전압 스케일/단위 최종해석 |
| `EncoderLane` | edge raw/derived 발행 | 차량 제어 판단 |
| `SafetySupervisor` | estop/fault/timeout/neutral policy | Qt UI 표시 |
| `HealthMonitor` | counters, queue depth, fault bits, status snapshot | 장애 숨김 |
| `CapabilityPublisher` | firmware/protocol/bus/channel 능력 발행 | 연결 성공 판단 자체 |

### I-2. VMS

| 모듈 | 담당 | 담당하지 말 것 |
|---|---|---|
| `SerialWorker` | COM open/read/write, worker thread, reconnect | UI state 직접 수정 |
| `TypedTransportParser` | frame resync, CRC, length, seq gap counter | record payload 의미 해석 전체 |
| `TypedRecords` | record decode | 제어 정책 결정 |
| `BoardConnectionState` | CAPABILITY 기반 connected/typed alive 판단 | COM open만으로 alive 처리 |
| `BoardHealthModel` | BOARD_HEALTH/BOARD_EVENT 누적 표시 | CAN signal decode |
| `ControlIntentModel` | 사용자가 원하는 목표값/profile 생성 | 실제 송신 성공 표시 |
| `ControlEvidenceModel` | CONTROL_ACK/CAN_TX_RAW/feedback timeline 표시 | host write를 성공으로 위장 |
| `ControlCommandEncoder` | host command binary frame 생성 | 차량 안전 정책 독단 확정 |
| `Recorder/Replay` | typed stream 저장/재생 | legacy와 typed의 의미 혼합 |
| QML ControlPage | desired/scheduled/accepted/sent/feedback 표시 | raw 값만 던지고 성공처럼 보이기 |

---

## J. 데이터 흐름 / 제어 흐름

### J-1. Live 수신 흐름

```text
CAN bus / ADC / encoder / health
  -> CSM lane별 raw capture
  -> typed record emit
  -> USB CDC uplink
  -> Qt SerialWorker
  -> TypedTransportParser
  -> TypedRecords decode
  -> domain model update
  -> UI + storage
```

### J-2. 제어 송신 흐름

```text
사용자 조작
  -> Qt ControlIntentModel
  -> profile/trajectory 생성
  -> ControlCommandEncoder
  -> HOST_CAN_TX_REQUEST typed frame
  -> USB CDC downlink
  -> CSM HostDownlinkParser
  -> ControlLane validation
  -> CONTROL_ACK accepted/rejected
  -> TX queue
  -> CAN write attempt
  -> 성공 시 CAN_TX_RAW
  -> 실패 시 BOARD_EVENT + BOARD_HEALTH counter
  -> Qt ControlEvidenceModel timeline 반영
```

### J-3. 상태 판단

```text
COM open = physical serial open only
CAPABILITY received = typed board candidate
CAPABILITY valid + protocol match = typed board alive
BOARD_HEALTH recent = board health current
CONTROL_ACK accepted = board accepted request
CAN_TX_RAW matching command/can_id = actual CAN tx evidence
feedback CAN RX matching ID = external device reaction evidence
```

### J-4. 로그/리플레이 흐름

- live typed stream은 raw record 그대로 저장한다.
- replay는 저장된 typed record의 원래 시간축을 재현한다.
- legacy 20-byte `.bin`은 과거 호환용으로만 읽는다.
- typed stream과 legacy stream을 같은 live parser에 섞지 않는다.

---

## K. 핵심 함수 / 클래스 / 인터페이스 설계

### K-1. CSM 핵심 인터페이스 초안

```cpp
struct TypedFrameHeader {
    uint8_t version;
    uint8_t recordType;
    uint8_t flags;
    uint16_t seq;
    uint16_t payloadLen;
};

class TypedFrameWriter {
public:
    bool emit(uint8_t recordType, const uint8_t* payload, uint16_t len, uint8_t flags = 0);
    uint16_t nextSeq() const;
};

class HostDownlinkParser {
public:
    void ingest(uint8_t byte);
    bool takeCommand(HostCommand& out);
    HostParserCounters counters() const;
};

class ControlLane {
public:
    ControlAck validateAndQueue(const HostCanTxRequest& request);
    void serviceTx(int budget);
    ControlCounters counters() const;
};

class SafetySupervisor {
public:
    void update(uint32_t nowMs, const BoardInputs& inputs);
    bool controlAllowed() const;
    bool neutralRequired() const;
    SafetyState state() const;
};

class HealthMonitor {
public:
    BoardHealthSnapshot snapshot() const;
    void noteCanDrop();
    void noteHostCrcFail();
    void noteTxFail(uint8_t bus, uint8_t reason);
};
```

### K-2. VMS 핵심 인터페이스 초안

```cpp
class BoardConnectionState : public QObject {
    Q_OBJECT
public:
    void onPortOpened(QString portName);
    void onCapability(const TypedCapability& cap);
    void onBoardHealth(const TypedBoardHealth& health);
    void onTransportError(const TransportCounters& counters);
    bool typedAlive() const;
    QString statusText() const;
};

class ControlIntentModel : public QObject {
    Q_OBJECT
public:
    ControlProfile makeSteerProfile(double targetDeg, double maxDegPerSec);
    ControlProfile makeDriveProfile(double targetRpmOrPercent, double maxRate);
    QList<HostCanTxRequest> schedule(const ControlProfile& profile, quint64 nowMonoUs);
};

class ControlEvidenceModel : public QObject {
    Q_OBJECT
public:
    void onHostCommandQueued(const HostCanTxRequest& request);
    void onControlAck(const TypedControlAck& ack);
    void onCanTxRaw(const TypedCanRaw& tx);
    void onCanRxRaw(const TypedCanRaw& rx);
    QVariantList timeline() const;
};
```

### K-3. Record type 고정

```text
1  CAN_RX_RAW
2  CAN_TX_RAW
3  ENC_EDGE_RAW
4  ENC_DERIVED
5  ADC_SAMPLE
6  CONTROL_ACK
7  BOARD_EVENT
8  BOARD_HEALTH
9  CAPABILITY
10 HOST_CAN_TX_REQUEST
```

---

## L. 에러 코드 / 상태 코드 / 복구 전략

### L-1. ControlAck status

```text
0 REJECTED
1 ACCEPTED_QUEUED
2 ACCEPTED_WRITTEN       // 선택: CAN write 성공과 ACK를 분리할 경우
3 ACCEPTED_RATE_LIMITED  // 선택
```

현재 코드의 `status=1`은 “수락/송신시도 성공” 의미에 가까우므로, 최종 설계에서는 `CONTROL_ACK`가 실제 최종 성공이 아님을 문서와 UI에서 명확히 해야 한다.

### L-2. ControlAck reason

```text
0 OK
1 BAD_LENGTH
2 BAD_BUS
3 UNSUPPORTED_FRAME
4 DLC_OUT_OF_RANGE
5 ID_NOT_ALLOWED
6 CAN_NOT_READY
7 CAN_WRITE_FAILED
8 BAD_PROTOCOL
9 SAFETY_NOT_ARMED
10 HOST_TIMEOUT
11 RATE_LIMITED
12 QUEUE_FULL
13 ESTOP_ACTIVE
14 FAULT_ACTIVE
```

### L-3. SafetyState

```text
0 MONITOR_ONLY
1 CONTROL_STANDBY
2 CONTROL_ARMED
3 FAULT
4 ESTOP
5 NEUTRAL_HOLD
6 TX_DISABLED
```

### L-4. 복구 전략

| 상황 | 보드 동작 | Qt 표시 |
|---|---|---|
| host frame CRC fail | counter 증가, `BOARD_EVENT` 제한 발행 | transport warning |
| host command timeout | neutral burst 또는 TX disable | control timeout red |
| CAN write fail | `CONTROL_ACK` rejected 또는 event, txFail 증가 | requested but not sent |
| MCP RX overflow | health/event 발행 | capture unreliable |
| CAPABILITY 없음 | typed board로 인정하지 않음 | COM open only |
| BOARD_HEALTH 끊김 | stale/fault 표시 | board heartbeat lost |
| estop active | control reject, neutral/TX block | estop active |

---

## M. 사용자 편의성 디테일

1. **연결 상태 문구를 3단계로 분리**
   - Serial open
   - Typed board detected
   - Control capable

2. **제어 성공 표시를 4단계로 분리**
   - Requested
   - Accepted
   - Sent audit
   - Feedback observed

3. **ControlPage 그래프 분리**
   - 목표값 desired
   - profile scheduled
   - 보드 수락 accepted
   - 실제 송신 sent
   - 외부 feedback observed

4. **안전 버튼**
   - Arm
   - Neutral
   - Stop TX
   - Clear fault
   - Send test frame

5. **경고 문구**
   - “COM은 열렸지만 CAPABILITY 미수신”
   - “명령은 수락됐지만 CAN_TX_RAW audit 없음”
   - “CAN_TX_RAW는 있으나 feedback CAN 미관측”
   - “BOARD_HEALTH stale”

6. **로그 저장**
   - session folder에 `capture.stream`, `capture.index`, `session.meta.json`, `events.jsonl` 유지
   - 저장 중 중간 파일은 `.part` 사용
   - 종료 시 finalize

---

## N. 성능 / 안정성 설계

### N-1. CSM

- CAN RX는 ISR에서 heavy work 금지.
- MCP INT는 active-low level hint로만 사용.
- 실제 RX authority는 MCP status/register drain.
- CAN RX queue overflow는 반드시 counter/event로 노출.
- USB emit은 bounded budget으로 처리.
- `BOARD_HEALTH`는 100~500ms 권장, 현재 코드의 1000ms는 최종 목표보다 느릴 수 있다.
- host downlink parsing은 CRC/length/seq error를 health에 반영한다.
- control TX는 작은 queue를 두고 loop에서 순차 처리한다.

### N-2. VMS

- Serial parsing은 UI thread 금지.
- UI update는 dirty model 중심.
- graph는 fixed axis + pause/micro-zoom 분리.
- replay와 live의 카운터/상태를 섞지 않는다.
- AppController는 facade로 축소한다.
- typed parser counter는 UI/로그에 노출한다.

---

## O. 테스트 / 검증 전략

### O-1. CSM 빌드 테스트

```bash
pio run -e portenta_h7_m7_dual_can_basic
pio run -e portenta_h7_m7_mcp_int_main
pio run -e portenta_h7_m7_mcp_polling_recovery
pio run -e portenta_h7_m7_usb_diag
```

성공 기준:
- 모든 env 컴파일 성공
- GitHub HEAD 의존성 제거 또는 버전/commit 고정
- `src/main.cpp` 분리 후에도 기능 동일

### O-2. CSM HIL 테스트

1. 부팅 후 CAPABILITY 주기 송신 확인
2. BOARD_HEALTH 주기 송신 확인
3. MCP CAN RX `CAN_RX_RAW bus=0` 확인
4. built-in CAN RX/TX `bus=1` 확인
5. Qt에서 `HOST_CAN_TX_REQUEST` 송신
6. `CONTROL_ACK` 수신
7. 실제 `CAN_TX_RAW` audit 수신
8. timeout/estop/fault에서 neutral/TX block 확인

### O-3. VMS 빌드 테스트

```bash
cmake --preset vs-release-qt6
cmake --build --preset build-release
ctest --preset test-release
```

성공 기준:
- existing tests 통과
- typed parser tests 통과
- control command encoder tests 통과
- serial worker typed ingest tests 통과

### O-4. VMS 수동 확인

- CAPABILITY 없을 때 connected로 표시하지 않는지
- BOARD_HEALTH stale 표시가 되는지
- CONTROL_ACK와 CAN_TX_RAW가 UI에서 분리되는지
- live/replay 전환 시 카운터가 섞이지 않는지
- legacy replay가 삭제되지 않았는지

---

## P. 빌드 / 실행 / 디버깅 기준

### CSM

- 기본 빌드: PlatformIO
- board: `portenta_h7_m7`
- framework: `arduino`
- upload: DFU
- 현재 `platformio.ini`의 env는 유지
- `lib_deps`는 버전/commit 고정

### VMS

- Windows + MSVC 2022 x64
- Qt 6.10.2 baseline 유지
- `CAN_MONITOR_QT_PREFIX_PATH` 사용
- CMake preset 유지
- 배포는 `windeployqt` 유지

### 디버깅 로그

Codex 작업 후 보고에는 반드시 포함한다.

```text
- 수정한 파일 목록
- 생성한 파일 목록
- 삭제한 파일 목록
- 실행한 빌드 명령
- 실행한 테스트 명령
- 통과/실패 결과
- 실패 시 전체 에러 로그 요약
- 남은 리스크
```

---

## Q. 단계별 개발 순서

### Phase 0. 문서/하네스 정착

완료 조건:
- root/board/qt/shared 문서 구조 확정
- Codex 작업 기준 문서 생성
- 빌드/테스트 명령 명시

### Phase 1. CSM protocol 분리

완료 조건:
- `TypedFrame`, `TypedRecords` 분리
- record ID/payload 문서와 코드 일치
- 기존 env 빌드 통과

### Phase 2. CSM lane 분리

완료 조건:
- MCP lane, built-in CAN lane, ADC lane, health/capability 분리
- `main.cpp`는 setup/loop orchestration 중심
- 기존 CAN RX/TX/ADC behavior 유지

### Phase 3. Control + Safety 강화

완료 조건:
- host command queue
- allowlist
- timeout
- neutral/TX block policy
- CONTROL_ACK/CAN_TX_RAW 의미 분리

### Phase 4. VMS evidence model 분리

완료 조건:
- BoardConnectionState
- BoardHealthModel
- ControlEvidenceModel
- CAPABILITY 기반 연결 판단
- CONTROL_ACK/CAN_TX_RAW UI 분리

### Phase 5. 파일 정리

완료 조건:
- 불필요한 build artifact, 중복 reference zip, Obsidian 쓰레기 파일 정리 정책 수립
- 실제 삭제는 빌드/참조 여부 확인 후 진행

---

## R. 실패 가능성이 높은 지점과 예방책

| 실패 지점 | 예방책 |
|---|---|
| Codex가 `main.cpp`를 롤백하거나 새로 갈아엎음 | “기존 verified baseline 유지, behavior-preserving split only” 명시 |
| Qt가 CONTROL_ACK만 보고 성공 표시 | UI/도메인에서 `CAN_TX_RAW` 없으면 sent=false 고정 |
| CAPABILITY 없는데 연결 성공 표시 | `SerialOpen`, `TypedDetected`, `ControlCapable` 상태 분리 |
| MCP2515 INT edge 설계 복구 | AGENTS와 safety doc에 “falling-edge gate 금지” 명시 |
| ADC를 CAN ID로 포장 | shared protocol에 fake CAN 금지 명시 |
| Qt 6.11 업그레이드 섞임 | 이번 작업 명시적 금지 |
| 라이브러리 Git HEAD 추적 | `lib_deps` 버전/commit 고정 |
| AppController 더 비대화 | 새 model/class로 분리하고 AppController는 facade 유지 |
| 파일 삭제로 빌드 깨짐 | 삭제 전 reference check + 빌드 통과 후 삭제 |
| 차량 안전값 임의 확정 | unknown은 config/profile로 빼고 conservative default 적용 |

---

## S. Codex에게 넘길 최종 작업 프롬프트

아래를 그대로 Codex에 넣는다.

```text
너는 이 저장소의 CSM/VMS 통합 CAN 모니터링·제어 시스템을 재구성하는 수석 개발자다.

목표는 단순 기능 추가가 아니다. Portenta H7 기반 CSM 펌웨어와 Qt/C++ 기반 VMS 앱을 typed evidence + control gateway 구조로 고정하고, AI 하네스/문서/코드 아키텍처/폴더 구조를 장기 유지보수 가능한 형태로 정리하라.

반드시 먼저 읽을 파일:
1. AGENTS.md
2. BRIEF.md
3. board/AGENTS.md 또는 firmware_csm/AGENTS.md
4. qt/AGENTS.md 또는 qt_vms/AGENTS.md
5. shared/docs/TRANSPORT_AND_RECORDS_KO.md 또는 shared/protocol/typed_stream_v1.md
6. docs/architecture 관련 문서
7. platformio.ini
8. CMakeLists.txt, CMakePresets.json
9. CSM src/main.cpp
10. VMS TypedRecords, TypedTransportParser, SerialWorker, ControlCommandEncoder, AppController 관련 파일

프로젝트 목적:
- Qt가 명령을 보낸 사실을 성공으로 보지 않는다.
- 보드가 CONTROL_ACK를 보낸 것도 최종 성공이 아니다.
- 보드가 실제 CAN write 성공 후 발행한 CAN_TX_RAW audit만 actual sent evidence로 본다.
- CAN_RX_RAW, CAN_TX_RAW, ADC_SAMPLE, BOARD_EVENT, BOARD_HEALTH, CAPABILITY, CONTROL_ACK를 typed frame으로 분리한다.
- live path에서 legacy 20-byte stream을 섞지 않는다. legacy는 replay compatibility only다.
- ADC/전압/엔코더 원신호를 fake CAN frame으로 만들지 않는다.

이번 작업 범위:
1. 문서/AI 하네스 구조 정리
   - root AGENTS.md를 짧고 강한 헌법으로 정리한다.
   - docs/architecture/PROJECT_CONSTITUTION_KO.md 생성 또는 갱신.
   - docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md 생성 또는 갱신.
   - docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO.md 생성 또는 갱신.
   - docs/architecture/CSM_ARCHITECTURE_KO.md 생성 또는 갱신.
   - docs/architecture/VMS_ARCHITECTURE_KO.md 생성 또는 갱신.
   - docs/ai_harness/CODEX_WORKFLOW_KO.md 생성 또는 갱신.
   - docs/ai_harness/BUILD_VERIFY_POLICY_KO.md 생성 또는 갱신.
   - docs/ai_harness/REGRESSION_MATRIX_KO.md 생성 또는 갱신.

2. CSM 펌웨어 구조 정리
   - src/main.cpp를 무작정 갈아엎지 말고 behavior-preserving split을 우선한다.
   - protocol encode/decode를 protocol/TypedFrame.*로 분리한다.
   - record payload 정의를 protocol/TypedRecords.*로 분리한다.
   - Capability/BoardHealth/BoardEvent 발행 책임을 별도 모듈로 분리한다.
   - MCP2515 RX lane과 built-in CAN lane을 분리할 수 있는 구조를 만든다.
   - Host downlink parser, ControlLane, SafetySupervisor를 분리할 수 있는 구조를 만든다.
   - MCP2515 INT_N은 active-low level hint + bounded polling fallback으로 유지한다. falling-edge 단발 gate를 복구하지 마라.
   - lib_deps의 MCP2515 라이브러리는 GitHub HEAD 추적을 피하고 버전 또는 commit으로 고정하라.

3. VMS 구조 정리
   - 기존 TypedTransportParser, TypedRecords, SerialWorker, Recorder/TypedReplayReader 테스트를 유지한다.
   - AppController에 새 기능을 계속 누적하지 말고 BoardConnectionState, BoardHealthModel, ControlEvidenceModel, ControlIntentModel 분리를 설계/구현한다.
   - COM open만으로 connected로 보지 말고 CAPABILITY 수신과 protocol match를 typed board alive 기준으로 삼는다.
   - CONTROL_ACK와 CAN_TX_RAW를 UI/모델에서 분리한다.
   - requested, accepted, sent audit, feedback observed를 같은 상태로 합치지 마라.

4. 제어 철학
   - 0→90도 같은 step steering command를 기본으로 만들지 마라.
   - 목표값은 profile/trajectory로 변환되어야 한다.
   - max rate, timeout, neutral fallback, safety state를 구조에 포함하라.
   - 실제 차량 안전 스케일이 불확실하면 임의 확정하지 말고 config/profile의 conservative default로 둔다.

금지:
- Qt 6.11 업그레이드 금지.
- CAN FD/DLC>8 추가 금지.
- Portenta M4 분산 금지.
- legacy 20-byte live stream 부활 금지.
- ADC/전압 fake CAN frame 금지.
- CONTROL_ACK만 보고 성공 표시 금지.
- src/main.cpp와 AppController를 더 비대하게 만드는 방식 금지.
- 빌드 확인 없이 완료 주장 금지.

빌드/검증:
CSM:
- pio run -e portenta_h7_m7_dual_can_basic
- pio run -e portenta_h7_m7_mcp_int_main
- pio run -e portenta_h7_m7_mcp_polling_recovery

VMS:
- cmake --preset vs-release-qt6
- cmake --build --preset build-release
- ctest --preset test-release

작업 완료 보고 형식:
1. 적용 요약
2. 수정한 파일
3. 생성한 파일
4. 삭제한 파일
5. 유지한 기존 기능
6. 새로 구현/정리한 구조
7. 실행한 빌드/테스트 명령과 결과
8. 실패한 명령과 에러 요약
9. 남은 리스크
10. 다음 작업 제안
```

---

## T. 사용자가 Codex 작업 후 확인할 체크리스트

### T-1. 문서/폴더

- [ ] root `AGENTS.md`가 너무 길지 않고 핵심 금지사항을 포함하는가?
- [ ] CSM/VMS/shared 문서가 서로 중복보다 참조 구조로 연결되는가?
- [ ] typed protocol 문서와 코드 record ID가 일치하는가?
- [ ] Codex 작업 보고에 수정/생성/삭제 파일이 명확한가?

### T-2. CSM

- [ ] `pio run -e portenta_h7_m7_dual_can_basic` 성공?
- [ ] `pio run -e portenta_h7_m7_mcp_int_main` 성공?
- [ ] `pio run -e portenta_h7_m7_mcp_polling_recovery` 성공?
- [ ] 부팅 후 `CAPABILITY`가 나오는가?
- [ ] `BOARD_HEALTH`가 주기적으로 나오는가?
- [ ] MCP RX는 `CAN_RX_RAW bus=0`으로 보이는가?
- [ ] built-in CAN TX는 `CAN_TX_RAW bus=1`로 audit되는가?
- [ ] ADC는 `ADC_SAMPLE`로만 나오는가?
- [ ] fake CAN ID로 전압을 만들지 않았는가?
- [ ] MCP INT edge gate가 부활하지 않았는가?

### T-3. VMS

- [ ] `cmake --preset vs-release-qt6` 성공?
- [ ] `cmake --build --preset build-release` 성공?
- [ ] `ctest --preset test-release` 성공?
- [ ] COM open만으로 board alive가 되지 않는가?
- [ ] CAPABILITY 수신 후 typed board alive가 되는가?
- [ ] BOARD_HEALTH stale/정상 상태가 표시되는가?
- [ ] CONTROL_ACK와 CAN_TX_RAW가 분리 표시되는가?
- [ ] requested/accepted/sent/feedback이 구분되는가?
- [ ] legacy replay가 깨지지 않았는가?

### T-4. 실제 제어

- [ ] 0→90도 즉발 명령이 기본값으로 남아있지 않은가?
- [ ] 조향/구동 명령에 rate/profile 제한이 있는가?
- [ ] host timeout 시 neutral 또는 TX block 정책이 있는가?
- [ ] neutral payload가 문서화되어 있는가?
- [ ] control allowed bus가 capability/profile 기반으로 제한되는가?
- [ ] 실제 `CAN_TX_RAW` 없이는 UI가 성공 표시하지 않는가?

---

## 최종 결론

이 프로젝트의 최종 방향은 “Qt에서 CAN을 보내는 프로그램”이 아니다.
정답은 **증거 기반 제어 시스템**이다.

따라서 Codex 운용 문서도 하나의 거대 MD가 아니라, 아래처럼 역할을 나누는 것이 최선이다.

```text
루트 AGENTS.md = 짧은 헌법
shared protocol 문서 = CSM/VMS 공통 binary contract
CSM architecture 문서 = 보드 펌웨어 책임
VMS architecture 문서 = Qt 앱 책임
AI harness 문서 = Codex 작업/검증/회귀 방지 절차
acceptance matrix = 사용자가 직접 확인할 성공 기준
```

이 구조를 먼저 고정해야 이후 보드 코드 수정, Qt 코드 수정, 폴더 정리, 불필요 파일 삭제, 제어 안전성 강화가 같은 방향으로 진행된다.

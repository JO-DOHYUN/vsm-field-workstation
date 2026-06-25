# VSM Field Workstation 개선안 — Capture Core 분리 및 메모리 증가 차단 구조

## 0. 문서 목적

이 문서는 `JO-DOHYUN/vsm-field-workstation` 저장소의 현재 VSM 구조를 기준으로, 장시간 high-load 테스트에서 발생하는 메모리 증가와 UI 정체 문제를 근본적으로 줄이기 위한 개선 지시서다.

이번 작업의 목표는 단순 UI 최적화가 아니다. 목표는 **수신 데이터량에 비례해 메모리가 계속 증가하는 구조를 제거**하는 것이다.

현재 관찰된 실패 증상은 다음과 같다.

```text
- 앱 시작 직후 메모리 사용량: 1000MB 미만
- high-load 테스트 10분 미만에서 메모리 사용량: 3800MB 이상
- CPU 사용률: 약 20%
- 입력 중 화면 갱신이 점점 느려짐
- 입력이 끝난 뒤 한참 지나면 앱이 천천히 회복됨
- 로그 기록은 어느 정도 되는 것처럼 보이나, UI와 메모리 상태는 실패
```

이 증상은 단순한 그래프 렌더링 문제가 아니라 다음 중 하나 또는 복합 문제로 판단한다.

```text
1. 수신 hot path에서 TypedRecord/QByteArray 객체가 계속 생성되고 해제가 늦음
2. parser/storage/UI projection 경로가 분리되지 않아 backlog가 누적됨
3. Qt queued signal/event가 main thread 처리량보다 빠르게 쌓임
4. capture truth와 UI projection이 같은 프로세스/객체 소유권 안에 섞여 있음
5. AppController가 capture, analysis, graph, UI state를 과도하게 직접 보유함
```

---

## 1. 핵심 결론

### 1.1 Qt 자체를 버리는 것이 목표가 아니다

Qt/QML은 UI shell로 유지해도 된다.

버려야 하는 것은 다음 구조다.

```text
Qt GUI 앱 하나가
USB read
+ typed parser
+ capture writer
+ live analysis
+ graph history
+ UI model
+ replay state
를 모두 직접 소유하고 처리하는 구조
```

### 1.2 목표 구조

최종 목표는 다음과 같다.

```text
vsm-capture-core.exe
  - USB/Serial/WinUSB/PCAN/Kvaser 입력
  - typed parser
  - capture writer
  - drop/gap/seq counter
  - bounded memory queue
  - UI용 projection snapshot 생성

vsm-ui.exe
  - Qt/QML UI
  - 그래프/필터/버튼/상태 표시
  - capture-core에서 projection만 수신
  - full capture truth를 직접 보유하지 않음
```

단, 한 번에 2프로세스 구조로 갈 필요는 없다. 먼저 현재 코드 안에서 **capture hot path와 Qt UI path의 경계**를 만들고, 이후 별도 프로세스로 분리 가능한 구조로 리팩토링한다.

---

## 2. 용어 정의

### 2.1 Capture Truth

`capture truth`는 UI 표시용 데이터가 아니라, 실제 수신된 typed stream 또는 CAN frame record의 원본 저장 기준이다.

특징:

```text
- 손실되면 안 되는 기준 데이터
- 파일에 기록되어야 함
- UI가 느려져도 저장은 계속되어야 함
- UI projection drop과 구분되어야 함
```

### 2.2 UI Projection

`UI projection`은 화면 표시용으로 축약된 데이터다.

예:

```text
- 최근 N개 frame
- ID별 latest frame
- RX/TX fps
- drop/fault/gap counter
- graph용 downsampled point
- alarm summary
```

특징:

```text
- full truth가 아님
- high-load에서 sampling/drop 허용
- bounded memory여야 함
- UI가 멈춰도 capture truth를 막으면 안 됨
```

### 2.3 Bounded Queue

`bounded queue`는 최대 크기가 정해진 queue다.

금지:

```text
QVector/QList/QByteArray가 입력량에 비례해 계속 증가
Qt queued signal이 수신량만큼 무제한 누적
writer backlog가 무한 증가
```

허용:

```text
최대 N개
최대 N bytes
최대 N ms worth of data
초과 시 counter 증가
UI projection drop 허용
capture truth loss는 반드시 counter/gap으로 노출
```

### 2.4 Hot Path

`hot path`는 high-load 입력 중 매 frame 또는 매 byte마다 실행되는 경로다.

현재 hot path 후보:

```text
QSerialPort::readyRead
SerialWorker::processIncomingBytes
TypedTransportParser::takeOne
TypedIngressRuntime::ingest
StorageRuntime::appendTypedRecord
LiveProjectionRuntime
Qt queued signal emit
AppController live ingest
```

목표:

```text
hot path에서 malloc/QByteArray deep copy/QVariant/QObject signal 남발/QFile write를 제거하거나 최소화한다.
```

---

## 3. 현재 구조의 문제 정의

### 3.1 TypedRecord가 hot path 공용 객체로 너무 무겁다

현재 `TypedRecord`는 다음 구조를 가진다.

```cpp
struct TypedRecord {
    TypedFrameHeader header;
    QByteArray payload;
    QByteArray frameBytes;
};
```

문제:

```text
- payload와 frameBytes를 둘 다 보유함
- parser가 frame마다 payload를 복사하고 frame 전체도 복사함
- storage, projection, UI 경로가 같은 TypedRecord를 공유하려 함
- high-load에서 QByteArray 메모리 churn이 커짐
- batch에 담기면서 추가 보유 시간이 길어짐
```

개선 원칙:

```text
TypedRecordList를 hot path 공용 전달 객체로 쓰지 않는다.
```

### 3.2 Parser가 owning object를 계속 만든다

현재 parser는 완성 frame을 발견하면 `TypedRecord`를 생성한다.

문제:

```text
- payload QByteArray 생성
- frameBytes QByteArray 생성
- record batch에 보관
- storage에도 사용
- live projection에도 사용
```

개선 원칙:

```text
parser output은 owning object가 아니라 lightweight view/descriptor여야 한다.
```

예시:

```cpp
struct TypedFrameView {
    TypedFrameHeader header;
    const uint8_t* payload;
    uint16_t payloadLen;
    const uint8_t* frame;
    uint16_t frameLen;
};
```

또는 slab/ring 기반 reference:

```cpp
struct TypedFrameRef {
    uint32_t slabId;
    uint32_t frameOffset;
    uint16_t frameLen;
    uint16_t payloadOffset;
    uint16_t payloadLen;
    TypedFrameHeader header;
};
```

### 3.3 StorageRuntime이 read/parser 경로에서 동기 write를 수행한다

현재 storage write가 typed ingress 내부에서 수행된다.

문제:

```text
- parser/read worker가 QFile write latency에 영향받음
- record마다 stream write + index write가 발생함
- writer가 느려지면 read/parser/projection까지 함께 밀림
- 결과적으로 Qt/serial/event/batch backlog가 커질 수 있음
```

개선 원칙:

```text
parser/read path에서 QFile::write 직접 호출 금지.
CaptureWriterRuntime으로 분리.
```

### 3.4 UI와 Capture가 같은 프로세스에서 같은 상태를 공유한다

현재 `AppController`는 다음을 직접 보유한다.

```text
- live states
- replay states
- graph history
- graph bucket cache
- pending live frames
- UI models
- control/evidence state
- log state
- replay rebuild state
```

문제:

```text
- UI가 무거워지면 capture/analysis 관련 객체도 같은 프로세스 메모리 안에서 영향을 받음
- AppController가 점점 더 큰 상태 저장소가 됨
- 테스트 시간이 길어질수록 어떤 컨테이너가 커지는지 추적이 어려움
```

개선 원칙:

```text
AppController는 capture truth를 소유하지 않는다.
AppController는 bounded UI projection만 소유한다.
```

---

## 4. 목표 아키텍처

### 4.1 단기 목표: 단일 프로세스 내부 경계 분리

먼저 한 프로세스 안에서 다음 경계를 만든다.

```text
TransportReader
  ↓
IngressByteQueue / ByteSlabRing
  ↓
TypedParserRuntime
  ↓
CaptureWriterRuntime
  ↓
LiveProjectionRuntime
  ↓
AppController / QML UI
```

### 4.2 장기 목표: 2프로세스 구조

최종적으로는 다음 구조로 분리 가능해야 한다.

```text
[ vsm-capture-core.exe ]
  TransportReader
  TypedParserRuntime
  CaptureWriterRuntime
  ProjectionEngine
  DiagnosticsRuntime

        IPC

[ vsm-ui.exe ]
  Qt/QML UI
  AppController
  Graph Viewer
  Replay Viewer
  Control Panel
```

IPC 후보:

```text
1. Windows Named Pipe
2. Local TCP
3. Shared memory + event
4. gRPC는 초기 구조에서는 과함
```

---

## 5. 새 컴포넌트 정의

### 5.1 TransportReader

역할:

```text
- USB/Serial/WinUSB/PCAN/Kvaser 등 입력 backend에서 bytes 또는 CAN records를 수신
- 가능한 한 빠르게 byte queue에 넣고 return
- parser/storage/UI를 직접 호출하지 않음
```

금지:

```text
- readyRead에서 parser 직접 호출
- readyRead에서 QFile write
- readyRead에서 AppController signal emit
- 무제한 QByteArray append
```

필수 counter:

```text
transport_read_bytes_total
transport_read_calls_total
transport_read_zero_total
transport_read_error_total
ingress_queue_used_bytes
ingress_queue_max_bytes
ingress_queue_overrun_total
```

### 5.2 IngressByteQueue / ByteSlabRing

역할:

```text
- TransportReader가 읽은 raw bytes를 임시 보관
- parser가 소비
- fixed capacity
```

요구사항:

```text
- capacity compile-time 또는 config-time fixed
- used/max telemetry 제공
- overrun 시 counter 증가
- 무한 확장 금지
```

권장:

```text
- ring buffer
- fixed slab pool
- QByteArray 누적 append 최소화
```

### 5.3 TypedParserRuntime

역할:

```text
- ingress queue에서 bytes 소비
- SOF/length/version/CRC/typed_seq 검증
- frame boundary 확정
- output을 storage/projection용으로 분리
```

금지:

```text
- payload + frameBytes 이중 deep copy
- TypedRecordList 대량 생성
- QFile write
- UI signal emit
```

출력:

```text
1. StorageFrameRef
   - capture writer가 파일 저장에 필요한 frame reference

2. ProjectionEventLite
   - UI/live analysis가 필요한 최소 정보

3. Diagnostics counter
   - crc fail, length fail, seq gap 등
```

필수 counter:

```text
typed_frames_parsed_total
typed_bytes_parsed_total
typed_crc_fail_total
typed_length_fail_total
typed_seq_gap_total
typed_parser_buffered_bytes
typed_parser_buffered_max_bytes
```

### 5.4 CaptureWriterRuntime

역할:

```text
- capture truth를 파일에 저장
- parser/read path와 분리된 writer thread에서 동작
- stream/index batch write
```

입력 예시:

```cpp
struct StorageFrameRef {
    uint64_t seq;
    uint64_t monoUs;
    uint8_t recordType;
    uint16_t frameLen;
    // copied compact frame block or slab reference
};
```

요구사항:

```text
- bounded writer queue
- batch write
- stream/index write 분리 가능
- writer latency telemetry
- queue full 시 silent drop 금지
```

필수 counter:

```text
writer_queue_used
writer_queue_max
writer_backlog_bytes
writer_backlog_max_bytes
writer_records_written_total
writer_bytes_written_total
writer_index_entries_written_total
writer_batch_count_total
writer_batch_max_latency_ms
writer_overrun_total
writer_error_total
```

중요:

```text
writer queue full이 발생하면 capture truth loss 후보이므로 반드시 diagnostics에 드러내야 한다.
```

### 5.5 LiveProjectionRuntime

역할:

```text
- UI에 full frame stream을 보내지 않음
- ID별 latest, sampled recent, health summary, control evidence만 생성
- high-load에서는 표시용 drop 허용
```

projection 종류:

```text
CanRxLite
HealthLite
ControlLite
FaultLite
StatsLite
RecentFrameSample
```

예시:

```cpp
struct CanRxLite {
    uint64_t monoUs;
    uint32_t canId;
    uint8_t bus;
    uint8_t dlc;
    uint8_t data[8];
    uint64_t captureSeq;
};
```

요구사항:

```text
- 20~30Hz 이하로 UI 전달
- key별 latest coalescing
- bounded recent frame ring
- UI가 느리면 projection drop
- capture truth writer와 독립
```

필수 counter:

```text
projection_input_frames_total
projection_output_frames_total
projection_dropped_frames_total
projection_coalesced_frames_total
projection_flush_count_total
projection_max_backlog
projection_last_flush_ms
```

### 5.6 AppController

역할 변경:

```text
현재:
capture + storage + analysis + graph + UI state 통합 controller

목표:
UI projection client + command/control facade
```

AppController가 소유해도 되는 것:

```text
- bounded recent frames
- UI filter state
- selected graph keys
- UI-visible latest diagnostics
- control button state
- projection snapshot
```

AppController가 소유하면 안 되는 것:

```text
- full capture truth
- unbounded raw frame history
- unbounded TypedRecordList
- unbounded graph history
- parser backlog
- writer backlog
```

---

## 6. 구현 단계

### Phase 0 — 메모리 계측 추가

먼저 원인 추적 없이 구조를 또 바꾸지 말 것. 다음 telemetry를 UI와 로그에 추가한다.

```text
process_working_set_mb
process_private_bytes_mb
ingress_queue_used_bytes
parser_buffered_bytes
writer_queue_used
writer_backlog_bytes
typed_frames_parsed_total
typed_records_sent_to_ui_total
projection_dropped_total
pending_ui_projection_count
pending_live_frame_count
graph_live_point_count
graph_bucket_point_count
capture_bytes_written
capture_records_written
```

Acceptance:

```text
10분 high-load 테스트에서 어떤 counter가 메모리 증가와 같이 증가하는지 확인 가능해야 한다.
```

### Phase 1 — TypedRecordList 제거

작업:

```text
- TypedRecordList를 hot path 반환값으로 쓰지 않도록 변경
- TypedIngressRuntime::ingest()가 QVector<TypedRecordList>를 만들지 않게 변경
- parser 결과를 StorageFrameRef와 ProjectionEventLite로 분리
- UI path에는 TypedRecord 전체를 넘기지 않음
```

금지:

```text
batch.push_back(TypedRecord)
payload + frameBytes를 동시에 가진 객체를 여러 경로에 전달
```

Acceptance:

```text
typed frame 입력량이 증가해도 TypedRecordList 관련 메모리가 테스트 시간에 비례해 증가하지 않아야 한다.
```

### Phase 2 — CaptureWriterRuntime 분리

작업:

```text
- StorageRuntime::appendTypedRecord()를 read/parser path에서 제거
- CaptureWriterRuntime 추가
- writer thread 추가
- stream/index batch write 구현
- writer queue bounded
```

Acceptance:

```text
storage ON 상태에서도 parser/read path가 QFile write latency에 직접 막히지 않아야 한다.
writer backlog가 증가하면 UI에 표시되어야 한다.
writer backlog가 무한 증가하면 실패로 판단한다.
```

### Phase 3 — Single-flight UI projection

작업:

```text
- UI projection signal은 single-flight 구조로 변경
- 이미 pending UI update가 있으면 추가 signal을 쌓지 않음
- 20~30Hz 이하로 flush
- latest snapshot으로 덮어쓰기
```

금지:

```text
emit framesReceived(...)를 수신량만큼 호출
emit typedRecordsReceived(...)로 full record batch 전달
```

Acceptance:

```text
main thread가 느려져도 queued signal/event가 입력량에 비례해 계속 증가하지 않아야 한다.
```

### Phase 4 — Graph memory hard cap

작업:

```text
- live graph는 graph page active일 때만 기록
- per-series max point 설정
- bucket cache max level/max point 설정
- graph memory estimate counter 추가
```

Acceptance:

```text
graph page inactive에서 graph memory가 증가하면 실패.
graph active에서도 retention/cap 이상 증가하면 실패.
```

### Phase 5 — 2프로세스 경계 준비

작업:

```text
- capture core로 분리 가능한 클래스 경계 생성
- Qt UI에 종속되지 않는 CaptureCore module 생성
- AppController가 capture core 내부 queue에 직접 접근하지 않게 변경
- IPC DTO 정의
```

DTO 예시:

```cpp
struct CaptureStatusSnapshot {
    uint64_t rxFramesTotal;
    uint64_t bytesWritten;
    uint64_t parserSeqGaps;
    uint64_t writerOverruns;
    uint32_t ingressUsedBytes;
    uint32_t writerBacklogBytes;
};

struct UiProjectionSnapshot {
    QVector<CanRxLite> recentFrames;
    QVector<IdLatestState> latestById;
    CaptureStatusSnapshot status;
};
```

Acceptance:

```text
capture core가 Qt/QML 없이 unit test 가능해야 한다.
```

---

## 7. 검증 시나리오

### 7.1 필수 테스트 모드

다음 조건으로 각각 10분 이상 테스트한다.

```text
A. storage OFF
B. storage ON
C. live UI paused ON
D. graph page inactive
E. graph page active + selected 4 series
F. model/rules disabled
G. model/rules enabled
```

판단:

```text
storage OFF에서 메모리 증가가 멈추면 writer/storage 경로 주범
live UI paused에서도 증가하면 UI model보다 parser/storage/queue 주범
graph inactive에서도 증가하면 graph 주범 아님
model disabled에서 줄어들면 analysis/model decode 비용 주범
```

### 7.2 성공 기준

```text
1. 10분 high-load에서 메모리가 선형 증가하면 실패
2. 1시간 high-load에서 메모리가 plateau를 형성해야 함
3. 입력 중단 후 UI 회복이 오래 걸리면 실패
4. writer backlog가 무한 증가하면 실패
5. ingress queue가 무한 증가하면 실패
6. Qt queued projection이 수신량에 비례해 증가하면 실패
7. graph inactive 상태에서 graph memory가 증가하면 실패
8. capture truth loss는 반드시 counter/gap으로 드러나야 함
9. UI projection drop은 허용되지만 capture truth silent loss는 금지
```

---

## 8. 비목표

이번 작업에서 하지 말아야 할 것:

```text
- QML 색상/레이아웃 최적화만 수행
- graph paint만 수정하고 종료
- QSerialPort를 Win32로 바꾸는 것만으로 해결했다고 판단
- storage write를 그대로 둔 채 UI coalescing만 추가
- TypedRecordList 구조를 유지한 채 batch 크기만 조정
- 무제한 queue를 더 큰 queue로 바꾸기
```

QSerialPort vs Win32는 후순위다. 먼저 hot path 소유권과 queue bounded 구조를 고쳐야 한다.

---

## 9. 최종 지시

Codex는 다음 결론을 기준으로 작업하라.

```text
현재 실패 원인은 UI projection 부족만이 아니다.
핵심은 TypedRecord/QByteArray 중심 hot path, synchronous storage write, Qt queued event/backlog, AppController 과다 소유 구조다.

따라서 다음 패치는:
1. TypedRecordList 제거
2. parser output 경량화
3. CaptureWriterRuntime 분리
4. bounded queue
5. single-flight UI projection
6. memory telemetry
7. 2프로세스 capture-core 분리 가능 경계 생성

을 수행해야 한다.
```

최종 목표:

```text
VSM은 장시간 high-load에서 메모리가 테스트 시간에 비례해 증가하지 않아야 한다.
capture truth는 파일로 보존하고, UI는 bounded projection만 표시해야 한다.
Qt는 capture engine이 아니라 viewer shell이 되어야 한다.
```

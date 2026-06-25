# VSM Capture Core Memory Architecture

## 1. 목적
VSM live high-load에서 시간이 지날수록 메모리가 증가하고 UI가 멈추는 문제를 근본적으로 차단한다.

이 문서의 결론은 명확하다.

- 문제의 1차 축은 UI 렌더링만이 아니다.
- live hot path에서 `TypedRecord`, `QByteArray`, `TypedRecordList`, Qt queued event가 full batch를 반복 소유/복사하는 구조가 핵심 위험이다.
- 해결은 capture truth와 UI projection의 ownership을 분리하고, 모든 queue/pool을 bounded로 만드는 것이다.

## 2. 최종 목표 구조
초기 구현은 단일 프로세스 안에서 완성한다.
단, 구조는 나중에 `vsm-capture-core.exe`와 `vsm-ui.exe`로 분리 가능한 경계여야 한다.

```text
SerialDrainRuntime
  -> RawIngressQueue
  -> ByteSlabPool
  -> TypedFrameParserRuntime
  -> CaptureCoreRuntime
       -> CaptureWriterRuntime
       -> RawLedgerRuntime
       -> AnalysisWorkerRuntime
       -> ProjectionSnapshotRuntime
  -> AppController
  -> QML
```

`AppController`는 capture engine이 아니다.
`AppController`는 operator UI facade와 command/control facade만 담당한다.

## 3. 금지 구조
live production path에서 아래는 금지한다.

- parser가 frame마다 `payload`와 `frameBytes`를 각각 owning `QByteArray`로 생성.
- `TypedRecordList`를 pipeline thread에서 `SerialWorker`/`AppController`로 queued signal 전달.
- capture writer, raw ledger, analysis, projection이 같은 full record batch를 각자 복사.
- Qt event queue가 full typed batch의 숨은 backlog가 되는 구조.
- `AppController`가 full capture truth, parser backlog, writer backlog, raw history를 소유.
- UI projection drop을 parser/storage/CSM truth loss처럼 표시.

`TypedRecord`는 replay/offline/import 호환 타입으로 유지할 수 있다.
하지만 live production hot path의 주 전달 객체로 쓰지 않는다.

## 4. 핵심 타입
### TypedFrameRef
raw typed frame bytes를 직접 소유하지 않는 descriptor다.

```text
slab_id
frame_offset
frame_len
payload_offset
payload_len
record_type
flags
seq
mono_us_optional
```

### CanRxLite
analysis/raw ledger/live latest/graph의 CAN RX 기본 단위다.

```text
mono_us
capture_seq
bus
can_id
ext
rtr
dlc
data[8]
typed_seq
```

### CriticalEvidenceLite
UI/control에 필요한 최소 evidence DTO다.

대상:

- `BoardHealthLite`
- `BoardEventLite`
- `CapabilityLite`
- `ControlAckLite`
- `CanTxAuditLite`

필요 시 원본 frame은 capture offset으로 역참조한다.

### UiProjectionSnapshot
UI로 넘어가는 유일한 high-load display packet이다.

포함:

- latest CAN RX by key
- recent sampled tail
- board/csm/transport/capture/writer/parser/analysis status
- graph display status
- UI degradation counters

## 5. Queue And Ownership
모든 queue/pool은 fixed capacity를 가진다.

권장 기본값:

- raw ingress queue: 32MB
- slab pool: 64MB
- writer descriptor queue: 64MB equivalent
- raw ledger handoff: fixed record/byte cap
- analysis queue: fixed frame count cap
- projection snapshot: single-flight only

공통 counter:

```text
used
max_used
capacity
overrun
alloc_fail
push_count
pop_count
last_error
```

queue full은 silent drop이 아니다.

- capture truth queue full: capture invalid + fatal diagnostic.
- analysis queue full: `analysis_overrun` / `truth_loss`.
- UI projection full: projection dropped/coalesced display diagnostic.

## 6. Writer 기준
`CaptureWriterRuntime`은 `TypedFrameRef` descriptor를 받는다.

- `capture.stream`: slab raw bytes를 그대로 batch write.
- `capture.index`: descriptor metadata로 batch write.
- `events.jsonl`: lifecycle/fatal/overrun events.
- `capture.diagnostics.json`: parser/writer/slab/queue/process memory counter.

writer latency는 parser/drain을 직접 막지 않는다.
다만 writer queue가 cap에 닿으면 capture invalid 상태로 명확히 표시한다.

## 7. Analysis 기준
analysis는 `CanRxLite`만 받는다.

- timing/value/alarm/DLC/control evidence는 sampled UI projection에서 계산하지 않는다.
- 모든 accepted CAN_RX가 analysis에 들어가야 한다.
- 못 들어가면 `truth_loss` 또는 `analysis_overrun`으로 표시한다.

## 8. UI/Graph 기준
UI는 full typed stream consumer가 아니다.

- UI는 `UiProjectionSnapshot`만 받는다.
- snapshot은 20Hz 이하 single-flight로 전달한다.
- pending snapshot이 있으면 새 event를 쌓지 않고 latest state를 병합한다.
- live graph는 page active일 때만 기록한다.
- live graph는 per-series hard cap 또는 peak-preserving bucket ring을 사용한다.
- graph inactive 상태에서 graph memory가 증가하면 실패다.

## 9. 필수 Telemetry
최종 구현은 아래를 UI 상세, snapshot export, HIL artifact에 남긴다.

- process private bytes / working set
- raw ingress queue used/max/capacity/overrun
- slab pool used/max/capacity/alloc fail
- parser frames/bytes/CRC/length/version/seq/buffered bytes
- writer queue used/max/overrun/write max
- raw ledger queue/write max/failure
- analysis queue used/max/overrun/truth loss
- projection snapshot rate/pending/coalesced/dropped
- graph live points/buckets/memory estimate
- UI event loop p95/max/stall counters

## 10. Acceptance
PASS 기준:

- 30초 high-load smoke에서 parser/storage/queue fatal 0.
- 10분 high-load에서 memory slope가 안정화된다.
- 1시간 high-load에서 private bytes가 warmup 이후 plateau를 형성한다.
- 입력 중단 후 UI가 긴 backlog drain 없이 빠르게 회복한다.
- capture truth loss는 없거나 명시 counter/fatal로 드러난다.
- projection drop은 display degradation으로만 표시된다.

FAIL 기준:

- private memory가 시간에 비례해 계속 증가.
- Qt queued event나 hidden batch backlog가 telemetry 밖에서 증가.
- `TypedRecordList`가 live fanout 주 객체로 남음.
- full capture truth가 `AppController`에 남음.
- 30초만 통과하고 10분/1시간에서 멈춤.

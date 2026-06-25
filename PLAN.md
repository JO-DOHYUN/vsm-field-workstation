# VSM Capture Core Memory Refactor 최종 플랜

## Summary
목표는 UI throttle 보강이 아니라, VSM live hot path를 `TypedRecord/QByteArray/Qt queued full-batch` 구조에서 제거해 장시간 high-load에서도 메모리가 시간 비례로 증가하지 않게 만드는 것이다.

최종 구조는 단일 프로세스 안에서 먼저 완성하되, `CaptureCoreRuntime`을 Qt/QML과 독립된 capture engine처럼 설계한다. 이후 별도 `vsm-capture-core.exe`로 분리 가능해야 한다.

성공 기준은 30초 PASS가 아니라 `1시간 high-load`에서 process private memory가 plateau를 형성하고, UI 정지/회복 지연/무한 backlog가 없어지는 것이다.

## Key Architecture
- Harness 먼저 정리:
  - 새 skill `capture-core-memory`를 추가해 typed hot path, bounded queue, memory telemetry, capture writer, projection snapshot 작업을 전담시킨다.
  - `typed-evidence`는 protocol/truth/evidence 의미 담당, `graph-performance`는 renderer/graph 의미 담당으로 축소한다.
  - `AGENTS.md` 라우팅에 capture-core memory work를 명시하고, `history/decisions/`에 이번 구조 변경 이유/rollback 조건을 남긴다.

- Live production hot path 재정의:
  - 금지: live path에서 `TypedRecordList`를 Qt queued signal로 전달.
  - 금지: parser output이 `payload` + `frameBytes` 2개 `QByteArray`를 소유.
  - 금지: writer/raw ledger/analysis/projection이 같은 full record batch를 각각 복사.
  - 허용: replay/offline/import 호환용 `TypedRecord`는 유지하되, live production path에서는 사용하지 않는다.

- 새 core ownership:
  - `CaptureCoreRuntime`
    - `SerialDrainRuntime` raw byte 입력 수신
    - `ByteSlabPool` fixed-capacity raw bytes 보관
    - `TypedFrameParserRuntime` frame boundary/CRC/seq 검증
    - `CaptureWriterRuntime` raw typed frame bytes를 직접 stream/index batch write
    - `AnalysisWorkerRuntime`은 `CanRxLite`만 소비
    - `ProjectionSnapshotRuntime`은 UI용 bounded snapshot만 생성
  - `AppController`
    - full truth/typed frame/record batch를 소유하지 않는다.
    - 최신 status/projection snapshot/control facade만 가진다.

## Data Model
- `TypedFrameRef`
  - `slab_id`, `offset`, `frame_len`, `payload_offset`, `payload_len`
  - `record_type`, `flags`, `seq`, `mono_us_optional`
  - raw bytes는 slab에 1회만 존재한다.

- `CanRxLite`
  - `mono_us`, `capture_seq`, `bus`, `can_id`, `ext`, `rtr`, `dlc`, `data[8]`
  - analysis/raw tail/live latest/graph 입력의 기본 단위다.

- `CriticalEvidenceLite`
  - `BoardHealthLite`, `BoardEventLite`, `CapabilityLite`, `ControlAckLite`, `CanTxAuditLite`
  - UI/control 상태에 필요한 필드만 보유한다.
  - full payload가 필요하면 capture file offset을 참조한다.

- `UiProjectionSnapshot`
  - `latest_by_key`
  - `recent_tail_sample`
  - `transport_status`
  - `capture_status`
  - `analysis_status`
  - `csm_uplink_status`
  - `graph_status`
  - 20Hz 이하 single-flight로만 AppController에 전달한다.

## Implementation Changes
- Parser/core:
  - `TypedTransportParser::takeOne()` live path를 `TypedFrameRef` 반환 방식으로 교체한다.
  - live parser는 payload/frame bytes deep copy를 만들지 않는다.
  - SOF/length/CRC/version/seq diagnostics는 유지하고 capture diagnostics sidecar에 저장한다.
  - unknown record도 diagnostic count와 raw capture에는 남긴다.

- Queue/pool:
  - raw byte ingress queue: fixed 32MB.
  - typed frame/slab pool: fixed 64MB.
  - writer descriptor queue: fixed 64MB equivalent.
  - analysis queue: fixed frame count, overflow 시 `analysis_overrun/truth_loss`.
  - UI projection queue: single-flight snapshot only.
  - 어떤 queue도 무한 증가 금지. full이면 counter + fatal/invalid state로 드러낸다.

- Writer:
  - `TypedCaptureWriterRuntime`은 `TypedRecordList`가 아니라 `TypedFrameRef` descriptor를 받는다.
  - stream write는 slab raw bytes를 그대로 batch write한다.
  - index write는 descriptor metadata로 생성한다.
  - writer 지연은 parser/drain을 직접 막지 않지만, writer queue full이면 capture invalid + fatal diagnostic.

- Raw ledger:
  - raw tail 표시를 위해 full typed record를 재복사하지 않는다.
  - `CanRxLite` batch에서 ledger block을 만들고, UI에는 committed tail snapshot만 보낸다.
  - raw table은 계속 30000 rows hard cap 유지.

- Analysis:
  - analysis input은 `CanRxLite`만 받는다.
  - 모든 accepted CAN_RX는 analysis에 전달하거나, 전달 실패를 `truth_loss`로 남긴다.
  - timing/value/alarm/DLC/control evidence는 UI projection이 아니라 analysis input 기준으로 유지한다.

- UI/Graph:
  - AppController의 live full record 처리 제거.
  - `typedRecordsReceived(TypedRecordList)` live path 제거 또는 critical-lite snapshot으로 대체.
  - live graph는 raw vector 무한 append 금지.
  - graph active일 때만 per-series peak-preserving bucket ring 사용.
  - per-series live memory cap을 초과하면 오래된 bucket 제거, peak 의미 보존.

- Telemetry:
  - process private bytes / working set
  - slab used/max/capacity
  - raw ingress used/max/overrun
  - parser buffered bytes/max
  - writer queue used/max/overrun/write max
  - analysis queue used/max/overrun
  - projection pending/sampled/dropped
  - graph live points/buckets/memory estimate
  - Qt pending projection single-flight state
  - 1시간 테스트 artifact에 모두 저장한다.

## Test Plan
- Unit:
  - slab pool allocate/release/reuse/capacity/overrun
  - parser ref output preserves frame bytes, seq, CRC diagnostics
  - capture writer writes stream/index identical to old accepted bytes
  - `CanRxLite` segment expansion parity
  - analysis consumes every CAN_RX or reports overrun
  - UI projection single-flight never queues unbounded snapshots
  - live graph bucket cap preserves min/max/latest

- Regression:
  - existing typed replay/import compatibility 유지
  - legacy `.bin` replay 유지
  - control ACK/CAN_TX_RAW separation 유지
  - board health/event/capability diagnostics 유지
  - raw ledger DLC0 false row 재발 없음

- HIL/user route:
  - 30s smoke: parser fault 0, raw queue overrun 0, writer overrun 0, UI stall 없음.
  - 10m load: memory slope 안정, UI recovery 즉시성 확인.
  - 1h load: private bytes warmup 이후 +600MB 이하, slope 2MB/min 이하.
  - graph inactive/active 각각 측정.
  - storage OFF/ON 각각 측정.
  - 실패 시 어느 cap이 먼저 닿았는지 report로 자동 분리.

## Assumptions
- CSM firmware는 이번 작업 범위가 아니다.
- CDC transport는 유지한다.
- live production truth는 CSM typed stream raw bytes다.
- `TypedRecord`는 replay/offline compatibility type으로 남기지만 live hot path에서는 제거한다.
- 2프로세스 분리는 즉시 구현 목표가 아니라, 이번 `CaptureCoreRuntime` 경계가 성공한 뒤 가능한 다음 단계로 둔다.

# VSM 2+1 구조 완성 + 실사용 회귀 수정 최종 계획

## Summary
목표는 기존 철학을 유지한다: `capture.stream/index`만 authoritative truth, UI는 raw/typed stream consumer가 아니라 bounded view query consumer, Debug/Tap은 기본 OFF.  
최신 실데이터 기준으로 capture/CSM/drain은 정상에 가깝고, 문제는 Core view 생성/IPC/ViewClient/AppController/QML 표시 경계에 있다. 따라서 “UI throttle”이 아니라 **Core Data Plane / View Query Plane / Optional Debug Tap Plane의 소유권 폐쇄**를 완성한다.

웹 취약점 평가는 대부분 타당하다. 특히 `fromRawData` payload 소유권, `AnalysisSnapshot limit=1`, IPC backpressure/timeout 부재, raw ledger 의미 불일치, `ViewChanged` storm, AppController 재소유가 즉시 수정 대상이다.

## Key Decisions
- `capture.stream/index`는 유일한 원본 증거다. 현재 `RawLedger`는 원본 raw evidence가 아니라 decoded CAN tail이므로 이름과 UI 의미를 `decoded_can_tail/raw_tail_view`로 정정한다.
- `LiveTruthRuntime`은 truth가 아니므로 `LiveLatestViewRuntime`으로 개명하고, timing/value/alarm truth 입력에서는 완전히 제외한다.
- live hot path에서 `TypedRecord`의 `payload`가 소유/비소유를 상황별로 바꾸는 구조를 금지한다. 새 경계 타입은 `TypedFrameView`, `TypedFrameOwned`, `CanRxLite`, `DecodedCanTailRow`로 분리한다.
- `ViewChanged`는 per-record/per-batch push가 아니라 per-view single-flight coalesced notification이다. 기본 publish cadence는 live/latest 20Hz 이하, transport/capture/analysis 5~10Hz 이하.
- `AppController`는 Core view payload로 Evidence/Control/Transport runtime state를 재조립하지 않는다. UI DTO/model apply만 한다.
- IPC는 slow UI consumer가 Core capture를 막지 않게 한다. command response는 보존, view notification은 coalesce/drop 가능, drop counter 필수.

## Vulnerability Verdict
- P0-1 `fromRawData + compactRecord`: 맞음. 즉시 `TypedRecord` payload 소유권 분리.
- P0-2 `overrunFrames` 미전파: 맞음. analysis/raw-tail/capture invalid propagation 연결.
- P0-3 raw ledger 의미 불일치: 맞음. 원본 증거가 아니므로 decoded tail로 정정.
- P0-4 raw ledger pending drop: 맞음. truth loss가 아니라 display/tail loss로 명확히 표시하고 counter 연결.
- P0-5 LiveTruthRuntime 명칭: 맞음. coalesced latest view로 개명.
- P0-6 drain overrun invalid 미연결: 맞음. raw ingress overrun은 capture invalid + fatal diagnostic.
- P0-7 IPC outbound backpressure 없음: 맞음. per-client outbound cap과 coalescing 필요.
- P0-8 CoreViewClient timeout 없음: 맞음. per-view inflight timeout/회복 필요.
- P0-9 CoreProcessRuntime God-runtime화: 맞음. owner별 runtime으로 분리.
- P1-10 aggregate/each path 공존: 부분 맞음. full list fanout은 줄었지만 dual parser API/boolean path는 제거.
- P1-11 includeFrameBytes 의미 불명확: 맞음. 타입으로 소유권 강제.
- P1-12 RawLedger 원본 맥락 축소: 맞음. decoded tail row에 typed_seq/type/flags/offset/capture_seq 보강.
- P1-13 AnalysisSnapshot limit=1: 맞음. 즉시 수정.
- P1-14 generic array limiter: 맞음. view schema별 primary array limit로 교체.
- P1-15 IPC command validation 약함: 맞음. Core boundary에서 shape/range validation 추가.
- P1-16 startup/reconnect timeout 약함: 맞음. 명시 상태기계/timeout 추가.
- P2-17 QJsonArray 비용: 맞음. P0/P1 후 native view cache + query-time serialization으로 축소.

## Implementation Plan

### 1. P0: 증거/소유권 안전화
- `TypedTransportParser`는 더 이상 `QByteArray::fromRawData` payload를 장기 생존 객체에 넣지 않는다. parser callback 내부 전용은 `TypedFrameView`, 큐/저장/오프라인은 `TypedFrameOwned`만 사용한다.
- `LiveProjectionRuntime::compactRecord`는 `TypedRecord` 저장을 제거하고, control display에 필요한 최소 `ControlEvidenceLite`만 deep-copy한다.
- `TypedRecord includeFrameBytes` boolean API를 폐기한다. capture writer는 `TypedFrameOwned/ref`, analysis는 `CanRxLite`, display는 `LiveLatestRow`만 받는다.
- raw ingress queue overrun 발생 시 `host_drain_overrun`, `capture_invalid`, `raw_ingress_invalid`를 동시에 세우고 `capture.diagnostics.json`, `transport_summary`, `fatal_diagnostics`에 남긴다.

### 2. P0: 실사용 UI 회귀 수정
- `AnalysisSnapshot` query policy의 `limit=1`을 제거하고, schema별 limits를 적용한다: timing/value/alarm 각 1600 기본, diagnostics 128.
- `CoreMaterializedViewStore`의 generic “모든 배열 limit” 제거. 각 view는 primary array key만 자른다.
- live 표시 경로를 `live_latest` snapshot apply 전용 model로 단순화한다. `appendPendingLiveFrames -> liveFlushTimer -> queueLiveViewBatch` 경로는 production live에서 제거한다.
- `liveUiPaused`는 세션에서 자동 복원하지 않는다. 검증/연결 시작 시 기본 unpaused, 사용자가 누른 pause는 화면에 명확히 표시만 한다.
- Capture storage 상세 row는 `capture_progress` view와 `captureStorageUpdate` 둘 중 하나의 단일 소유자로 통합한다. 실제 `storage_bytes_written/record_count`와 UI row 불일치 금지.
- live list는 “사용자가 끝에 있을 때 자동 follow”, 사용자가 스크롤을 올리면 follow off, 최신 버튼으로 복귀하도록 QML 상태를 분리한다.

### 3. P1: Core/View/IPC 경계 폐쇄
- `CoreIpcServerRuntime`에 per-client outbound state를 추가한다: `queued_bytes`, `max_queued_bytes`, `dropped_view_notifications`, `disconnect_count`, `last_write_ms`.
- `view_changed`는 per-view pending map으로 coalesce한다. 같은 view가 이미 pending이면 최신 `view_seq/cheap_counts`로 replace하고 새 socket write를 쌓지 않는다.
- `CoreViewClientRuntime`은 per-view `inflight_started_ms`와 `timeout_ms=1500`을 가진다. timeout 시 inflight 해제, timeout counter 증가, 최신 pending seq 재요청.
- IPC command boundary에서 `start_transport`, `host_frame`, `control_cycle`, `start_capture`, `stop_capture`, `set_analysis_model` payload를 validation한다. 잘못된 값은 Core 내부 runtime에 도달하기 전 error response로 끝낸다.
- startup/reconnect 상태를 `ProcessStarting -> IpcConnecting -> IpcConnected -> TransportOpening -> TransportConnected -> Degraded/Stopped`로 고정하고 각 단계 timeout과 UI 표시를 둔다.

### 4. P1: Core God-runtime 분해
- `CaptureCoreProcessRuntime`은 lifecycle orchestrator만 남긴다.
- 새 내부 owner를 분리한다:
  - `CoreTransportOwner`: drain/thread/open/close/host TX
  - `CoreCaptureOwner`: capture writer, invalid/finalize, progress view
  - `CoreAnalysisOwner`: `CanRxLite` queue, overrun/truth_loss, analysis snapshot view
  - `CoreDecodedTailOwner`: decoded CAN tail writer/view, display drop counter
  - `CoreViewPublisher`: materialized view store, coalesced ViewChanged, IPC publish
  - `CoreControlOwner`: host command/control cycle, ACK/TX audit view
- owner 간 전달 타입은 raw `FrameRecordList/QVariantList/QJsonObject`가 아니라 명시 DTO만 허용한다. JSON은 IPC boundary에서만 생성한다.

### 5. P1/P2: View Store와 성능 구조
- `CoreMaterializedViewStore`는 native bounded cache를 보유하고, JSON/QCbor 변환은 query 응답 시에만 수행한다.
- `live_latest`는 `LiveLatestRow` keyed map + dirty flag로 유지하고, full sort/QJsonArray rebuild를 hot path에서 제거한다.
- `analysis_snapshot`은 이미 만들어진 bounded row vectors만 보관한다. UI query 중 full replay/scan 금지.
- `decoded_can_tail`은 원본 증거처럼 보이지 않도록 UI/diagnostics 용어를 정정하고, `source_capture_seq_range`, `typed_seq`, `record_type`, `flags`, `capture_stream_offset`를 포함한다.
- graph는 별도 `graph_bucket` view로만 이동한다. AppController의 graph raw accumulation은 active page + hard cap 조건만 허용한다.

### 6. Legacy Path Removal
- `SerialWorker` direct live/capture/analysis/raw-ledger path는 production build에서 제거하거나 explicit legacy compatibility flag 뒤로 격리한다.
- `TransportRuntime`이 `SerialWorker`를 직접 소유하는 production route를 제거한다. UI는 `CoreProcessClientRuntime`만 사용한다.
- `AppController`의 `applyCoreTransportSummaryView()`는 state mutation 금지. `TransportSummaryViewModel` apply만 수행하게 축소한다.
- `TypedRecordList` registration은 replay/test/offline 범위로만 남기고 production live target에서는 사용 금지 static check를 강화한다.

## Test Plan
- Unit:
  - typed parser ownership: dangling payload 재현 테스트, moved/copied frame lifetime 테스트.
  - view store schema limit: analysis rows가 `limit=1`에 잘리지 않는 테스트.
  - CoreViewClient timeout: inflight 응답 누락 후 재요청 테스트.
  - IPC backpressure: slow client에서 view_changed coalesce/drop counter 증가, command response 보존.
  - raw ingress overrun: capture invalid/fatal diagnostics propagation 테스트.
  - decoded tail semantics: tail drop은 truth loss가 아니라 display/tail loss로만 표시.
- Integration:
  - UI starts `vsm-capture-core.exe`, UI가 COM 직접 open하지 않는지 검증.
  - capture start/stop 후 UI capture row와 `capture.diagnostics.json` bytes/records parity.
  - live_latest snapshot 수신 후 model row 갱신, paused false 기본값, auto-follow 동작.
  - analysis snapshot `T/V/A` row count가 Core summary와 일치.
  - strict boundary scan에 `AppController transport state 조립`, `TypedRecordList live fanout`, `storage frameBytes live path` 재발 방지 rule 추가.
- HIL:
  - 30s smoke: parser fault 0, capture seq gap 0, raw ingress overrun 0, CSM ring_clear 0, segment_enqueue_fail 0, live model rows > 0.
  - 10m: Core private bytes plateau, UI private bytes plateau 또는 bounded oscillation, ViewChanged/sec 제한 확인.
  - 1h: capture parity OK, UI disconnect/freeze 중 Core truth 보존, reconnect 후 최신 view query 복구.
  - debug off 상태에서 gateway/profiler writer path 실행 0 확인.

## Acceptance Criteria
- 최신 30초 듀얼과 같은 조건에서 capture truth 정상 + UI rows 정상 + capture storage row parity가 동시에 만족된다.
- `core_view_changed_notifications`가 frame 수에 비례하지 않고 view cadence에 묶인다.
- `AnalysisSnapshot limit=1` 회귀가 불가능하다.
- slow UI/IPC가 Core drain/capture/analysis를 막지 않는다.
- raw ingress/capture/analysis/display drop이 서로 다른 counter와 severity로 분리된다.
- `VSM 장기 구조 개편 최종 플랜.md` 기준 Phase 1~4가 production path에서 충족되고, Phase 5 legacy removal은 strict scan으로 확인된다.

## Assumptions
- CSM firmware는 이번 계획의 수정 대상이 아니다.
- `capture.stream/index` 포맷은 유지한다.
- 현재 raw ledger 파일 포맷은 증거 truth가 아니라 decoded tail cache로 재정의한다.
- UI 표시량 감소는 허용하지만 analysis/capture truth 손실 은폐는 금지한다.
- 구현은 P0부터 순서대로 진행하고, 각 단계마다 구경로 삭제와 regression guard를 같은 slice에 포함한다.

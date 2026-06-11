# PROJECT_ARCHITECTURE_KO

이 문서는 현재 CAN Monitor의 큰 구조 지도다. 상세 완료 계획은 [[../PLAN]], board/Qt 방향은 [[../BOARD_QT_FINAL_ARCHITECTURE]]가 우선이다.

## 현재 축
- `src/backend/`: packet parser, replay, recorder, analysis, model validation, typed transport foundation
- `qml/`: operator UI, replay bar, recent graph, full-range `전체그래프`
- `data/`: active model/rules baseline
- `tests/`: unit/component integration tests
- `scripts/`: Windows deploy helper
- `packaging/`: release notice, SBOM, installer hook

## Runtime 분리 목표
AppController는 QML facade와 orchestration만 남기고 내부 runtime을 분리하는 방향이다.
- TransportRuntime: `SerialWorker` thread lifecycle, queued live serial/log/control operations, typed-only production mode
- SerialWorker runtimes: `TypedIngressRuntime`, `LegacyIngressRuntime`, `HostTxRuntime`, `ControlCycleRuntime`; `SerialWorker` itself stays at serial open/read/write, timer orchestration, and signal bridging
- EvidenceRuntime: board alive/control-capable state over `CAPABILITY` and `BOARD_HEALTH`
- StorageRuntime: project-local `replay_data` root, typed capture/session paths, snapshot paths
- ReplayRuntime: load/play/pause/seek/rebuild pacing, replay open/cache/session path ownership
- AnalysisRuntime: timing/value/alarm source separation
- GraphRuntime: recent graph/full overview/detail rebuild
- OperatorRuntime: badges, headline, action text, event summary
- UiStateStore: filters, sort, selection, display/session state
- ControlAuditModel: request/write/ACK/CAN_TX_RAW/feedback/fault audit rows and stage summaries

VMS-CSM 통합 기준의 상세 런타임 목표는 [[VMS_ARCHITECTURE_KO]]를 따른다.
Live production transport는 typed evidence stream 전용이며, legacy 20-byte는 replay/import compatibility로만 유지한다.

`RuntimePaths`와 저장/replay open-directory 경로 책임은 `StorageRuntime`/`ReplayRuntime` 쪽으로 이동했다. board alive/control-capable 판정은 `EvidenceRuntime`, control evidence counters/stages/model rows는 `ControlAuditModel`이 소유한다. `TransportRuntime`은 `SerialWorker` 생성, thread lifecycle, queued COM/log/control 호출을 소유한다. `TypedIngressRuntime`, `LegacyIngressRuntime`, `HostTxRuntime`, `ControlCycleRuntime`은 각각 typed ingest, legacy ingest/logging, host TX queue, control-cycle burst/pacing 상태를 소유한다. `TransportSession`은 typed parser fault, host TX queue/backpressure, live delay 진단을 소유한다. 다음 분리 대상은 Graph/Replay UX runtime과 model-pack backed control policy다.

## Build-risk 경계
아래를 건드리면 build-risk로 본다.
- `AppController.h/.cpp`
- 신규 C++ 파일
- `main.cpp`
- `CMakeLists.txt`
- QML 등록/import/module
- signal/property/slot/`Q_PROPERTY`

## Invariant
live/replay source separation, graph truth/fixed-axis/peak, legacy 20-byte compatibility, typed evidence truth는 구조 변경 중에도 먼저 보호한다.

# VSM Core Data/View/Tap Architecture

## Passive-Safe Runtime Profile
VSM product default is `passive_product`.

- UI process: `vsm-ui.exe` / `can_monitor_qml_reboot.exe`
- Core process: `vsm-capture-core.exe --profile passive_product`
- Optional debug/tap: default OFF, separate plane
- Serial open: read-only
- DTR/RTS: DTR is allowed only as a CSM-declared Arduino CDC session gate; RTS
  remains no-touch.
- Host TX/control/lab gateway: disabled
- `profile_status` is a Core materialized view and exposes this runtime contract.

`full_instrumented` is bench/lab only. It may open serial read/write, touch DTR/RTS,
send host TX/control, or run a COM-owning gateway, but it must not be used as a
vehicle passive product acceptance result.

## 목적
VSM live production 구조의 장기 기준은 단순한 3개 프로세스 분리가 아니라
`Core-owned Data Plane + View Query Plane + Optional Debug Tap Plane`이다.

물리 실행은 장기적으로 `vsm-capture-core.exe + vsm-ui.exe`를 기본으로 하고,
debug/gateway/tap은 필요할 때만 붙인다.

## 핵심 원칙
- `capture.stream`과 `capture.index`만 authoritative truth다.
- raw ledger, live latest, analysis snapshot, graph bucket은 모두 재생성 가능한 materialized view다.
- Core만 COM/USB를 소유한다.
- UI는 raw/typed stream을 직접 받지 않는다.
- UI는 `ViewChanged` 알림을 받고 필요한 view만 query한다.
- Core는 이미 만들어진 bounded view만 반환한다. query 중 full scan/replay 계산을 하지 않는다.
- Debug/Gateway는 production path가 아니라 non-blocking tap이며 기본 OFF다.

## Core-Owned Data Plane
`vsm-capture-core.exe` 또는 현재 전환기의 in-process `CaptureCoreRuntime`이 data plane owner다.

책임:
- USB/serial drain
- typed SOF/length/CRC/seq 검증
- append-only `capture.stream/index` 기록과 finalize
- raw ledger segment/index 기록
- analysis truth state 계산
- bounded materialized view 생성
- host control request와 board evidence audit 보존

금지:
- QML 의존
- AppController 의존
- UI table/graph model 보유
- full `TypedRecordList`를 UI/main thread로 fanout
- debug/profiler 문자열을 hot path에서 생성

## View Query Plane
UI는 CoreClient다.

Core push:
```text
ViewChanged {
  view_name
  view_seq
  severity
  cheap_counts
  timestamp_ms
}
```

UI pull:
```text
GetView {
  view_name
  since_seq
  limit
}
```

Core response:
```text
ViewSnapshot {
  view_name
  view_seq
  updated_at_ms
  severity
  dropped_display_count
  source_capture_seq_range
  payload
}
```

View 목록:
- `core_health`
- `transport_summary`
- `capture_progress`
- `analysis_snapshot`
- `live_latest`
- `raw_ledger_tail`
- `graph_bucket`
- `control_audit`
- `fatal_diagnostics`

## Optional Debug Tap Plane
Debug/Gateway는 production capture를 대체하지 않는다.

규칙:
- normal mode에서는 Core가 COM/USB를 단독 소유한다.
- gateway mode는 normal production mode와 상호 배타다.
- tap은 fixed cap과 drop counter를 가진다.
- tap backpressure는 Core capture data plane으로 전파되면 안 된다.
- debug artifact PASS는 production `capture.stream/index` PASS를 대체하지 않는다.

## Migration Rule
Live/capture-core 변경은 [[docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO]]의 owner/consumer/drop policy와 금지 경계를 먼저 만족해야 한다.

기능 구현 중 편의를 위해 AppController나 Qt signal 경로에 Core 책임을 임시로 쌓지 않는다.
새 live/capture 기능은 먼저 data plane owner, view owner, tap 여부를 정하고 들어간다.

허용되는 전환 상태:
- 아직 한 프로세스여도 `CaptureCoreRuntime` API가 process-neutral하면 허용한다.
- UI가 Core API/query contract만 사용하면 허용한다.
- legacy in-process fallback은 30s/10m/1h HIL 통과 후 제거 대상으로 표시한다.

금지되는 전환 상태:
- UI가 raw typed stream consumer가 됨.
- View query가 즉석 full scan/replay 계산을 수행함.
- Debug/Gateway writer가 normal live mode에서 실행됨.
- projection drop을 parser/storage/CAN truth loss처럼 표시함.

## Acceptance
- Core-only capture 30s/10m/1h memory plateau.
- UI connected 상태에서도 Core memory plateau.
- UI disconnected/frozen 상태에서도 Core capture seq gap 0.
- raw ledger/capture parity OK.
- parser CRC/length/seq fault 0.
- CSM `ring_clear=0`, `segment_enqueue_fail=0`.
- debug off 상태에서 tap/gateway/profiler writer path 실행 0.

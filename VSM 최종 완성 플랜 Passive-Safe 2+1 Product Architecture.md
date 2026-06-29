# VSM 최종 완성 플랜: Passive-Safe 2+1 Product Architecture

## Summary
VSM 최종 목표는 단순 UI/성능 개선이 아니라 **실차 CAN에 영향을 주지 않는 Passive Product 구조**와 **개발/검증용 Full Instrumented 구조**를 명확히 분리하는 것이다.

최종 기본값은 `Passive Product Profile`이다. `vehicle_impact_state=impossible`은 VSM 설정만으로 표시 금지이며, 반드시 VSM runtime profile + CSM capability + hardware safety case + bench verification이 모두 맞을 때만 표시한다.

## Key Architecture
- 기본 실행 구조:
  - `vsm-ui.exe`: UI/View query client. COM 직접 open 금지.
  - `vsm-capture-core.exe`: COM/USB 단독 owner, capture truth owner.
  - `vsm-debug-sidecar.exe`: 선택 실행. 기본 OFF. COM 소유 금지.
- 진실 기준:
  - `capture.stream/index`만 authoritative truth.
  - `live_latest`, decoded tail, graph, analysis rows, transport detail은 bounded materialized view.
- 기본 profile:
  - `Passive Product`: read-only, DTR/RTS no-touch, host TX/control/debug gateway hard reject.
  - `Full Instrumented`: bench/HIL 전용, vehicle impact possible 표시, host TX/control/debug 허용 가능하되 unlock/audit/timeout 필수.
- CSM은 별도 구현 대상이지만, VSM은 CSM capability가 없거나 active-capable이면 passive PASS 표시 금지.

## Implementation Phases

### Phase 0. Baseline Lock
- 현재 build/test/log 상태를 기준 파일로 남긴다.
- `capture.stream/index` truth, materialized view, debug sidecar, passive/full profile 정의를 문서와 코드 enum에 고정한다.
- strict boundary scan 대상을 확장한다:
  - UI COM open 금지
  - passive에서 serial write 금지
  - passive에서 DTR/RTS set 금지
  - `TypedRecordList`/`FrameRecordList` production fanout 금지
  - debug gateway normal path 실행 금지

### Phase 1. Profile/Capability/Safety Contract
- 새 타입을 추가한다:
  - `CoreRuntimeProfile`
  - `TransportOpenPolicy`
  - `CsmCapabilityManifest`
  - `ProfileMatchResult`
  - `VehicleImpactState`
  - `SafetyCaseId`
- VSM profile manifest를 추가한다:
  - `profiles/passive_product.profile.json`
  - `profiles/full_instrumented.profile.json`
- Passive 판단 규칙:
  - VSM passive + CSM passive + hardware safety case verified + bench id 있음 → `verified_passive`
  - VSM passive + CSM capability 없음 → `blocked_unknown`
  - VSM passive + CSM active-capable → `blocked_active_csm`
  - VSM full → `active_possible`, passive acceptance forbidden
- UI는 `connected`보다 먼저 `profile_match_result`, `vehicle_impact_state`, `serial_open_mode`, `host_tx_enabled`, `control_enabled`, `debug_mode`를 표시한다.

### Phase 2. Transport Safety Gate
- `SerialDrainRuntime`은 profile을 주입받는다.
- Passive:
  - `QIODevice::ReadOnly`
  - DTR/RTS no-touch
  - host TX queue 생성/쓰기 금지
  - `sendHostFrame()` 호출 시 core boundary에서 즉시 rejected audit
- Full:
  - `ReadWrite` 허용 가능
  - DTR/RTS 정책은 명시값으로만 적용
  - host TX/control은 unlock/audit/lease/timeout 필수
- `CoreIpcServerRuntime`에서 passive `host_frame`, `control_cycle`, lab gateway start를 hard reject한다. UI 버튼 비활성화만으로 처리 금지.

### Phase 3. Core Data Plane Closure
- Core 내부 data flow를 고정한다:
  - raw bytes → bounded ingress/slab → typed parser → capture writer
  - parser output은 `TypedFrameView`/`TypedFrameOwned`/`CanRxLite`로 분리
  - UI/display에는 `LiveLatestRow`, `DecodedCanTailRow`만 전달
- 제거/격리 대상:
  - production hot path의 `TypedRecordList` fanout
  - production hot path의 `FrameRecordList` 공용 fanout
  - storage `frameBytes` live path 전달
  - diagnostics payload로 runtime state 갱신
- 모든 queue는 bounded + high-water + overrun counter 필수.
- raw ingress overrun은 `capture_invalid=true`, `fatal_diagnostics`, `capture.diagnostics.json`에 강제 전파한다.

### Phase 4. View Query Plane Completion
- UI는 `ViewChanged` cheap notification만 받는다.
- 실제 데이터는 `GetView(view_name, since_seq, limit)`로만 가져온다.
- Core view:
  - `core_health`
  - `profile_status`
  - `transport_summary`
  - `capture_progress`
  - `live_latest`
  - `decoded_can_tail`
  - `analysis_snapshot`
  - `graph_bucket`
  - `control_audit`
  - `fatal_diagnostics`
- `ViewChanged`는 per-view single-flight coalesced publish로 제한한다.
- `CoreViewClientRuntime`은 per-view timeout/retry를 가진다.
- `capture_progress`와 기존 `captureStorageUpdate` 중복 상태 갱신은 하나로 통합한다.
- Live UI는 latest snapshot model만 사용한다. legacy pending live route는 production에서 삭제한다.

### Phase 5. Debug Plane Split
- 기존 `vsm_debug_gateway.py`는 `Full Instrumented / Lab Gateway`로만 분류한다.
- Passive에서는 lab gateway start를 core boundary에서 reject한다.
- 새 passive debug는 sidecar 방식만 허용한다:
  - COM open 금지
  - serial write 금지
  - core/capture/view query attach만 허용
  - attach/detach가 transport route를 바꾸면 실패
- Debug OFF 상태에서 profiler/gateway/tap writer path 실행 0을 검증한다.

### Phase 6. UI/UX Product Completion
- Live 화면:
  - 새 row가 있으면 사용자가 끝에 있을 때 자동 follow
  - 사용자가 스크롤을 올리면 follow off
  - “최신 보기”로 복귀
  - empty message는 truth/raw ledger가 아니라 display view 의미로 수정
- 전송 상세:
  - profile/capability/safety state를 최상단 노출
  - CSM uplink loss, VSM drain overrun, display drop, analysis truth loss를 분리 표시
- RawLedger 명칭은 `DecodedCanTail`로 정정한다. 원본 evidence처럼 보이는 표현 금지.

## Verification Plan
- Static:
  - transition/strict boundary scan 모두 통과
  - passive path serial write/DTR/RTS/host TX/control/debug gateway 금지 rule 추가
- Unit:
  - passive IPC hard reject
  - passive serial open policy
  - profile/capability match matrix
  - CoreViewClient timeout/retry
  - view coalescing/backpressure
  - raw ingress overrun → capture invalid/fatal diagnostic
- Integration:
  - UI가 COM 직접 open하지 않음
  - UI starts/attaches `vsm-capture-core.exe`
  - core crash/restart/reconnect 상태기계 검증
  - debug sidecar attach가 COM open을 만들지 않음
  - capture progress UI와 `capture.diagnostics.json` bytes/records parity
- HIL/long-run:
  - 30s smoke
  - 10m memory plateau
  - 1h memory plateau
  - parser fault 0
  - capture seq gap 0 또는 명시 invalid
  - raw ingress overrun 0
  - CSM ring clear 0
  - segment enqueue fail 0
- Vehicle-impact gate:
  - USB plug/unplug 중 ECU error 0
  - VSM connect/disconnect 반복 중 ECU error 0
  - capture start/stop 중 ECU error 0
  - passive debug sidecar attach/detach 중 ECU error 0
  - passive에서 serial write call count 0
  - passive에서 DTR/RTS transition 0
  - 이 gate 없이는 `vehicle_impact_state=verified_passive` 표시 금지

## Non-Negotiable Rules
- 기능 숨김으로 PASS 만들기 금지.
- UI 버튼만 막고 core/IPC/transport를 열어두는 구현 금지.
- diagnostics를 architecture 대체물로 사용 금지.
- debug gateway를 passive debug라고 부르기 금지.
- `capture.stream/index` 외 데이터를 truth로 표시 금지.
- Full profile 결과로 passive 제품 PASS 주장 금지.
- CSM capability 없이 board alive/passive safe 단정 금지.

## Definition of Done
VSM 최종 완료는 다음을 모두 만족해야 한다.

- 기본 실행이 `vsm-ui.exe + vsm-capture-core.exe` 2프로세스다.
- UI는 COM/USB를 직접 소유하지 않는다.
- Passive profile에서 host TX/control/debug gateway/serial write/DTR/RTS touch가 모두 불가능하다.
- Full profile은 bench/test only로 표시되고 passive acceptance가 불가능하다.
- Debug는 production path 밖에 있고 기본 OFF다.
- UI는 view query consumer이며 raw/typed stream consumer가 아니다.
- Core capture는 UI freeze/disconnect/debug attach와 무관하게 지속된다.
- 실차 영향 없음은 bench/vehicle gate를 통과한 경우에만 표시된다.

# VSM Passive-Safe 2+1 Product Architecture

## 목적
VSM 제품 기본 실행은 실차 CAN/차량 상태에 영향을 주지 않는 passive monitor/logger여야 한다.
기본 구조는 `vsm-ui.exe + vsm-capture-core.exe` 2프로세스이며, 디버그/탭은 필요할 때만 별도 plane으로 붙는 `2+1` 구조다.

## 실행 프로필

### Passive Product
기본값이다.

- Serial open: `ReadOnly`
- DTR/RTS: RTS no-touch. DTR may be asserted only as a read-only Arduino CDC
  session gate when the CSM `CAPABILITY` declares `usb_cdc_dtr_session_only=1`.
- Host TX: disabled
- Control cycle: disabled
- COM-owning lab gateway: disabled
- Debug: non-owning sidecar/tap만 허용
- Acceptance: CSM passive capability와 하드웨어 safety case 없이는 `verified_passive`로 주장하지 않는다.

### Full Instrumented
bench/lab 전용이다.

- Serial open: `ReadWrite`
- DTR/RTS: assert 가능
- Host TX/control/lab gateway 가능
- 실차 passive product PASS로 사용 금지

프로필 소유자는 `RuntimeProfile`이다. Serial/Core/IPC/UI는 각자 임의 정책을 만들지 않고 이 타입을 참조한다.

## 프로세스 책임

### Capture Core Data Plane
`vsm-capture-core.exe`가 COM/USB를 단독 소유한다.

- typed byte drain
- typed parser 검증
- `capture.stream/index` append-only truth 저장
- bounded materialized view 생성
- control/host TX 명령의 프로필 gate

Core는 QML model, AppController UI 상태, 그래프 위젯을 소유하지 않는다.

### View Query Plane
`vsm-ui.exe`는 CoreClient다.

- `ViewChanged` 알림을 받고 필요한 view만 `GetView` query
- live/latest, transport, capture, analysis, decoded tail을 bounded view로 표시
- raw typed stream이나 full `TypedRecordList`를 직접 소비하지 않는다.

### Optional Debug Tap Plane
기본 OFF다.

- 실차 제품 모드에서는 COM ownership을 가져오지 않는다.
- raw/evidence deep trace가 필요하면 non-blocking sidecar/tap으로 붙는다.
- drop counter와 artifact는 debug evidence이며 production truth를 대체하지 않는다.
- 기존 `vsm_debug_gateway.py` 식 COM-owning gateway는 Full Instrumented lab profile에서만 허용한다.
- Passive Product diagnostics는 `vsm-debug-tap.exe` non-owning Core IPC sidecar만 허용한다.
- `vsm-debug-tap.exe`는 COM/USB를 열지 않고 `ViewChanged`/`GetView`만 사용한다.

## Truth와 View
- Authoritative truth: `capture.stream` + `capture.index`
- Derived view: `live_latest`, `decoded_can_tail`, `analysis_snapshot`, `graph_bucket`, `transport_summary`, `profile_status`
- UI 표시 drop은 display/tail drop으로만 표시한다. capture/parser/CSM truth loss와 섞지 않는다.

## Passive Gate Refinement
- VSM은 `configured_passive`, `runtime_passive`,
  `hardware_evidence_claimed`, `external_artifact_verified`,
  `verified_passive`를 분리한다.
- CSM `CAPABILITY` hardware field는 runtime claim/reference다. mismatch
  진단에는 사용하지만 물리적 PASS proof로 쓰지 않는다.
- `verified_passive`는 2-bus RX-only passive capability, host/control/downlink
  path 없음, MCP passive/TXREQ violation 0, complete hardware claim, 그리고
  reference analyzer/scope/DTC artifact 검증이 모두 맞을 때만 가능하다.
- 제품은 2-bus 전용이다. capability가 2-bus RX-only를 만족하지 못하면
  VSM은 blocking mismatch로 표시한다. 이것은 1-bus product path가 아니다.
- USB attach quarantine은 CDC/uplink/session payload cleanup이다. CAN
  front-end passive drain을 멈추거나 MCP/transceiver를 reset/reconfigure하는
  상태가 아니다.

## 금지 경계
- RuntimeProfile 없이 serial `ReadWrite`를 직접 열지 않는다.
- Passive Product에서 DTR/RTS를 직접 assert하지 않는다.
- Passive Product에서 `host_frame`, `control_cycle`, `gateway_tcp`를 Core 내부로 통과시키지 않는다.
- `TypedRecordList`를 UI/analysis/projection/capture 공용 fanout으로 쓰지 않는다.
- AppController가 transport raw state owner가 되지 않는다.
- diagnostics payload로 runtime state를 갱신하지 않는다.
- UI projection을 timing/value/alarm truth로 쓰지 않는다.
- storage `frameBytes`를 live view path로 넘기지 않는다.

## 완료 기준
- UI 실행 시 child `vsm-capture-core.exe`가 `--profile passive_product`로 뜬다.
- 진단 기록을 켜면 `vsm-debug-tap.exe`가 세 번째 프로세스로 뜨며 Core/UI transport를 끊지 않는다.
- 전송 상세 `passive_safety_profile` row가 serial `read_only`, DTR/RTS `no_touch`, host/control/gateway `off`를 보인다.
- Passive Product에서 GW 버튼, host TX, control cycle은 실행되지 않고 blocked evidence만 남는다.
- Full Instrumented는 명시 프로필로만 가능하고 실차 PASS로 표기하지 않는다.
- `scripts/check_vsm_boundary_rules.py --mode strict`가 passive serial policy bypass를 잡는다.
- Release build와 targeted ctest가 통과한다.

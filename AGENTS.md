# AGENTS.md

이 저장소의 Codex 상위 운영 맵이다. 짧고 안정적으로 유지한다.
상세 절차는 `.agents/skills/`, 설계 근거는 `docs/`, 오래된 이력은
`history/`에 둔다.

## 1. 먼저 읽을 것

항상 아래 순서로 본다.

1. `START_HERE_KO.md`
2. `BRIEF.md`
3. `INDEX.md`
4. 이번 턴과 직접 관련된 skill 또는 문서만 추가
5. 하네스 자체를 바꾸는 턴일 때만 `HARNESS_MASTER_KO.md`

## 2. 프로젝트 목표

목표는 실차/현장 기준에서 차량 CAN에 영향을 주지 않는 2-bus
Passive-Safe CAN monitor / logger / replay / decode / evidence-first
workstation을 완성하는 것이다.

항상 보존한다.

- VSM field/product 기본 실행은 Passive-Safe 2+1이다:
  `vsm-ui.exe + vsm-capture-core.exe`, optional `vsm-debug-tap.exe` default OFF.
- VSM live production path는 CSM typed evidence stream 전용이다.
- COM open만으로 board alive로 보지 않고 valid `CAPABILITY`와 fresh
  `BOARD_HEALTH`를 기준으로 판단한다.
- `capture.stream/index`만 authoritative capture truth다.
- UI/graph/decoded tail/analysis rows는 bounded materialized view다.
- CAN RX, CAN TX audit, voltage/ADC raw, board health/event, capability,
  control ack evidence type은 분리한다.
- Host 요청 TX, `CONTROL_ACK`, `CAN_TX_RAW`, feedback은 서로 다른 evidence다.
- Host 요청 TX는 matching `CAN_TX_RAW` 전까지 실제 CAN 송신 성공으로 보지 않는다.
- Passive Product profile에서는 serial read-only, DTR은 CSM-declared Arduino
  CDC session gate일 때만 허용, RTS/host TX/control cycle/COM-owning lab
  gateway는 금지한다.
- Full/Instrumented profile은 bench/lab 전용이며 실차 PASS나 passive product
  acceptance로 주장하지 않는다.
- Hardware passive evidence in CSM `CAPABILITY` is a claim/reference only.
  `verified_passive`는 외부 analyzer/scope/DTC artifact 검증 전에는 금지한다.
- 제품은 2-bus ACK-capable observe-only monitor다. 1-bus product/acceptance는 금지하지만,
  missing/one-bus capability mismatch 경고는 반드시 유지한다.
- `USB_ATTACH_QUARANTINE`은 CDC/uplink/session payload quarantine이며 CAN
  front-end drain 정지가 아니다.
- 현재 CSM passive firmware는 USB power-up 동안 CAN front-end initialization을
  지연하고, `CAN_FRONTEND_PRESESSION_HOLD` 및 `CAN_FRONTEND_SESSION_READY`
  event로 ACK-observe arm 시점을 증명해야 한다.
- Passive Product는 host TX/control/downlink를 제공하지 않는다. 단, 안정된
  CSM host session 이후 ACK-capable observe mode는 제품 동작이다. ACK 능력과
  `CAN_TX_RAW`/control 능력을 절대 같은 것으로 취급하지 않는다.

## 3. VSM/CSM 동시 작업 기준

- VSM repo: `C:\WORKS\VS\turn81_full_buildfix2`.
- CSM repo: `C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM`.
- VSM/CSM 통합 동작, wire contract, passive lifecycle, capability, board health,
  USB/CAN safety를 건드리는 턴은 두 repo 상태를 모두 확인한다.
- CSM 파일 수정/빌드/upload는 반드시 CSM repo root에서만 한다.
- VSM 파일 수정/빌드/test는 반드시 VSM repo root에서만 한다.
- 두 repo는 독립 commit/push한다. 한쪽 변경을 다른 repo commit에 섞지 않는다.
- 사용자 입력/분석용 untracked 문서는 명시 없이는 commit하지 않는다.

## 4. 검증 최소화 규칙

불필요한 빌드는 금지한다. 검증은 변경 위험을 증명하는 최소 단위로 선택한다.

- 문서/하네스/주석만 변경: `git diff --check`와 관련 텍스트 검색만 수행한다.
  빌드하지 않는다.
- VSM C++/QML/runtime 경계 변경: affected target build와 관련 `ctest -R`부터
  수행한다.
- VSM release/runtime boundary 완성 slice: Release build, 필요한 subset test,
  필요 시 full ctest와 startup smoke를 수행한다.
- CSM 문서만 변경: `git diff --check`만 수행한다. PlatformIO build하지 않는다.
- CSM firmware/platformio/guard/protocol 변경: 해당 env만 PlatformIO build한다.
  passive product env와 alias/full env를 모두 빌드하는 것은 profile 분리 검증이
  필요한 경우에만 한다.
- Upload는 빌드 검증이 아니다. MCU reset/USB re-enumeration으로 차량 CAN에
  영향을 줄 수 있으므로 사용자가 명시적으로 요구하고 현재 hardware context가
  안전할 때만 수행한다.
- 실차 PASS는 코드 빌드로 주장하지 않는다. external analyzer/scope/DTC evidence가
  필요하다.

## 5. 상시 금지 경계

- 기능을 없애서 UI/성능/빌드 문제를 숨기지 않는다.
- 검증하지 않은 build/run/replay/graph/deploy/HIL 성공은 단정하지 않는다.
- `TypedRecordList` UI/analysis/projection 공용 fanout 금지.
- `AppController` transport raw state 조립 금지.
- diagnostics payload 기반 runtime state 갱신 금지.
- UI projection의 timing/value/alarm truth 사용 금지.
- storage `frameBytes` live view 전달 금지.
- RuntimeProfile을 우회한 `QIODevice::ReadWrite` serial open 금지.
- Passive profile에서 host_frame/control_cycle/gateway_tcp 실행 금지.
- 나중에 Runtime으로 뺄 책임이면 `AppController`에 임시 누적하지 말고 boundary,
  telemetry, tests, exit condition을 같은 slice에 포함한다.
- live hot path에서 시간 비례 메모리 증가가 보이면 UI throttle이 아니라
  capture-core ownership 문제로 먼저 의심한다.

## 6. 작업 라우팅

- capture-core memory/hot path/slab/bounded queue/projection snapshot:
  `.agents/skills/capture-core-memory/SKILL.md`
- typed board evidence/storage/control gate/protocol semantics:
  `.agents/skills/typed-evidence/SKILL.md`
- graph truth/performance/overview/detail:
  `.agents/skills/graph-performance/SKILL.md`
- replay/live/source semantics:
  `.agents/skills/replay-semantics/SKILL.md`
- build/test/deploy/startup smoke:
  `.agents/skills/qt-build-verify/SKILL.md`
- AGENTS/.codex/skill boundary/harness structure:
  `.agents/skills/harness-maint/SKILL.md`
- BRIEF 축소/history 이관/Obsidian link:
  `.agents/skills/doc-history-rollup/SKILL.md`

핵심 계약 문서:

- `docs/architecture/VSM_CSM_PRODUCT_IDENTITY_KO.md`
- `docs/architecture/VSM_CSM_FINAL_PRODUCT_COMPLETION_TARGET_KO.md`
- `docs/architecture/PROJECT_CONSTITUTION_KO.md`
- `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`
- `shared/protocol/typed_stream_v1.md`
- `docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO.md`
- `docs/architecture/VSM_PASSIVE_SAFE_2PLUS1_ARCHITECTURE_KO.md`
- `docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO.md`
- `docs/reviews/passive_product_boundary_audit.md`
- `docs/hardware/CSM_PASSIVE_FRONTEND_REQUIREMENTS.md`
- `docs/hardware/CSM_PASSIVE_FRONTEND_ACCEPTANCE.md`
- `docs/hardware/CSM_FIELD_SKU_BOM_RULES.md`
- `docs/hardware/CSM_USB_CAN_ISOLATION_POLICY.md`
- `docs/ai_harness/BUILD_VERIFY_POLICY_KO.md`

## 7. 보고 형식

작업 결과는 기본적으로 아래를 포함한다.

- 변경 파일
- 핵심 변경점
- 실행한 검증과 결과
- build-risk 또는 미검증 리스크
- 사용자가 바로 확인할 포인트

## 8. 한 줄 원칙

현재 프로젝트 폴더의 실제 파일을 기준으로, VSM/CSM 경계를 혼동하지 않고,
필요한 검증만 수행하며, 실차 안전을 빌드 성공으로 과장하지 않는다.

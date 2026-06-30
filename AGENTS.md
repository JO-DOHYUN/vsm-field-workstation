# AGENTS.md

이 저장소의 Codex 상위 운영 맵이다. 이 파일은 짧고 안정적으로 유지한다.
상세 절차는 `.agents/skills/`, 설계 근거는 `docs/`, 오래된 이력은 `history/`에 둔다.

## 1. 먼저 읽을 것
항상 아래 순서로 본다.

1. `START_HERE_KO.md`
2. `BRIEF.md`
3. `INDEX.md`
4. 이번 턴과 직접 관련된 skill 또는 문서만 추가
5. 하네스 자체를 바꾸는 턴일 때만 `HARNESS_MASTER_KO.md`

## 2. 프로젝트 목표
목표는 실차/현장 기준에서 신뢰 가능한 CAN monitor / logger / replay / decode / evidence-first control workstation을 완성하는 것이다.

항상 보존한다.

- VSM live production path는 CSM typed evidence stream 전용이다.
- COM open만으로 board alive로 보지 않고 valid `CAPABILITY`와 fresh `BOARD_HEALTH`를 기준으로 판단한다.
- typed board stream 원본 byte가 production truth다.
- CAN RX, CAN TX audit, voltage/ADC raw, board health/event, capability, control ack evidence type은 분리한다.
- host 요청 TX, `CONTROL_ACK`, `CAN_TX_RAW`, feedback은 서로 다른 evidence다.
- host 요청 TX는 matching `CAN_TX_RAW` 전까지 실제 CAN 송신 성공으로 보지 않는다.
- live와 replay 의미는 분리한다.
- graph는 truth-first, fixed-axis, peak-preserving 기준을 유지한다.
- legacy 20-byte packet / CRC8 / DLC / `t_us` wrap은 replay/import 호환으로만 보존한다.
- Windows Qt/CMake build, test, deploy 재현성을 유지한다.

## 3. 상시 규칙
- `BRIEF.md`는 현재 기준본, 유지 기능, 현재 목표, 즉시 다음 작업만 둔다.
- 과거 실패, 시행착오, 결정 배경은 `history/`로 보낸다.
- routine 코드 수정 턴에서 하네스 재설계를 섞지 않는다.
- `.agents/`와 `.codex/` 수정은 명시적인 하네스 변경 턴에서만 한다.
- 기능을 없애서 UI/성능/빌드 문제를 숨기지 않는다.
- 검증하지 않은 build/run/replay/graph/deploy/HIL 성공은 단정하지 않는다.
- 나중에 Runtime으로 뺄 책임이면 `AppController`에 임시 누적하지 말고 boundary, telemetry, tests, exit condition을 같은 slice에 포함한다.
- live hot path에서 시간 비례 메모리 증가가 보이면 UI throttle이 아니라 capture-core ownership 문제로 먼저 의심한다.
- VSM long-run live 구조는 Core-owned Data Plane / View Query Plane / Optional Debug Tap Plane 기준으로 판단한다.
- VSM field/product 기본 실행은 Passive-Safe 2+1 기준이다: `vsm-ui.exe + vsm-capture-core.exe`가 기본 2프로세스이고, debug/tap은 기본 OFF인 별도 plane이다.
- Passive Product profile에서는 VSM이 serial read-only로 열고, CSM capability가 요구하는 Arduino CDC session gate 목적의 DTR만 허용하며, RTS/host TX/control cycle/COM-owning lab gateway를 실행하지 않는다.
- Full/Instrumented profile은 bench/lab 전용이며 실차 PASS나 passive product acceptance로 주장하지 않는다.
- `capture.stream/index`만 authoritative truth이며 UI/graph/raw tail/analysis rows는 bounded materialized view로 다룬다.
- live/capture-core 리팩토링은 데이터 흐름도, owner/consumer/drop policy, 기존 owner 위반 검색을 먼저 끝낸 뒤 기능 이동과 구 경로 삭제를 진행한다.
- 금지 경계: `TypedRecordList` UI/analysis/projection 공용 fanout, `AppController` transport raw state 조립, diagnostics payload 기반 runtime state 갱신, UI projection의 timing truth 사용, storage `frameBytes` live view 전달.
- 금지 경계: RuntimeProfile을 우회한 `QIODevice::ReadWrite` serial open, DTR/RTS hard assert, passive profile에서 host_frame/control_cycle/gateway_tcp 실행.

## 4. 작업 라우팅
- capture-core memory/hot path/slab/bounded queue/projection snapshot: `.agents/skills/capture-core-memory/SKILL.md`
- typed board evidence/storage/control gate/protocol semantics: `.agents/skills/typed-evidence/SKILL.md`
- graph truth/performance/overview/detail: `.agents/skills/graph-performance/SKILL.md`
- replay/live/source semantics: `.agents/skills/replay-semantics/SKILL.md`
- build/test/deploy/startup smoke: `.agents/skills/qt-build-verify/SKILL.md`
- AGENTS/.codex/skill boundary/harness structure: `.agents/skills/harness-maint/SKILL.md`
- BRIEF 축소/history 이관/Obsidian link: `.agents/skills/doc-history-rollup/SKILL.md`

핵심 계약 문서:

- VSM-CSM 통합 원칙: `docs/architecture/PROJECT_CONSTITUTION_KO.md`
- typed stream/protocol: `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`, `shared/protocol/typed_stream_v1.md`
- capture-core memory architecture: `docs/architecture/VSM_CAPTURE_CORE_MEMORY_ARCHITECTURE_KO.md`
- core data/view/tap architecture: `docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO.md`
- passive-safe product architecture: `docs/architecture/VSM_PASSIVE_SAFE_2PLUS1_ARCHITECTURE_KO.md`
- passive debug tap product architecture: `docs/architecture/VSM_PASSIVE_DEBUG_TAP_PRODUCT_ARCHITECTURE_KO.md`
- data ownership boundary rules: `docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO.md`
- control evidence: `docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO.md`
- build/verification policy: `docs/ai_harness/BUILD_VERIFY_POLICY_KO.md`

## 5. 보고 형식
작업 결과는 기본적으로 아래를 포함한다.

- 변경 파일
- 핵심 변경점
- 실행한 검증과 결과
- build-risk 또는 미검증 리스크
- 사용자가 바로 확인할 포인트

## 6. 한 줄 원칙
현재 프로젝트 폴더의 실제 파일을 기준으로, 기준본과 invariant를 흔들지 않고, 필요한 문서만 지연 참조한다.

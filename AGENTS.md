# AGENTS.md

이 저장소의 Codex 상위 운영 맵이다. 이 파일은 짧고 안정적으로 유지한다.
상세 절차는 `.agents/skills/`, 상세 근거는 `docs/`, 오래된 이력은 `history/`에 둔다.

## 1. 먼저 읽을 것
항상 아래 순서로 본다.
1. `START_HERE_KO.md` (새 계정 또는 채팅 히스토리 없는 시작)
2. `BRIEF.md`
3. `INDEX.md`
4. 이번 턴과 직접 관련된 skill 또는 문서만 추가
5. 하네스 자체를 바꾸는 턴일 때만 `HARNESS_MASTER_KO.md`

## 2. 이 프로젝트의 목표
목표는 실차/현장 기준에서 신뢰 가능한 CAN monitor / logger / replay / decode / evidence-first control workstation을 완성하는 것이다.

항상 아래를 보존한다.
- VMS live production path는 CSM typed evidence stream 전용
- COM open만으로 board alive로 보지 않고 valid `CAPABILITY` 수신을 기준으로 판단
- live와 replay 의미 분리
- graph truth-first, fixed-axis, peak 보존
- legacy 20-byte packet / CRC8 / DLC / `t_us` wrap은 replay/import 호환으로 보존
- typed board stream 원본 byte를 production truth로 취급
- CAN RX, CAN TX audit, voltage raw, board health/event, control ack evidence type 분리
- host 요청 TX, `CONTROL_ACK`, `CAN_TX_RAW`, feedback은 서로 다른 evidence로 분리
- host 요청 TX는 board가 matching `CAN_TX_RAW`를 낼 때까지 실제 CAN 송신 성공으로 보지 않음
- Windows Qt/CMake build, test, deploy 재현성
- 모델팩/rules/decode 해석 추적 가능성

## 3. 상시 규칙
- `BRIEF.md`는 현재 기준본, 유지 기능, 현재 목표, 즉시 다음 작업만 둔다.
- 과거 실패, 시행착오, 결정 배경은 `history/`로 보낸다.
- routine 코드 수정 턴에서 하네스 재설계를 섞지 않는다.
- `.agents/`와 `.codex/` 수정은 명시적인 하네스 변경 턴에서만 한다.
- 기능을 없애서 UI/성능/빌드 문제를 숨기지 않는다.
- 검증하지 않은 build/run/replay/graph/deploy 성공은 단정하지 않는다.
- 나중에 Runtime으로 뺄 책임이면 `AppController`에 임시 누적하지 말고 boundary/테스트/exit condition을 같은 slice에 포함한다.

## 4. 작업 라우팅
- 빌드/테스트/배포/실행 smoke: `.agents/skills/qt-build-verify/SKILL.md`
- replay/live/source semantics: `.agents/skills/replay-semantics/SKILL.md`
- graph truth/performance/overview/detail: `.agents/skills/graph-performance/SKILL.md`
- typed board evidence/storage/control gate: `.agents/skills/typed-evidence/SKILL.md`
- VMS-CSM 통합 원칙: `docs/architecture/PROJECT_CONSTITUTION_KO.md`
- typed stream/protocol 계약: `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`, `shared/protocol/typed_stream_v1.md`
- control evidence 계약: `docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO.md`
- BRIEF 축소/history 이관/Obsidian 링크: `.agents/skills/doc-history-rollup/SKILL.md`
- AGENTS/.codex/skill boundary/하네스 구조: `.agents/skills/harness-maint/SKILL.md`

## 5. 보고 형식
작업 결과는 기본적으로 아래를 포함한다.
- 변경 파일
- 핵심 변경점
- 실행한 검증과 결과
- build-risk 또는 미검증 리스크
- 사용자가 바로 확인할 포인트

## 6. 한 줄 원칙
현재 프로젝트 폴더의 실제 파일을 기준으로, 기준본과 invariant를 흔들지 않고, 필요한 문서만 지연 참조한다.

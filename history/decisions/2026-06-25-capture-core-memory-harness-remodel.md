# 2026-06-25 Capture-Core Memory Harness Remodel

## Decision
VSM 장시간 high-load memory growth 작업을 `capture-core-memory` 축으로 분리한다.

기존 `typed-evidence`, `graph-performance`, 일반 UI 최적화 작업에 섞지 않는다.
이 축은 live capture hot path ownership, `TypedRecord/QByteArray` copy 제거, bounded queue/pool, writer/analysis/projection fanout, process memory telemetry, 10m/1h plateau 검증을 전담한다.

## Reason
사용자 HIL에서 30초는 비교적 정상처럼 보이지만 10분/1시간에서 UI가 멈추고 process memory가 10GB 이상 계속 증가했다.

현재 코드 구조에는 다음 위험이 남아 있다.

- live parser가 `payload`와 `frameBytes` owning `QByteArray`를 per typed frame 생성.
- `TypedRecordList`가 pipeline thread, `SerialWorker`, writer, raw ledger, analysis, projection, UI critical evidence 경로로 반복 복사.
- 각 runtime queue에는 cap이 있어도 Qt queued event 내부 full batch backlog는 직접 cap/telemetry 밖에 존재할 수 있음.
- UI throttle만으로는 capture truth ownership 문제를 제거하지 못함.

## Expected Gain
- 다음 구현 턴부터 목표가 UI throttle이 아니라 capture-core ownership 재설계로 고정된다.
- 문서/skill routing이 `TypedRecordList` live fanout 제거를 명시하므로, 부분 최적화 후 PASS 선언을 막는다.
- 10m/1h memory plateau 검증이 공식 gate가 된다.

## Changed Surfaces
- `AGENTS.md`
- `HARNESS_MASTER_KO.md`
- `.agents/skills/capture-core-memory/SKILL.md`
- `docs/PROJECT_FOLDER_GUIDE_KO.md`
- `INDEX.md`
- `docs/README.md`
- `docs/architecture/VSM_CAPTURE_CORE_MEMORY_ARCHITECTURE_KO.md`
- `docs/runbooks/VSM_CAPTURE_CORE_MEMORY_VERIFY_KO.md`
- `docs/ai_harness/BUILD_VERIFY_POLICY_KO.md`
- `BRIEF.md`

## Rollback Rule
아래 중 하나가 발생하면 이 harness remodel을 되돌리거나 scope를 줄인다.

- capture-core work가 protocol semantics나 graph renderer work와 혼동된다.
- Codex가 `capture-core-memory` skill 대신 UI throttle만 반복한다.
- 10m/1h memory gate가 보고에서 빠진다.
- root 문서가 다시 임시 계획서로 오염된다.
- skill boundary 때문에 build/replay/typed/graph invariant가 누락된다.

## Human Risk
구조 변경 범위가 크므로 다음 구현은 작은 파일 수정이 아니라 runtime boundary migration으로 진행해야 한다.
HIL 없이 완료 판정을 내리면 같은 문제가 재발할 가능성이 높다.

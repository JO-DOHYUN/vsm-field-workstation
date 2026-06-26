# PROJECT_FOLDER_GUIDE_KO

이 문서는 VSM standalone repository의 폴더 기준이다.
목표는 source, docs, history, runtime data, generated output을 명확히 분리해 Codex와 사람이 같은 기준으로 작업하게 만드는 것이다.

## Root Entry Files
- `AGENTS.md`: 항상 먼저 읽는 상위 운영 맵.
- `START_HERE_KO.md`: 새 계정/새 채팅 진입 문서.
- `BRIEF.md`: 현재 기준본과 즉시 다음 작업.
- `INDEX.md`: 문서 허브.
- `HARNESS_MASTER_KO.md`: 하네스 구조 변경 전용 상위 문서.
- `CMakeLists.txt`, `CMakePresets.json`: Windows Qt/CMake build entry.
- `README_SETUP_KO.md`, `BUILD_FOLDER_USAGE_KO.md`: deploy/install 호환 안내.

Root에는 임시 계획서, handoff, 실패 분석 초안, 긴 prompt를 두지 않는다.
그런 파일은 목적에 따라 `docs/architecture/`, `docs/runbooks/`, `docs/field/`, `history/`로 이동한다.

## Source
- `src/`: Qt/C++ backend, runtime, parser, transport, replay, recorder, model validation.
- `src/backend/transport/`: serial drain, typed parser/pipeline, capture writer, raw ledger, host TX, transport diagnostics.
- `src/backend/analysis/`: timing/value/alarm truth analysis runtime.
- `src/backend/perf/`: performance and UI responsiveness telemetry.
- `qml/`: operator UI pages/components.
- `tests/`: unit/component/QML smoke tests.
- `shared/`: CSM/VSM shared protocol contract.
- `data/`: active model/rules baseline and fixtures.
- `scripts/`: build/deploy/HIL/debug/report helpers.
- `packaging/`: release notice, SBOM, installer hook.

## Docs
- `docs/architecture/`: product constitution, runtime split, capture-core memory architecture, core data/view/tap architecture, protocol/control architecture.
- `docs/interfaces/`: external format and hardware/software data contracts.
- `docs/ai_harness/`: Codex workflow, build verification policy, regression matrix.
- `docs/runbooks/`: build, release, HIL, debug gateway, memory verification, field validation procedures.
- `docs/field/`: dated field/HIL observations and triage reports.
- `docs/quality/`: traceability, release checklist, architecture map, coding rules.

Current truth는 `BRIEF.md`가 우선이다.
과거 보고서나 field report는 현재 판단 근거가 될 수 있지만, 최신 코드와 검증 결과로 재확인해야 한다.

## Harness And Skills
- `.agents/skills/capture-core-memory/`: live capture hot path, memory growth, bounded queue, slab/pool, telemetry, 10m/1h load verification.
- `.agents/skills/typed-evidence/`: typed protocol/evidence semantics and control gate.
- `.agents/skills/graph-performance/`: graph renderer, recent/overview/detail graph performance.
- `.agents/skills/replay-semantics/`: replay/live source meaning and replay fixtures.
- `.agents/skills/qt-build-verify/`: Qt/CMake build, tests, deploy, startup smoke.
- `.agents/skills/harness-maint/`: AGENTS, skills, `.codex`, harness document architecture.
- `.agents/skills/doc-history-rollup/`: BRIEF/history cleanup and Obsidian link maintenance.

## History
- `history/decisions/`: architecture/operation decisions and rollback rules.
- `history/incidents/`: failures, regressions, field incidents.
- `history/changes/`: retired handoffs, prompts, old setup notes.

History는 current truth가 아니다.
현재 작업 판단은 `BRIEF.md`, relevant skill, relevant architecture/runbook 순서로 한다.

## Runtime Data And Generated Output
- `replay_data/`: project-local runtime data root. Source가 아니다.
- `replay_data/logs/`: typed captures and session logs.
- `replay_data/snapshots/`: replay/export snapshot staging.
- `artifacts/`: HIL/debug/report generated artifacts.
- `out/`: CMake build output.
- `.logs/`: local Codex/build/run logs.
- `.ref-replay/`: local reference replay cache.
- `.vs/`, `build/`: local IDE/build output.

Generated capture/log/binary files are not tracked except intentional README/placeholder files.

## Capture-Core Memory Work Placement
- 설계 기준: `docs/architecture/VSM_CAPTURE_CORE_MEMORY_ARCHITECTURE_KO.md`
- 장기 process/data boundary 기준: `docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO.md`
- 검증 기준: `docs/runbooks/VSM_CAPTURE_CORE_MEMORY_VERIFY_KO.md`
- 하네스 결정: `history/decisions/2026-06-25-capture-core-memory-harness-remodel.md`
- 기존 root 임시 계획서는 root에 두지 말고 위 문서로 흡수한다.

## Cleanup Rule
새 문서를 추가할 때는 목적에 맞는 하위 폴더에 둔다.
Root에 새 markdown을 추가해야 하는 경우는 entry document 또는 build/deploy 호환 문서뿐이다.

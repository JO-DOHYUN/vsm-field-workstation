# PROJECT_FOLDER_GUIDE_KO

이 문서는 VSM 단독 repository 폴더를 처음 여는 Codex 계정이 source, docs, history, runtime data를 구분하도록 돕는 폴더 지도다.

## 루트 진입 파일

- `AGENTS.md`: 항상 먼저 읽는 상위 운영 맵
- `START_HERE_KO.md`: 새 계정/메모리 없는 시작점
- `BRIEF.md`: 현재 기준본과 즉시 다음 작업
- `INDEX.md`: 문서 허브
- `HARNESS_MASTER_KO.md`: 하네스 상세 원칙. 하네스 자체를 바꾸는 턴에만 읽는다.
- `CMakeLists.txt`, `CMakePresets.json`: Windows Qt/CMake build entry
- `README_SETUP_KO.md`, `BUILD_FOLDER_USAGE_KO.md`: CMake install/deploy 호환용 짧은 안내 파일

## Source

- `src/`: Qt/C++ backend, runtime, parser, replay, recorder, model validation
- `qml/`: operator UI, replay bar, live/board/control/graph 화면
- `tests/`: unit/component/QML smoke tests
- `shared/`: CSM/VSM 공용 protocol contract
- `data/`: active model/rules baseline and fixtures
- `scripts/`: deploy/build helper scripts
- `packaging/`: release notice, SBOM, installer hook

## Docs

- `docs/architecture/`: product constitution, runtime split, protocol/control architecture
- `docs/interfaces/`: external format and hardware/software data contract
- `docs/ai_harness/`: Codex 작업 운영, 검증 정책, regression matrix
- `docs/runbooks/`: build, release, Obsidian vault 운용 절차
- `docs/quality/`: traceability, release checklist, architecture map, coding rules

현재 기준은 `BRIEF.md`가 우선이고, 세부 근거는 필요한 문서만 지연 참조한다.

## History

- `history/changes/`: 오래된 handoff, setup, prompt, cleanup 기록
- `history/decisions/`: 구조/운영 결정과 rollback 조건
- `history/incidents/`: 장애, 실패, 현장 이슈 기록

과거 문서는 현재 기준이 아니다. 현재 작업 판단에는 `BRIEF.md`, `START_HERE_KO.md`, 관련 `docs/`를 먼저 본다.

## Runtime Data And Generated Output

- `replay_data/`: 프로젝트 내부 runtime data root. source가 아니다.
- `replay_data/logs/`: session logs, typed captures, migrated legacy logs
- `replay_data/snapshots/`: replay/export snapshot staging
- `out/`: CMake build output
- `.logs/`: local Codex/build/run logs
- `.ref-replay/`: local reference replay binary cache
- `.vs/`, `build/`: local IDE/build output

`replay_data`, `.logs`, `.ref-replay`, `artifacts` 내부 실제 capture/log/binary/generated 파일은 git에 넣지 않는다. README 파일만 추적해 폴더 의도를 보존한다.

## 루트 정리 원칙

루트에는 새 계정이 반드시 읽어야 하는 entry 문서와 build/config 파일만 남긴다. 긴 handoff, architect prompt, 오래된 setup 전문은 `history/`나 `docs/architecture/`로 이동한다.

루트에 예외로 남긴 파일:

- `README_SETUP_KO.md`: installer/deploy copy 대상이라 짧은 호환 안내로 유지한다.
- `BUILD_FOLDER_USAGE_KO.md`: installer/deploy copy 대상이라 짧은 호환 안내로 유지한다.

새 문서를 추가할 때는 목적에 맞는 하위 폴더에 둔다. 임시 메모나 긴 작업 기록은 root에 만들지 않는다.

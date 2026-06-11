# CODEX_HARNESS_KO

이 문서는 Codex 앱에서 이 프로젝트 하네스를 운용하는 방법만 다룬다.
상위 규칙은 [[../AGENTS]], 현재 기준은 [[../BRIEF]], 하네스 철학은 [[../HARNESS_MASTER_KO]]가 우선이다.

## 기본 운용
- 시작 시 `BRIEF.md`를 읽어 현재 기준본과 invariant를 먼저 잠근다.
- 기능 코드 작업과 하네스 구조 작업을 섞지 않는다.
- build-risk 파일을 건드리면 가장 작은 관련 build/test를 직접 실행하거나 미검증을 명시한다.
- 과거 실패 기록은 필요할 때만 [[../history/INDEX]]에서 찾는다.
- 루트의 zip, screenshot, `out/`, `.vs/`는 상시 문맥이 아니라 artifact로 본다.

## Codex 앱 전달 기준
기본 결과물은 프로젝트 폴더 직접 수정이다.
patch zip, export bundle, handoff 문서는 사용자가 요청하거나 외부 전달이 필요할 때만 만든다.

## Skill 사용 기준
- build/test/deploy는 [[../.agents/skills/qt-build-verify/SKILL]]
- replay/live/source 의미는 [[../.agents/skills/replay-semantics/SKILL]]
- graph truth/performance는 [[../.agents/skills/graph-performance/SKILL]]
- typed board evidence는 [[../.agents/skills/typed-evidence/SKILL]]
- 문서/history 정리는 [[../.agents/skills/doc-history-rollup/SKILL]]
- 하네스 구조 변경은 [[../.agents/skills/harness-maint/SKILL]]

## 주의
현재 폴더는 VSM 단독 git repository root다. `git status`를 우선 사용하되, generated output은 `.gitignore` 기준으로 source와 분리한다.
성공을 직접 실행 검증하지 못했으면 성공이라고 쓰지 않는다.

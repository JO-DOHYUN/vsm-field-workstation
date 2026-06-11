# 2026-05-29 Self-Contained Onboarding Docs

## Context

새 Codex 계정이 채팅 히스토리 없이 `C:\WORKS\VS\turn81_full_buildfix2` 폴더만 보고도 VSM/CSM 프로젝트의 목적, 기준 경로, 현재 검증 상태, 다음 작업 축을 이해해야 한다.

기존 문서는 일부가 예전 PlatformIO `qt` 경로를 primary workspace로 설명했고, root에는 handoff/prompt류 문서가 섞여 있었다.

## Decision

- 현재 폴더를 active self-contained workspace로 문서화한다.
- `START_HERE_KO.md`를 새 계정 온보딩 진입점으로 추가한다.
- 폴더 역할은 `docs/PROJECT_FOLDER_GUIDE_KO.md`로 분리한다.
- 작업 철학과 runtime-boundary-first 원칙은 `docs/ai_harness/AI_WORKFLOW_PHILOSOPHY_KO.md`로 분리한다.
- 오래된 handoff/prompt류는 `history/changes/` 또는 `docs/architecture/`로 이동한다.
- `README_SETUP_KO.md`와 `BUILD_FOLDER_USAGE_KO.md`는 CMake install/deploy 참조가 있으므로 root 호환 안내 파일로 유지한다.

## Expected Gain

- 새 계정이 첫 턴에 잘못된 upstream 경로에서 작업하는 위험을 줄인다.
- `AppController`에 임시 책임을 더 쌓는 패턴을 다음 작업 축에서 차단한다.
- runtime data와 source를 구분해 replay/log 경로를 현장 운영 기준으로 이해할 수 있게 한다.
- 문서 정리는 Level 0 검증만으로 끝낼 수 있어 코드 검증 토큰을 쓰지 않는다.

## Rollback

다음 조건이면 이 결정을 되돌리거나 보정한다.

- 실제 active workspace가 다시 upstream PlatformIO `qt` 경로로 바뀐다.
- CMake install/deploy가 root compatibility docs 때문에 혼란을 만든다.
- 새 계정 onboarding 문서가 `BRIEF.md`와 다른 현재 기준을 말하기 시작한다.

Rollback 시 `START_HERE_KO.md`, `BRIEF.md`, `INDEX.md`, `AGENTS.md`의 workspace 기준을 함께 수정한다.

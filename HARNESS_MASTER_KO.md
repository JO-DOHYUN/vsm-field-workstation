---
kind: master
scope: harness
updated: 2026-04-23
read_when:
  - harness redesign
  - instruction conflict
  - skill boundary change
  - onboarding
---
# HARNESS_MASTER_KO

이 문서는 하네스 구조 변경 전용 상위 목적 문서다.
routine 코드 수정, 단순 build fix, UI 패치 때는 매번 읽지 않는다.

## 1. 목적
목표는 최소 상시 문맥으로 큰 작업을 안정적으로 수행하는 것이다.
상시 규칙은 작고 느리게 바꾸며, 반복 절차와 상세 근거는 필요할 때만 읽게 분리한다.

## 2. 계층 철학
- `AGENTS.md`: 거의 불변인 운영 규칙, 문서 라우팅, 보고 형식만 둔다.
- `BRIEF.md`: 현재 기준본, 반드시 유지할 기능, 현재 목표, 즉시 다음 작업만 둔다.
- `.agents/skills/*`: 반복 가능한 작업별 절차를 좁은 scope로 둔다.
- `docs/`: 설계 근거, runbook, traceability, interface contract를 둔다.
- `history/`: 오래된 BRIEF, 실패 기록, 결정 배경, incident를 둔다.

## 3. 이 프로젝트에서 중요한 이유
이 앱은 단순 화면 앱이 아니라 실차/현장 관측 도구다.
하네스가 무거워지면 Codex가 오래된 UI 실패 기록, 빌드 로그, 기능 아이디어를 상시 참조해 현재 기준본 판단이 흐려진다.
따라서 현재 truth와 과거 history를 분리하고, replay/graph/typed/build처럼 위험 축이 다른 작업은 별도 skill로 분리한다.

## 4. 변경 판단 순서
하네스를 바꿀 때는 아래 순서로 판단한다.
1. 상위 불변 규칙이면 `AGENTS.md`에 둔다.
2. 현재 기준본이면 `BRIEF.md`에 둔다.
3. 반복 workflow면 `.agents/skills/`에 둔다.
4. 상세 근거면 `docs/`에 둔다.
5. 지난 이력이나 실패 기록이면 `history/`에 둔다.

## 5. 변경 정책
- 하네스 변경은 기능 개발과 섞지 않는다.
- 상위 파일 수정은 최소화한다.
- skill description은 서로 겹치지 않게 적어 implicit trigger 오작동을 줄인다.
- 변경 이유, 기대 효과, rollback 기준은 `history/decisions/`에 남긴다.
- Obsidian 호환을 위해 핵심 md에는 `[[내부 링크]]`를 둔다.

## 6. Rollback 기준
아래 문제가 생기면 이전 구조 또는 더 작은 scope로 되돌린다.
- Codex가 BRIEF를 읽지 않아 기준본을 자주 놓친다.
- skill trigger가 겹쳐 엉뚱한 workflow를 반복 사용한다.
- build/replay/graph/typed 작업에서 필수 invariant가 누락된다.
- 문서 링크가 실제 파일 위치와 맞지 않아 탐색 시간이 늘어난다.

## 7. 연결
- 현재 상태: [[BRIEF]]
- 문서 허브: [[INDEX]]
- 상세 문서 허브: [[docs/README]]
- 이력 허브: [[history/INDEX]]
- 이번 결정: [[history/decisions/2026-04-23-harness-slim-context-split]]

---
kind: master
scope: harness
updated: 2026-06-25
read_when:
  - harness redesign
  - instruction conflict
  - skill boundary change
  - onboarding
---
# HARNESS_MASTER_KO

이 문서는 하네스 구조 변경 전용 상위 목적 문서다.
routine 코드 수정, 단순 build fix, UI patch에서는 읽지 않는다.

## 1. 목적
Codex 작업이 긴 이력과 실패 로그에 끌려가지 않고, 현재 기준본과 직접 관련된 문서만 읽고 안정적으로 작업하게 한다.

하네스는 다음 역할만 가진다.

- 진입 순서 정의
- skill routing 정의
- 현재 기준본과 history 분리
- 검증 ladder와 보고 기준 정의
- 반복 실패가 난 작업축을 독립 skill로 분리

## 2. 계층 철학
- `AGENTS.md`: 짧은 상위 운영 규칙, 읽기 순서, routing, 보고 형식.
- `BRIEF.md`: 현재 기준본, 보존 기능, 현재 목표, 즉시 다음 작업.
- `.agents/skills/*`: 반복 가능한 작업별 절차와 불변조건.
- `docs/`: 설계 근거, runbook, protocol/interface contract.
- `history/`: 오래된 기준본, 실패 기록, 결정 배경, rollback 조건.

## 3. Capture-Core Memory Skill 분리 이유
VSM 장시간 high-load 멈춤/메모리 폭증은 단순 UI 렌더링 문제가 아니라 live capture hot path의 ownership, copy, queue, Qt event backlog 문제일 수 있다.

따라서 아래 작업은 기존 `typed-evidence`나 `graph-performance`에 섞지 않고 `capture-core-memory`로 라우팅한다.

- `TypedRecord/QByteArray` live hot path 제거
- raw byte slab/pool ownership
- bounded queue와 overrun diagnostics
- writer/analysis/projection snapshot fanout
- process memory telemetry
- 10분/1시간 high-load memory plateau 검증

`typed-evidence`는 evidence 의미와 protocol 보존을, `graph-performance`는 renderer/graph 의미 보존을 담당한다.

## 4. 변경 판단 순서
하네스를 바꿀 때는 아래 순서로 판단한다.

1. 상위 불변조건이면 `AGENTS.md`.
2. 현재 기준본이면 `BRIEF.md`.
3. 반복 workflow면 `.agents/skills/`.
4. 설계 근거면 `docs/`.
5. 결정 배경과 rollback이면 `history/decisions/`.

## 5. 변경 정책
- 기능 개발과 하네스 변경을 같은 slice에 섞지 않는다.
- 상위 문서는 짧게 유지하고 세부 절차는 skill로 내린다.
- skill description은 서로 겹치지 않게 쓴다.
- 새 skill을 만들면 `AGENTS.md`, `INDEX.md`, 관련 architecture/runbook, decision history를 함께 갱신한다.
- 문서 링크는 실제 파일 위치와 맞춘다.

## 6. Rollback 기준
아래 문제가 생기면 이전 하네스 구조 또는 더 작은 scope로 되돌린다.

- Codex가 BRIEF를 읽지 않고 과거 이력으로 판단한다.
- skill trigger가 겹쳐 같은 작업에서 다른 불변조건을 적용한다.
- build/replay/graph/typed/capture-core 작업에서 필수 invariant가 누락된다.
- 문서 위치가 실제 파일 구조와 맞지 않아 검색 시간이 늘어난다.
- capture-core memory 작업이 다시 UI throttle 수준으로 축소된다.

## 7. 연결
- 현재 기준본: [[BRIEF]]
- 문서 허브: [[INDEX]]
- 폴더 가이드: [[docs/PROJECT_FOLDER_GUIDE_KO]]
- build 검증 정책: [[docs/ai_harness/BUILD_VERIFY_POLICY_KO]]
- 이번 결정: [[history/decisions/2026-06-25-capture-core-memory-harness-remodel]]

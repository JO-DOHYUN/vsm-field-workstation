# 2026-04-23 Harness Slim Context Split

## Decision
루트 `AGENTS.md`와 `BRIEF.md`를 상시 문맥으로 쓰되, 장문 절차와 이력은 skills/docs/history로 분리한다.

## Why
이 프로젝트는 replay, graph, typed evidence, build/deploy가 각각 다른 invariant를 가진다.
하나의 큰 skill이나 BRIEF에 모두 넣으면 Codex가 오래된 실패 기록과 현재 기준을 혼동할 가능성이 커진다.

## Expected Gain
- AGENTS instruction chain이 얇아진다.
- BRIEF가 현재 truth만 담는다.
- build/replay/graph/typed/harness/doc 작업이 명확한 skill boundary를 가진다.
- Obsidian에서 문서 그래프가 hub 중심으로 연결된다.

## Rollback
기준본 누락, skill trigger 충돌, build-risk 판단 누락이 반복되면 `history/changes/2026-04-23-*`에 보관한 이전 문서를 참조해 더 보수적인 AGENTS/BRIEF 구조로 되돌린다.

## Links
- [[../../AGENTS]]
- [[../../BRIEF]]
- [[../../HARNESS_MASTER_KO]]
- [[../../INDEX]]

---
name: harness-maint
description: Use only when modifying AGENTS.md, .codex configuration, skill boundaries, HARNESS_MASTER_KO.md, or the harness document architecture. Do not use for normal feature development, build fixes, graph tuning, or replay semantics.
---

# harness-maint

## Inputs
- `HARNESS_MASTER_KO.md`
- `AGENTS.md`
- current skill list
- explicit reason for changing the harness

## Rules
- Change the smallest upper-layer surface that solves the problem.
- Keep AGENTS short and stable.
- Keep BRIEF current-state only.
- Split reusable workflows into narrowly described skills.
- Record reason, expected gain, and rollback rule in `history/decisions/`.
- Do not redesign the whole tree unless the user explicitly asks for harness restructuring.

## Output
- changed harness files
- skill boundary changes
- docs/history movement
- rollback condition
- remaining human risks

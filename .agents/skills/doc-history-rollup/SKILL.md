---
name: doc-history-rollup
description: Use when BRIEF must be shortened, stale notes should move to history, Obsidian links need repair, or current-state docs need a non-code cleanup. Do not use for build fixes, graph renderer work, or harness boundary redesign.
---

# doc-history-rollup

## Rules
- `BRIEF.md` keeps current truth only.
- Old failure notes, long addenda, and stale decisions move to `history/`.
- Hub notes stay shallow and use `[[internal links]]`.
- Do not invent current verification; preserve verified vs unverified distinction.
- Do not mix doc cleanup with functional code changes unless explicitly requested.

## Workflow
1. Read `BRIEF.md` and `INDEX.md`.
2. Classify each note as current truth, detailed docs, or history.
3. Move stale content to `history/changes`, `history/decisions`, or `history/incidents`.
4. Add backlinks from hubs where useful.
5. Report what moved and what remains current.

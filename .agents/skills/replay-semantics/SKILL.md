---
name: replay-semantics
description: Use when modifying replay load/play/pause/seek, issue navigation, live-vs-replay source selection, analysis source state, or replay fixtures. Do not use for graph renderer-only work or build-only fixes.
---

# replay-semantics

## Invariants
- live and replay states remain separate.
- replay cursor, seek, speed, pause, issue focus, and analysis source must not overwrite live state.
- timing issues and value/alarm meaning remain separate.
- legacy 20-byte replay semantics remain compatible while typed replay is added side-by-side.

## Workflow
1. Read `BRIEF.md`.
2. Identify source ownership: live, replay, held UI, or exported snapshot.
3. Define acceptance before edits: seek behavior, issue focus, analysis tables, operator summary, graph cursor impact.
4. Add or update component tests when state transitions change.
5. Run the smallest relevant `ctest`; use `qt-build-verify` only if build/deploy validation becomes the main task.

## Red Flags
- replay seek mutates live counters
- pause/hold hides source identity
- issue navigation rebuilds unrelated graph truth
- typed replay replaces legacy replay instead of coexisting

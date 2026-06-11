---
name: graph-performance
description: Use when modifying recent graph, full-range overview graph, detail rebuild, nested zoom, axis layout, graph projection, renderer cost, or graph UI density. Do not use for replay logic unless graph behavior is directly affected.
---

# graph-performance

## Invariants
- Graph is truth-first.
- Display optimization must not change raw truth.
- Fixed-axis meaning and peak preservation remain intact.
- Recent-window graph and full-range overview graph are separate tools.
- Full-range overview remains fixed while replay moves only cursor/selection overlays.
- Detail graph rebuilds from selected interval; it is not just a cropped overview bitmap.

## Workflow
1. Read `BRIEF.md`.
2. State whether the change touches truth, projection, renderer, layout, or interaction.
3. Protect selection history: nested zoom, back one step, root, clear.
4. Keep graph turns narrow; avoid unrelated settings/operator guidance rewrites.
5. Verify at 100% layout first; 90/110 are compatibility checks.

## Red Flags
- peak loss from downsampling
- axis label cutoff at 100%
- overlay swallowing graph content
- full overview rebuilding continuously during replay
- side signal list disappearing during layout fixes

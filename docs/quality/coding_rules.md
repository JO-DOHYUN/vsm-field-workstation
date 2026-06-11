# Coding Rules

## Core Rules
- preserve raw bridge semantics
- do not merge live and replay meaning
- do not change graph truth for display convenience
- keep fixed-axis and peak semantics stable

## Qt / C++ Rules
- keep new runtime helpers narrow and explicit
- prefer `QStandardPaths` for persistent app data
- use `QLoggingCategory` for engineer-facing logs
- keep `Q_PROPERTY` surface stable while internal split is in progress

## File I/O Rules
- prefer atomic temp-to-final writes for logs/meta/model snapshots in future phases
- do not silently ignore invalid external model packs

## Threading Rules
- serial ingest and replay pacing must remain explicit ownership domains
- worker-thread shutdown and reconnect behavior must be documented before broad refactors

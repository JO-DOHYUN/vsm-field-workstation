# 2026-06-26 Core Data/View/Tap Architecture

## Reason
The VSM live memory storm showed that thread splits and UI throttling are not enough when capture truth, display snapshots, diagnostics, and debug traces can still share high-rate Qt/event ownership boundaries.

## Decision
The long-run VSM target is Core-owned Data Plane + View Query Plane + Optional Debug Tap Plane.

- Core owns COM/USB, parser validation, append-only `capture.stream/index`, raw ledger, analysis truth, and bounded materialized views.
- UI receives only cheap `ViewChanged` notifications and queries bounded `ViewSnapshot` data with `GetView(view_name, since_seq, limit)`.
- Debug/gateway/tap is default OFF, non-blocking, fixed-cap, and cannot replace production capture evidence.

## Expected Gain
Capture truth becomes isolated from UI render cost, diagnostics storms, graph rebuilds, and optional debug tooling. UI can lag or reconnect without becoming the owner of raw typed stream truth.

## Rollback Condition
If the architecture split causes capture parity regressions, 10m/1h memory plateau failures, or inability to preserve `capture.stream/index` truth, roll back to the previous in-process capture-core boundary while keeping the bounded queue and telemetry gates.

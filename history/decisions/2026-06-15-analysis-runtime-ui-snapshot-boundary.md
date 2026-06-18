# 2026-06-15 AnalysisRuntime and UI Snapshot Boundary

## Decision

VSM live timing/value/alarm truth is calculated by `AnalysisRuntime`, not by QML, `LiveProjectionRuntime`, or coalesced live display frames.

`SerialWorker` feeds every accepted typed `CAN_RX_RAW` into `AnalysisRuntime` before UI projection. The runtime keeps bounded state keyed by `bus + canId + ext + rtr`, then emits snapshot rows and diagnostics to `AppController`. QML renders these snapshot rows and must not consume the raw stream.

## Why

The previous high-load mitigation reduced live display volume, but coalesced display frames could distort factual timing. A 20 ms message could appear as a slower period if intermediate frames were consumed only by the display/projection path. For a monitoring workstation, performance must never change timing/value/alarm/DLC/control evidence.

## Consequences

- Normal live mode remains in-memory state-update based. It does not write a DB/file unless logging or debug gateway is explicitly enabled.
- UI may be delayed or bounded, but truth analysis must consume accepted frames or report `truth_loss` / `analysis_overrun`.
- `LiveTruthRuntime` remains display/latest-state coalescing and diagnostics only. It is not the owner of timing/value/alarm truth.
- `AppController` still contains replay and legacy analysis code during the transition. The next cleanup is to move replay parity and table ViewModel ownership fully behind the same `AnalysisRuntime` contract.

## Rollback

Rollback only if `analysis_runtime_foundation`, `serial_worker_typed_ingest`, or `qml_shell_smoke` fails in a way that cannot be fixed without risking field logging. If rolled back, keep the policy rule that sampled/coalesced display rows cannot drive truth calculations.

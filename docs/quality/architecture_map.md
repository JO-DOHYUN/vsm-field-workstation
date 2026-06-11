# Architecture Map

## Current Runtime Ownership
- `AppController`
  - still owns live transport, replay orchestration, analysis refresh, graph cache/state, session load/save, operator summary
- `SessionManager`
  - thin key/value persistence wrapper
- `ReplayEngine`
  - replay pacing and cursor mechanics
- `SerialWorker`
  - worker-thread serial ingest path
- `GraphViewportItem`
  - graph rendering surface

## Phase-2 Target Split
- `TransportRuntime`
- `ReplayRuntime`
- `AnalysisRuntime`
- `GraphRuntime`
- `OperatorRuntime`
- `UiStateStore`

## Current Phase-1 Additions
- `BuildMetadata`: compile-time build identity surfaced to runtime/logging
- `RuntimePaths`: standard app/config/log/snapshot roots
- `AppLogging`: structured session log sink
- `ModelPackValidator`: JSON-level model-pack gate before loader acceptance

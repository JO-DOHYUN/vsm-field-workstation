---
name: capture-core-memory
description: Use when modifying VSM live capture hot path, memory growth, raw byte/slab ownership, bounded queues, typed frame fanout, writer/analysis/projection handoff, process memory telemetry, or 10m/1h high-load memory plateau verification. Do not use for protocol semantics-only changes, replay-only changes, graph renderer-only changes, or cosmetic UI work.
---

# capture-core-memory

## Purpose
VSM live high-load memory work is capture-core ownership work, not ordinary UI throttling.
Use this skill when the task can affect long-run memory, queue backpressure, raw typed stream ownership, parser output, writer handoff, analysis handoff, or UI projection fanout.

## Non-Negotiable Invariants
- Production truth is the accepted CSM typed stream raw bytes.
- Live hot path must not pass full `TypedRecordList` through Qt queued signals.
- Live parser output must not create both `payload` and `frameBytes` owning `QByteArray` per frame.
- Capture truth, analysis input, raw ledger, and UI projection must have separate ownership boundaries.
- Every queue/pool in the live path must have a fixed cap, high-water telemetry, and explicit overrun diagnostics.
- UI projection may sample/drop display data only; analysis truth must consume every accepted CAN_RX or report `truth_loss` / `analysis_overrun`.
- `AppController` must not own full capture truth, unbounded raw history, unbounded typed records, parser backlog, or writer backlog.
- Graph optimizations must preserve truth-first and peak semantics.
- A 30s pass is not enough for this skill. The acceptance gate includes 10m and 1h memory plateau behavior when hardware/HIL is available.

## Required Reading
1. `BRIEF.md`
2. `docs/architecture/VSM_CAPTURE_CORE_MEMORY_ARCHITECTURE_KO.md`
3. `docs/architecture/VSM_TRUTH_FIRST_LOAD_ARCHITECTURE_KO.md`
4. `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`
5. `docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO.md`
6. `docs/ai_harness/BUILD_VERIFY_POLICY_KO.md`

## Mandatory Refactor Order
1. Write the data-flow path being changed.
2. Define owner, consumer, and drop policy for each data type.
3. Search the existing code for owner violations before editing behavior.
4. Add boundary DTOs/descriptors before moving functionality.
5. Move the function behind the new owner boundary.
6. Delete the old route in the same slice unless explicitly listed as transitional debt.
7. Add a regression guard: unit test, integration test, or `scripts/check_vsm_boundary_rules.py` result.

Do not start with a patch that only moves methods/classes. A slice that leaves
the old owner/data-flow path active is not complete.

## Architecture Target
The live path must converge to the Core-owned Data Plane / View Query Plane / Optional Debug Tap Plane architecture in
`docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO.md`.

Logical target:

```text
Core-owned Data Plane
  SerialDrainRuntime
    -> bounded raw ingress queue
    -> ByteSlabPool / FrameRef parser
    -> CaptureCoreRuntime
         -> CaptureWriterRuntime
         -> RawLedgerRuntime
         -> AnalysisWorkerRuntime
         -> MaterializedViewStore

View Query Plane
  CoreClientRuntime
    -> ViewChanged only
    -> GetView(view_name, since_seq, limit)
    -> AppController snapshot facade
    -> QML

Optional Debug Tap Plane
  debug/gateway/tap
    -> default OFF
    -> non-blocking
    -> fixed cap and drop counters
```

Current transition structure:

```text
SerialDrainRuntime
  -> bounded raw ingress queue
  -> ByteSlabPool / FrameRef parser
  -> CaptureCoreRuntime
       -> CaptureWriterRuntime
       -> RawLedgerRuntime
       -> AnalysisWorkerRuntime
       -> ProjectionSnapshotRuntime
  -> AppController snapshot facade
  -> QML
```

`TypedRecord` remains allowed for replay/import/offline tools, but not as the live production hot-path fanout object.
`capture.stream` and `capture.index` are the only authoritative production truth. Raw ledger, latest state, analysis
snapshots, graph buckets, and transport summaries are derived materialized views.

## Implementation Rules
- Run `py -3 scripts/check_vsm_boundary_rules.py --mode transition` before broad live-path edits and use the output as the owner-debt checklist.
- Replace per-frame owning objects with `TypedFrameRef`, `CanRxLite`, and critical evidence lite DTOs.
- Prefer descriptor queues over payload-copy queues.
- Use batch writes for `capture.stream`, `capture.index`, raw ledger segment/index.
- Keep `SerialDrainRuntime::readyRead` read-only and return quickly.
- Keep writer and analysis dispatch bounded; if full, mark capture/analysis invalid explicitly.
- Use single-flight projection snapshots for UI. If a snapshot is pending, replace/coalesce latest data instead of enqueueing another full event.
- Prefer `ViewChanged` + bounded `GetView(...)` query over push-streaming full snapshots to the UI.
- Materialized view queries must return already-built bounded views; they must not perform full capture scans in response to UI demand.
- Debug/gateway/tap paths must be default-off and must not run in the normal live production path.
- Add telemetry before claiming improvement.
- Do not hide memory growth by reducing evidence fidelity.

## Hard Forbidden Boundaries
- Do not pass `TypedRecordList` as a shared live object between UI, analysis, projection, raw ledger, and capture writer.
- Do not let `AppController` assemble transport raw state or queue ownership state from diagnostics payloads.
- Do not update runtime state from diagnostics payloads; diagnostics are display/report evidence only.
- Do not use UI projection as timing truth, analysis truth, or CAN loss truth.
- Do not pass storage `frameBytes` through the live display path.

## Required Telemetry
- process private bytes and working set
- raw ingress used/max/capacity/overrun
- slab pool used/max/capacity/alloc fail
- typed parser buffered bytes/max, frames, CRC/length/version/seq counters
- capture writer queue used/max/overrun/write max
- raw ledger queue used/max/write max
- analysis queue used/max/overrun/truth loss
- projection pending/coalesced/dropped/snapshot rate
- graph live points/buckets/memory estimate
- UI event loop p95/max/stall counters

## Verification Ladder
- Level 1: isolated data structure or parser ref change. Run matching unit tests.
- Level 2: live transport/core boundary change. Release build plus targeted typed/capture/serial/analysis tests.
- Level 3: broad capture-core slice. Release build, full `ctest`, startup smoke, and artifact-producing memory test if hardware is unavailable.
- HIL gate: 30s smoke, 10m load, and 1h load. Report memory slope and plateau, not only PASS/FAIL.

## Red Flags
- `TypedRecordList` remains the main live fanout object.
- A queue cap exists but Qt queued events can still accumulate full batches outside telemetry.
- `AppController` receives full typed record batches under high load.
- Writer queue is bounded but a pre-writer handoff queue is not.
- Raw ledger returns large committed frame lists to UI faster than UI can apply them.
- Graph active mode appends raw points without a hard memory cap.
- UI consumes raw/typed stream directly instead of querying derived views.
- A UI view query performs full replay/capture scan on demand.
- Debug gateway/profiler writer code runs in normal live production mode.
- A test passes for 30s but no 10m/1h memory evidence exists.

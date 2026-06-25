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
5. `docs/ai_harness/BUILD_VERIFY_POLICY_KO.md`

## Architecture Target
The live path must converge to this structure:

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

## Implementation Rules
- Replace per-frame owning objects with `TypedFrameRef`, `CanRxLite`, and critical evidence lite DTOs.
- Prefer descriptor queues over payload-copy queues.
- Use batch writes for `capture.stream`, `capture.index`, raw ledger segment/index.
- Keep `SerialDrainRuntime::readyRead` read-only and return quickly.
- Keep writer and analysis dispatch bounded; if full, mark capture/analysis invalid explicitly.
- Use single-flight projection snapshots for UI. If a snapshot is pending, replace/coalesce latest data instead of enqueueing another full event.
- Add telemetry before claiming improvement.
- Do not hide memory growth by reducing evidence fidelity.

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
- A test passes for 30s but no 10m/1h memory evidence exists.

# VSM Truth-First Load Architecture

## Core Rule

VSM is a monitoring/evidence workstation. Performance work must not change factual values.

- Do not calculate timing/value/alarm/DLC/control evidence from sampled UI projection.
- Reduce display volume only.
- If display is sampled, delayed, or coalesced, show it as display degradation.
- If accepted truth cannot be consumed, report `truth_loss` or an explicit analysis overrun.

## Implemented Runtime Boundary

```text
CSM typed stream
  -> SerialWorker I/O thread
  -> TypedIngressRuntime
       - parses typed frames
       - appends capture.stream when logging is active
       - emits accepted typed records
  -> AnalysisRuntime
       - consumes every accepted CAN_RX_RAW before UI projection/coalescing
       - key = bus + canId + ext + rtr
       - keeps latest state per key
       - calculates timing/value/alarm/DLC evidence outside QML rendering
       - keeps bounded histograms/counters/state only; no raw frame list
       - emits bounded cadence snapshot rows and diagnostics to AppController
       - reports accepted frames, state cap, truth_loss, and analysis_overrun
  -> AppController snapshot facade
       - live timing/value/alarm rows are rendered from AnalysisRuntime snapshots
       - replay keeps the same AnalysisRuntime core as a parity foundation
       - replay and live remain separate sources
  -> QML models
```

Display-only path:

```text
TypedIngressRuntime
  -> LiveLatestRuntime
       - coalesces latest CAN_RX by bus+id+ext+rtr for lightweight display/graph state only
       - never feeds timing/value/alarm truth calculations
       - reports observed/emitted/coalesced/pending/truth_loss counters
  -> LiveProjectionRuntime
       - coalesces recent visible CAN_RX rows
       - samples routine display/control evidence for UI load only
       - reports projected/sampled/dropped/pending counters
  -> AppController recent frame/graph projection
  -> QML display
```

Debug-only crash evidence path:

```text
CSM COM7
  -> scripts/vsm_debug_gateway.py
       - owns serial port
       - writes gateway_capture.stream first
       - writes gateway_capture.index.jsonl/events/meta
       - forwards same bytes over localhost TCP
  -> VSM SerialWorker tcp:// endpoint
  -> normal typed ingest/truth/projection/storage path
```

Normal operation does not run the gateway process or gateway writer code.

Debug/performance diagnostics path:

```text
Debug mode ON only
  -> PerformanceProbeRuntime
       - records module timing/call/backlog counters in-process
       - OFF state is a no-op fast path
       - exported through Settings debug panel and analysis snapshot
  -> scripts/vsm_verify.py
       - launches official HIL/debug/report scripts as external processes
       - writes artifacts/vsm_verify/<run_id>/result.json
```

The debug gateway is raw serial evidence. It is not a UI/internal bottleneck profiler.
The performance probe is the UI/backend bottleneck profiler. It is not a replacement
for final capture validation.

## Diagnostics

`TransportSession` separates the causes:

- `capture_storage`: VSM typed capture writer state.
- `typed_parser`: typed parser CRC/length/version/sequence counters.
- `host_tx_queue`: VSM control downlink queue/backpressure.
- `board_health`: CSM CAN drop/FIFO/health age.
- `csm_uplink`: extended `BOARD_HEALTH` USB/segment counters. Ring clear,
  cleared bytes, enqueue failure, and segment enqueue failure are truth-loss
  evidence. Backpressure and MCP drain-budget hits are transport-risk evidence.
- `truth_analysis`: accepted truth consumption, state cap, snapshot, `truth_loss`, and `analysis_overrun`.
- `live_latest`: display-side latest-state coalescing counters; not timing/value/alarm truth.
- `live_projection`: display sampling/drop/backlog counters.
- `live_delay`: UI/live frame freshness.

These rows must not be merged. A projection drop is not a parser failure, and it is not CSM CAN loss.

## Memory/Load Model

The app must not keep unbounded raw frame history in memory.

- Truth analysis is state-update based, not list-append based.
- The long-term key set is bounded by observed `bus+id+ext+rtr` combinations, not by frame count.
- UI recent rows and graph display may be bounded/sampled.
- Logging, when active, writes accepted typed frame bytes to `capture.stream.part` and finalizes on stop.
- Typed capture storage buffers stream/index writes to reduce syscall pressure, while preserving record order, stream bytes, index entry format, and logical offsets.
- Finalized typed captures include `capture.diagnostics.json` with live parser
  counters from capture time. Replay diagnostics must show it separately from
  reparse counters so raw byte loss and final stream integrity are not conflated.
- Debug gateway raw stream recording is separate and opt-in.
- PerformanceProbeRuntime is debug-mode only. Normal mode must not build per-frame
  strings or write profiler logs in the hot path.

## Current Verification Targets

Automatic gates:

- Release build.
- Full `ctest` current count.
- Release exe startup smoke.
- `analysis_runtime_foundation` verifies all-frame analysis consumption, bus-aware keys, DLC preservation, overrun diagnostics, and snapshot diff.
- `transport_runtime_foundation` verifies `LiveLatestRuntime` bus-aware coalesced snapshots.
- `app_controller_log_flow` verifies live same-CAN-ID bus0/bus1 separation.
- `qml_shell_smoke` verifies truth-analysis and projection diagnostics load without QML errors.
- `performance_probe_runtime` verifies debug profiler no-op/record/snapshot behavior.
- `scripts/vsm_verify.py` is the official launcher catalog for user-route HIL,
  analysis-truth HIL, control smoke, debug gateway, and latest-capture reports.

Hardware/user-route gate:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --debug-gateway --port COM7 --duration 30 --no-api-load
py -3 scripts\hil_vsm_user_route_stress.py --debug-gateway --port COM7 --duration 30 --pcan-rate 1500 --kvaser-rate 1500 --id-count 64
```

PASS is allowed only from the actual VSM route, not a direct COM reader.

## Remaining Risks

- `AnalysisRuntime` is now the truth calculation owner for live snapshots. If future decode/rule/alarm work becomes heavier, it must remain bounded and measured.
- `LiveLatestRuntime` remains display/latest-state coalescing only. It must not become the source for timing/value/alarm truth.
- Snapshot delivery may be delayed under load; that is acceptable only when truth consumption counters remain clean.
- Replay/live analysis must keep using the same factual keys and semantics to avoid field/replay mismatch.
- Debug gateway can preserve raw serial evidence during a VSM crash, but it cannot survive PC power loss or physical USB disconnect.

# VSM Data Ownership Boundary Rules

## Purpose
This document is the mandatory boundary contract for VSM live-path refactors.
The goal is to stop file-only refactors that leave the same ownership and data
flow behind different class names.

Any capture-core/live-path change must define data flow, owner, consumer, and
drop policy before moving code.

## Required Refactor Order
1. Draw the data-flow path being changed.
2. Define owner, consumer, and drop policy for every data type crossing that path.
3. Search the existing code for owner violations.
4. Add boundary DTOs or descriptors first.
5. Move the function behind the new boundary.
6. Delete the old route.
7. Add a test or static boundary check that prevents the old route from returning.

Skipping steps 1-3 is not allowed for live capture, typed evidence, process
boundary, UI projection, analysis, graph, raw ledger, or diagnostics work.

## Production Data Flow
```text
Passive Product profile
  -> serial read-only, DTR only as declared CDC session gate, RTS no-touch, host TX/control/gateway off
CSM typed bytes
  -> Core-owned Data Plane
       SerialDrainRuntime
       RawIngressQueue / ByteSlabPool
       TypedFrameParserRuntime
       CaptureWriterRuntime
       RawLedgerRuntime
       AnalysisWorkerRuntime
       MaterializedViewStore
  -> View Query Plane
       ViewChanged cheap notification
       GetView(view_name, since_seq, limit)
       AppController view facade
       QML bounded models
  -> Optional Debug Tap Plane
       default OFF
       vsm-debug-tap.exe Core IPC sidecar
       fixed-cap non-blocking tap
```

The only authoritative production truth is `capture.stream` plus
`capture.index`. Everything shown in the UI is a bounded derived view.

## Data Ownership Table
| Data type | Owner | Allowed consumers | Drop policy |
| --- | --- | --- | --- |
| Raw USB/CDC bytes | Core drain/runtime | Typed parser only | Queue full is fatal host drain overrun. Silent drop forbidden. |
| Accepted typed frame bytes | Capture writer/core storage | Capture writer, replay/import tooling | Capture queue full invalidates capture and records fatal diagnostic. |
| `TypedFrameRef`/descriptor | Core parser/capture runtime | Writer, raw ledger, evidence extraction | Descriptor queue full must be explicit overrun. |
| CAN RX analysis input | Core analysis runtime | Analysis only | Every accepted CAN_RX must enter analysis or increment `analysis_overrun`/`truth_loss`. |
| UI live latest | Materialized view store | UI query client | Display coalesce/drop allowed with display-only counters. |
| Raw ledger tail | Raw ledger/runtime view store | UI query client, replay tools | Tail is bounded; committed storage remains truth. |
| Transport diagnostics | Runtime owner producing the counter | UI display only | Diagnostics may display state but must not become the state owner. |
| Debug trace/tap | Optional debug tap process/runtime | Debug tools only | Drop allowed only with debug drop counter; production path must not block. |
| Runtime profile | `RuntimeProfile` | SerialDrainRuntime, Core runtime, IPC, Core client, UI display | Passive default is read-only/session-only DTR/no-TX. Full/lab cannot be field passive acceptance. |

## Hard Forbidden Rules
- Do not pass `TypedRecordList` as a shared live object between UI, analysis,
  projection, raw ledger, and capture writer.
- Do not let `AppController` assemble transport raw state or queue ownership
  state from ad-hoc diagnostics payloads.
- Do not update runtime state from diagnostics payloads. Diagnostics are
  read-only evidence for UI/reporting.
- Do not use UI projection as timing truth, analysis truth, or CAN loss truth.
- Do not pass storage `frameBytes` through the live display path.
- Do not push full materialized snapshots to UI at high rate. Use
  `ViewChanged` plus bounded `GetView(...)`.
- Do not run debug/gateway/profiler writers in normal production mode.
- Do not use the COM-owning lab gateway as Passive Product diagnostics.
- Do not let `vsm-debug-tap.exe` own COM, send host TX/control, or update Core state.
- Do not open production serial as `QIODevice::ReadWrite` outside `RuntimeProfile`.
- Do not hard-assert DTR/RTS in Passive Product.
- Do not pass `host_frame`, `control_cycle`, or `gateway_tcp` through Passive Product IPC/Core gates.

## Allowed Transitional Exceptions
The current repository is still migrating toward the final 2+1 architecture.
Temporary exceptions are allowed only when all of the following are true:

- The exception is listed by `scripts/check_vsm_boundary_rules.py --mode transition`.
- The code path is bounded and exposes high-water/overrun counters.
- The next owner-removal target is clear in `BRIEF.md` or the active task.
- The exception does not claim final 10m/1h memory-pass status.

## Boundary Scan
Run this before and after capture-core/live-path refactors:

```powershell
py -3 scripts/check_vsm_boundary_rules.py --mode transition
```

Run strict mode only when closing the migration:

```powershell
py -3 scripts/check_vsm_boundary_rules.py --mode strict
```

Transition mode reports current architectural debt without failing the build.
Strict mode fails if live production forbidden patterns still exist.

## Completion Gate
A 2+1 architecture slice is not complete until:

- old push-stream route is deleted or listed as a transitional exception;
- owner/consumer/drop policy is visible in code or this document;
- UI receives only bounded view snapshots or view query results;
- capture truth can continue while UI is slow, disconnected, or frozen;
- memory plateau is proven by 30s, 10m, and 1h evidence when HIL is available.

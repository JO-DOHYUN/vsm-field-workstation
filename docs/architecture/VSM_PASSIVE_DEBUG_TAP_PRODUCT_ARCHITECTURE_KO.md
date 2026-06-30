# VSM Passive Debug Tap Product Architecture

## Goal
Passive Product mode must stay vehicle-impact-free while still allowing field
diagnosis of USB lifecycle, CSM session evidence, Core capture state, and UI/Core
separation. The debug plane is therefore productized as a non-owning sidecar:
`vsm-debug-tap.exe`.

This replaces the old field use of `vsm_debug_gateway.py`. The gateway still
exists only for Full/Instrumented lab work because it owns the COM port.

## Process Contract

```text
Passive Product normal:
  vsm-ui.exe
  vsm-capture-core.exe --profile passive_product

Passive Product diagnostics:
  vsm-ui.exe
  vsm-capture-core.exe --profile passive_product
  vsm-debug-tap.exe --server <core-ipc> --out-dir <artifact>

Full/Instrumented lab only:
  vsm_debug_gateway.py owns COM and forwards TCP
```

## Ownership

| Data | Owner | Normal UI | Debug Tap | Drop policy |
| --- | --- | --- | --- | --- |
| USB/CDC bytes | Core drain | no direct access | no direct access | raw queue overrun invalidates capture |
| accepted typed bytes | Core capture writer | no direct access | no direct access | writer overrun invalidates capture |
| product views | Core materialized view store | bounded query | bounded query | view notification may coalesce/drop |
| debug trace JSONL | Debug Tap | no access | owner | tap loss affects debug only |
| lab raw gateway bytes | Lab gateway | not passive | lab only | not accepted as passive PASS |

## Core Responsibilities
Core keeps only data needed for production truth and bounded product views:

- serial read-only ownership;
- typed SOF/length/CRC/seq validation;
- append-only `capture.stream/index`;
- `profile_status`, `transport_summary`, `capture_progress`, `core_health`,
  `fatal_diagnostics`, `analysis_snapshot`, `live_latest`, `decoded_can_tail`,
  `graph_bucket`, and `control_audit` materialized views;
- passive safety gates for host TX, control, gateway TCP, DTR/RTS policy.

Core must not:

- start the COM-owning gateway in Passive Product;
- create per-frame debug JSON/string records in the hot path;
- parse debug-only meaning that is not needed for production views;
- let debug tap backpressure enter drain, parser, capture writer, analysis, or UI.

## Debug Tap Responsibilities
`vsm-debug-tap.exe` is a read-only IPC client of Core.

It records:

- `debug_tap.ready.json`
- `debug_tap_trace.jsonl`
- `debug_tap_summary.json`

`debug_tap_summary.json` is updated during heartbeat and finalized on normal
stop, app quit, IPC disconnect handling, or object destruction. This is required
so USB/reconnect failures still leave a sidecar summary even when the operator
stops capture soon after the disturbance.

It subscribes to `ViewChanged` and periodically queries bounded snapshots:

- `profile_status`
- `core_health`
- `transport_summary`
- `capture_progress`
- `fatal_diagnostics`
- optional bounded deep views: `analysis_snapshot`, `live_latest`,
  `raw_ledger_tail`, `control_audit`

It never opens COM/USB, never sends host TX/control, and never updates Core state.
It may drop or coalesce debug view requests, but that loss is debug-only and
must never affect Core drain, capture, analysis, or UI view ownership.

## USB Instability Evidence
USB plug/unplug diagnosis requires aligning three evidence sources:

1. Core transport lifecycle:
   - serial open/close/error
   - drain queue overrun
   - capture active/invalid/finalized
   - profile and DTR/RTS policy
2. CSM typed evidence:
   - `CAPABILITY` passive policy
   - `BOARD_HEALTH` USB/session/passive counters
   - `BOARD_EVENT` `USB_CDC_SESSION_OPEN`
   - `BOARD_EVENT` `USB_CDC_SESSION_CLOSE` reported on next open
   - `BOARD_EVENT` `USB_CDC_DTR_CHANGE`
   - `BOARD_EVENT` `USB_HOST_ABSENT_CAN_DISCARD_SUMMARY`
   - `BOARD_EVENT` `MCP_PASSIVE_MODE_READBACK`
   - `BOARD_EVENT` `MCP_PASSIVE_MODE_VIOLATION`
   - `BOARD_EVENT` `MCP_TXREQ_VIOLATION`
   - `BOARD_HEALTH v7` host-absent discard/passive readback counters
   - MCP error/status events
3. External physical evidence when required:
   - CAN analyzer error counters
   - oscilloscope/CANH/CANL disturbance
   - power/ground/transceiver reset behavior

Software can prove the VSM/CSM logical sequence and counters. It cannot alone
prove analog CANH/CANL disturbance while the board is unpowered or disconnected.

## UI Contract
The top-bar diagnostics button starts:

- Passive Product: `vsm-debug-tap.exe`
- Full/Instrumented: lab `vsm_debug_gateway.py`

The UI must label this as diagnostics/tap in Passive Product, not as gateway.
Stopping passive diagnostics must not disconnect Core transport.

## Acceptance

- Normal passive run has exactly two processes: UI + Core.
- Diagnostics enabled has exactly three processes: UI + Core + Debug Tap.
- Debug Tap process does not own COM and does not change Core profile.
- Debug Tap artifact files appear and contain view lifecycle snapshots.
- Slow or stopped Debug Tap cannot increase Core drain/capture/analysis queues.
- Passive UI blocks lab gateway and control/host TX paths.

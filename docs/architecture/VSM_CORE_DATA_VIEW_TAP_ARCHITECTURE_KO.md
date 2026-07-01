# VSM Core Data / View Query / Debug Tap Architecture

## Architecture Summary

VSM 제품 구조는 단순한 파일 분리가 아니라 소유권 분리다.

```text
CSM typed evidence bytes
  -> Core-owned Data Plane
       SerialDrainRuntime
       TypedFrameParserRuntime
       CaptureWriterRuntime
       AnalysisRuntime
       DecodedTailRuntime
       MaterializedViewStore
  -> View Query Plane
       ViewChanged
       GetView
       AppController facade
       QML bounded models
  -> Optional Debug Tap Plane
       vsm-debug-tap.exe
       Core IPC read-only snapshots
```

## Core-Owned Data Plane

Core is the only owner of COM/USB and authoritative capture truth.

Responsibilities:

- open serial according to `RuntimeProfile`;
- drain raw typed bytes;
- validate SOF/length/CRC/typed sequence;
- append accepted typed frames to `capture.stream/index`;
- classify parser/storage/drain faults;
- feed analysis with every accepted CAN RX frame or report `analysis_overrun`;
- build bounded materialized views;
- publish cheap `ViewChanged` notifications.

Forbidden:

- QML dependency;
- AppController dependency;
- direct UI model mutation;
- full typed batch fanout to UI/main thread;
- debug/profiler string generation in the hot path;
- COM-owning gateway in Passive Product.

## View Query Plane

UI is a consumer of views, not a consumer of raw typed stream.

Core push:

```text
ViewChanged {
  view_name
  view_seq
  severity
  cheap_counts
  timestamp_ms
}
```

UI pull:

```text
GetView {
  view_name
  since_seq
  limit
}
```

Core response:

```text
ViewSnapshot {
  view_name
  view_seq
  updated_at_ms
  severity
  dropped_display_count
  source_capture_seq_range
  payload
}
```

Allowed production views:

- `core_health`
- `profile_status`
- `transport_summary`
- `passive_safety_profile`
- `passive_usb_lifecycle`
- `capture_progress`
- `analysis_snapshot`
- `live_latest`
- `decoded_can_tail`
- `graph_bucket`
- `control_audit`
- `fatal_diagnostics`

Query must return already materialized bounded data. It must not replay or scan
the full capture on demand.

## Optional Debug Tap Plane

`vsm-debug-tap.exe` is a product diagnostic sidecar.

- default OFF;
- connects to Core IPC only;
- never opens COM/USB;
- never sends host TX/control;
- never mutates Core state;
- bounded output files: `debug_tap_trace.jsonl`, `debug_tap_summary.json`;
- drop/backpressure affects debug artifact only.

The old COM-owning gateway remains lab-only and is not a Passive Product
diagnostic path.

## Data Ownership Table

| Data | Owner | Consumer | Drop policy |
| --- | --- | --- | --- |
| Raw USB bytes | Core drain | Parser | Queue full is fatal host-drain overrun |
| Accepted typed bytes | Capture writer | Replay/import/evidence extraction | Writer overrun invalidates capture |
| CAN RX analysis input | Analysis runtime | Analysis only | Overrun increments analysis truth loss |
| Live latest | MaterializedViewStore | UI query, debug tap | Display coalesce/drop allowed |
| Decoded CAN tail | DecodedTailRuntime | UI query, debug tap | Tail drop is display-only counter |
| Transport diagnostics | Runtime owners | UI/debug display | Diagnostics do not own runtime state |
| Debug trace | Debug tap process | Operator/debug tools | Drop with debug counter only |

## Migration Rule

Any live/capture change must:

1. define data flow;
2. define owner/consumer/drop policy;
3. search existing owner violations;
4. add boundary DTO/interface;
5. move the function;
6. delete old route;
7. add static/test guard.

Transitional code is allowed only when bounded, diagnosed, and listed by the
boundary scan. It must not be claimed as final architecture completion.

## Acceptance

- Core capture continues while UI is slow, frozen, or disconnected.
- UI receives only view notifications/query responses.
- Debug Tap ON/OFF does not alter Core capture or vehicle bus.
- Capture truth can be replayed to rebuild derived views.
- Memory plateau is proven by 30s/10m/1h HIL when available.

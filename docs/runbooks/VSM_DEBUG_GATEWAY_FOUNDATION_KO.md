# VSM Debug Gateway Runbook

## Purpose

VSM debug gateway mode is for crash/hang investigation under high-load CSM typed stream.
Normal VSM operation still opens `COM7` directly. Debug gateway mode is opt-in only.

The gateway owns the physical serial port, writes raw typed bytes to disk first, and forwards
the same bytes to VSM through localhost TCP. If VSM crashes, gateway artifacts remain available
up to the last successful serial read/write flush.

## Invariant

- Performance fixes must not change factual CAN values.
- UI display may be reduced, delayed, or coalesced, but that state must be explicit in diagnostics.
- Timing/value/alarm/DLC/control evidence must not be calculated from sampled UI projection.
- Direct COM reader PASS cannot replace VSM user-route PASS.
- Debug gateway code is not in the normal app hot loop.
- Debug gateway is raw serial evidence only. UI/backend bottleneck timing must be read from `PerformanceProbeRuntime` snapshots or `process_metrics.csv`.

## Normal Mode

```powershell
can_monitor_qml_reboot.exe
```

In normal mode `can_monitor_qml_reboot.exe` starts `vsm-capture-core.exe` and the core process owns
the physical serial endpoint. The UI is a view-query client and must not open COM directly.

Normal production flow:

- UI process: `can_monitor_qml_reboot.exe`
- Core process: `vsm-capture-core.exe`
- Debug gateway process: not started

No gateway writer/forwarder runs unless the user explicitly starts Debug Gateway mode.

## Gateway Mode

Manual gateway:

```powershell
py -3 scripts\vsm_debug_gateway.py --port COM7 --listen-port 18477 --out-dir artifacts\vsm_debug_gateway\manual
```

Then connect VSM to:

```text
tcp://127.0.0.1:18477
```

Automated VSM user-route HIL:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --debug-gateway --port COM7 --duration 30 --no-api-load
```

Official catalog wrapper:

```powershell
py -3 scripts\vsm_verify.py run --scenario debug_gateway --port COM7
```

High-load route:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --debug-gateway --port COM7 --duration 30 --pcan-rate 1500 --kvaser-rate 1500 --id-count 64
```

## Artifacts

Gateway artifacts are written under the HIL run directory:

- `gateway/gateway_capture.stream`: raw typed bytes read from CSM.
- `gateway/gateway_capture.index.jsonl`: sparse typed frame index generated after normal gateway stop.
- `gateway/gateway.meta.json`: gateway counters, parser counters, stream hash.
- `gateway/events.jsonl`: serial/TCP/segment events.
- `gateway/segments/*.stream`: rotated raw stream segments.
- `gateway_capture_report.json`: HIL summary of gateway result.

VSM user-route artifacts remain separate:

- `capture_report.json`: final VSM capture parse result.
- `session.meta.json`: copied VSM session metadata.
- `process_metrics.csv`: VSM process memory/CPU samples.
- `app_state.jsonl`: VSM HIL status samples.
- `result.json`: final PASS/FAIL and exact reasons.

## Deployment Contract

Portable and release folders must contain:

- `can_monitor_qml_reboot.exe`
- `vsm-capture-core.exe`
- `vsm_debug_gateway.py`

`vsm-capture-core.exe` is required for normal production operation. `vsm_debug_gateway.py` is not
executed in normal mode, but it must be present so field debug can be enabled without replacing the
application folder. Packaging must fail if either file is missing.

## Pass/Fail Meaning

Gateway parser failure after offline index generation means the raw serial stream evidence is degraded.
VSM parser/storage failure means VSM user-route capture is degraded.
PCAN/Kvaser sequence mismatch means the actual VSM final capture did not preserve the sent load.

PASS requires the actual VSM route:

1. VSM exe starts.
2. HIL control channel connects VSM to `tcp://127.0.0.1:<gateway-port>`.
3. VSM starts typed logging.
4. Optional PCAN/Kvaser load runs.
5. VSM stops logging and finalizes `capture.stream`, `capture.index`, `session.meta.json`.
6. Final VSM capture parses without typed CRC/length/seq/resync failures.
7. Gateway artifacts exist and show serial/TCP forwarding.
8. VSM remains responsive and memory growth stays bounded.

## Failure Triage

- Gateway `serial_rx_bytes=0`: COM port, CSM power, USB, or exclusive port ownership problem.
- Gateway `tcp_tx_bytes=0`: VSM never connected to gateway or connected after no data arrived.
- Gateway `initial_resync_drop>0`: gateway opened while CSM was already mid-frame; this is startup alignment evidence, not a mid-run stream failure.
- Gateway `resync_drop>0`: bytes were lost or corrupted after at least one valid typed frame; treat as raw stream evidence failure.
- Gateway `tcp_forward_errors>0`: VSM disconnected/hung/crashed while gateway continued reading.
- VSM final capture missing or `.part` remains: VSM logging/finalization problem.
- VSM capture parser failures but gateway parser clean: VSM transport/parser/storage path problem.
- Gateway parser failures too: raw serial stream or physical/protocol problem, not UI projection.

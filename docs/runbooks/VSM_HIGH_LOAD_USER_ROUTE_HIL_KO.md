# VSM High-Load User-Route HIL

## Goal

This runbook verifies the actual VSM user route, not a direct COM reader.

PASS requires VSM exe startup, VSM control-channel connect/start/stop logging, final VSM
`capture.stream` parse, and optional PCAN/Kvaser sequence comparison.

## Required Setup

- VSM Release exe: `out/build/x64-Release/can_monitor_qml_reboot.exe`
- CSM typed evidence stream: default `COM7`
- Python: use `py -3`
- Optional high-load senders:
  - PCANBasic.dll available
  - Kvaser canlib32.dll available
  - PCAN/Kvaser channels bus-on capable

## Normal VSM Route

CSM only, 30 seconds:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --no-api-load --duration 30 --port COM7
```

PCAN/Kvaser `1500fps + 1500fps`, 64 IDs, 30 seconds:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --duration 30 --port COM7 --pcan-rate 1500 --kvaser-rate 1500 --id-count 64
```

Escalate with `--duration 60` and `--duration 300` only after the 30 second run passes.

## Debug Gateway VSM Route

Use this mode when VSM crash/hang evidence is needed.

CSM only:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --debug-gateway --no-api-load --duration 30 --port COM7
```

High load:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --debug-gateway --duration 30 --port COM7 --pcan-rate 1500 --kvaser-rate 1500 --id-count 64
```

Gateway mode means:

```text
CSM COM7 -> vsm_debug_gateway.py -> tcp://127.0.0.1:<gateway-port> -> VSM
```

The gateway records raw serial bytes before forwarding them to VSM.

## Artifacts

Run directory:

- `artifacts/vsm_user_route_hil/<run>/result.json`
- `artifacts/vsm_user_route_hil/<run>/summary.md`
- `artifacts/vsm_user_route_hil/<run>/process_metrics.csv`
- `artifacts/vsm_user_route_hil/<run>/app_state.json`
- `artifacts/vsm_user_route_hil/<run>/app_state.jsonl`
- `artifacts/vsm_user_route_hil/<run>/capture_report.json`
- `artifacts/vsm_user_route_hil/<run>/sent_sequences.json`
- `artifacts/vsm_user_route_hil/<run>/session.meta.json`

Debug gateway adds:

- `artifacts/vsm_user_route_hil/<run>/gateway/gateway_capture.stream`
- `artifacts/vsm_user_route_hil/<run>/gateway/gateway_capture.index.jsonl`
- `artifacts/vsm_user_route_hil/<run>/gateway/gateway.meta.json`
- `artifacts/vsm_user_route_hil/<run>/gateway/events.jsonl`
- `artifacts/vsm_user_route_hil/<run>/gateway_capture_report.json`

VSM capture directory is created under:

```text
replay_data/logs/<run>.typed/
```

## PASS Criteria

- A new VSM typed capture directory is created for this run.
- `capture.stream`, `capture.index`, and `session.meta.json` exist.
- No `.part` file remains after stop logging.
- VSM process stays responsive enough to accept snapshot and stop logging.
- Final VSM capture has typed CRC/length/seq/resync faults equal to zero.
- CSM `can_drop` and FIFO overflow deltas are zero when board health is present.
- With API load, PCAN/Kvaser sent sequences match VSM final capture unique sequences.
- Process private memory stays bounded.
- In debug gateway mode, gateway artifacts exist and show nonzero `serial_rx_bytes` and `tcp_tx_bytes`.

## FAIL Criteria

- Direct COM reader passes but VSM final capture is missing or corrupt.
- The script selects a stale capture instead of a new capture.
- VSM cannot stop/finalize logging.
- Projection drop/sampling is reported as parser/storage/CSM CAN loss.
- Gateway captures raw data but VSM final capture has parser/storage failures.

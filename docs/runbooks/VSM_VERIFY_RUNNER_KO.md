# VSM Verify Runner Runbook

## Purpose

`scripts/vsm_verify.py` is the official local catalog for VSM field/HIL/debug checks.
It keeps the executable checks inside this project folder and writes a stable result
envelope under `artifacts/vsm_verify/<run_id>/`.

The runner is a launcher only. It does not replace the underlying HIL scripts, VSM
capture validation, or debug gateway artifacts.

## Commands

List scenarios:

```powershell
py -3 scripts\vsm_verify.py list
```

Run the actual VSM user-route 30s check:

```powershell
py -3 scripts\vsm_verify.py run --scenario user_route_30s --port COM7
```

Run timing/value/alarm/graph truth HIL:

```powershell
py -3 scripts\vsm_verify.py run --scenario analysis_truth_30s --port COM7
```

Run a dry-run schema check without hardware:

```powershell
py -3 scripts\vsm_verify.py run --scenario latest_capture_report --dry-run
```

Show the latest result envelope:

```powershell
py -3 scripts\vsm_verify.py status
```

## App Integration

Settings -> `디버그 / 검증` launches the same runner as an external `py -3`
process. The app only starts/stops the process and shows status/artifact paths.
The normal live/capture hot loop does not execute runner or gateway code.

## Artifact Contract

Every run writes:

- `result.json`: scenario, command, pass, exit code, elapsed time, stdout/stderr path.
- `summary.md`: short operator-readable summary.
- scenario-specific child artifacts from the underlying script.

## Diagnostics Boundary

- `vsm_debug_gateway.py`: crash-survivable raw serial evidence.
- `PerformanceProbeRuntime`: in-app module timing and queue/backlog diagnostics.
- `process_metrics.csv`: process-level memory/CPU.
- VSM PASS: final VSM capture and user-route result must pass. Gateway-only PASS is not enough.

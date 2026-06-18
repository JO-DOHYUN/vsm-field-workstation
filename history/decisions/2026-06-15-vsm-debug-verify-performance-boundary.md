# 2026-06-15 VSM Debug/Verify/Performance Boundary

## Decision

VSM keeps official HIL/debug/report runners inside the project folder and exposes
them through `scripts/vsm_verify.py`. The app may launch the runner from the
Settings debug panel, but only as an external `py -3` process.

Module-level bottleneck claims require `PerformanceProbeRuntime` counters. Debug
gateway artifacts remain raw serial evidence only and cannot be used as an
internal UI/backend profiler.

## Reason

Previous high-load work could prove capture truth and process-level memory, but
not which app module caused stalls. Keeping gateway, process metrics, and module
probe roles separate prevents false PASS claims.

## Rollback

If the debug panel or runner introduces runtime instability, remove the app
launcher surface first. Keep `vsm_verify.py` and `PerformanceProbeRuntime` tests
unless they are the direct failure source.

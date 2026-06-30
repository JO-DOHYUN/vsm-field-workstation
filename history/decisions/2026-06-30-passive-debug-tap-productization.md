# 2026-06-30 Passive Debug Tap Productization

## Decision
Passive Product diagnostics are productized as `vsm-debug-tap.exe`, a
non-owning Core IPC sidecar. The existing `vsm_debug_gateway.py` remains
Full/Instrumented lab-only because it owns the COM port.

## Reason
Vehicle-impact-free field diagnostics must not compete with Core for COM/USB,
must not send host TX/control, and must not add debug-only parsing or JSON work
to the Core hot path. USB plug/unplug diagnosis still needs artifacts that keep
recording when the UI is slow or unclear, so the debug plane is a separate
process that subscribes to bounded Core views.

## Expected Gain
- Normal passive run remains two processes: UI + Core.
- Diagnostics adds one process: Debug Tap.
- Debug Tap cannot physically affect the vehicle bus because it never opens COM.
- Debug artifacts capture Core transport/profile/capture/fatal view timeline.
- Slow debug logging can only degrade debug evidence, not production truth.

## Rollback Rule
Rollback if `vsm-debug-tap.exe` causes Core IPC backpressure, if it opens COM or
uses serial APIs, or if passive diagnostics can stop/restart Core transport
without explicit operator disconnect.

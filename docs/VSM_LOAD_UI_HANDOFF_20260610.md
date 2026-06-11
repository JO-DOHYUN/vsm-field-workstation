# VSM Load/UI Handoff - 2026-06-10

## Purpose

This document is the handoff from the CSM/Android planning thread to the VSM project thread.

The field failure was not proven to be a CSM USB typed-stream corruption issue. The stronger evidence is that VSM cannot sustain the actual user route under high load: Live/UI projection, diagnostics, and logging together can push the app into memory growth and `Responding=False`.

The next VSM thread must read this file first and implement the VSM-side fix before changing CSM firmware again.

## Hard Rules

- Do not treat direct COM7 typed-stream verification as equivalent to VSM user-output verification.
- Do not claim PASS unless the actual VSM app route works: VSM process alive, UI responsive, log started/stopped by VSM, final `capture.stream` produced, and capture analysis passes.
- Preserve typed original bytes as production truth.
- Preserve evidence separation: `CAN_RX_RAW`, `CAN_TX_RAW`, `BOARD_HEALTH`, `BOARD_EVENT`, `CAPABILITY`, `CONTROL_ACK`.
- Board alive still requires valid `CAPABILITY` plus fresh `BOARD_HEALTH`.
- Actual CAN sent still requires matching `CAN_TX_RAW`.
- UI projection may sample/drop display work, but capture storage must remain append-only and lossless.
- UI projection drop/sampling counters must not be mixed with CSM CAN drop, typed gap, CRC, or FIFO overflow.

## Current Evidence

### VSM executable under test

- `C:\WORKS\VS\turn81_full_buildfix2\out\build\x64-Release\can_monitor_qml_reboot.exe`

### Valid VSM user-route 30 second capture

- Capture directory: `C:\WORKS\VS\turn81_full_buildfix2\replay_data\logs\typed_capture_20260610_162910.typed`
- Final stream: `C:\WORKS\VS\turn81_full_buildfix2\replay_data\logs\typed_capture_20260610_162910.typed\capture.stream`
- Result:
  - `stream_bytes=2062068`
  - `records=50218`
  - `CAN_RX_RAW=49561`
  - `BOARD_HEALTH=31`
  - `CAPABILITY=16`
  - `seq_gaps=0`
  - `crc=0`
  - `length=0`
  - `dropped_bytes=0`
  - `can_rx_delta=48760`
  - `can_drop_total=0`
  - `fifo_total=0`
  - `serial_delta=49405`
  - `max_queue=1`
- This proves the VSM route can work briefly at the current external load, but it does not prove high-load field readiness.

### Reproduced VSM failure

- VSM process became `Responding=False`.
- Observed memory:
  - PID `26800`: private memory reached about `3.78GB`.
  - PID `25628`: private memory reached about `7.9GB`.
- Session log `C:\WORKS\VS\turn81_full_buildfix2\replay_data\logs\session_20260610_072720.log` stopped after:
  - Serial connected COM7 typed evidence.
  - Typed `CAPABILITY` received.
- The process stopped producing useful session progress while the UI became nonresponsive.

### Invalid stress artifact

Do not use this file as a PASS/FAIL result:

- `C:\WORKS\VS\vsm_android_app\artifacts\vsm_actual_route_stress_1500x2_30s_20260610_result.json`

Reason: the high-load attempt did not create a new VSM capture directory and selected the previous capture (`typed_capture_20260610_162910.typed`). Its `pcan_missing=45000` and `kvaser_missing=45000` are a harness/capture-selection failure, not a valid VSM capture verdict.

## Existing HIL Tools And Limits

### Direct typed-stream HIL

- Tool: `C:\WORKS\VS\vsm_android_app\tools\hil_dual_can_stress.py`
- It directly controls PCAN and Kvaser through vendor APIs and reads COM7 typed bytes itself.
- It is useful for CSM/USB typed-stream integrity.
- It is not sufficient for VSM user-output validation because it bypasses VSM UI, VSM logging, VSM tab responsiveness, and VSM capture finalization.

### Known direct typed-stream results

- PASS direct stream:
  - `C:\WORKS\VS\vsm_android_app\artifacts\hil_dual_usb_highload_1500_1500_20260609_085230\result.json`
  - `1500fps + 1500fps`, 64 IDs, 60 seconds
  - PCAN sent `89993`, received unique `89993`, missing `0`
  - Kvaser sent `89995`, received unique `89995`, missing `0`
  - typed gaps `0`, CRC `0`, length `0`, CAN drop `0`, FIFO `0`
- FAIL/limit direct stream:
  - `C:\WORKS\VS\vsm_android_app\artifacts\hil_dual_usb_highload_2000_2000_20260609_085719\result.json`
  - `2000fps + 2000fps`, 64 IDs, 60 seconds
  - PCAN missing `0`
  - Kvaser missing `10`
  - Use this as a known upper-limit warning, not as a VSM load target until the physical/device side is rechecked.

## Primary VSM Suspect Area

Focus on these files first:

- `C:\WORKS\VS\turn81_full_buildfix2\src\backend\AppController.cpp`
- `C:\WORKS\VS\turn81_full_buildfix2\src\backend\AppController.h`
- `C:\WORKS\VS\turn81_full_buildfix2\src\backend\transport\TypedIngressRuntime.cpp`
- `C:\WORKS\VS\turn81_full_buildfix2\src\backend\transport\TransportSession.cpp`
- `C:\WORKS\VS\turn81_full_buildfix2\src\backend\FrameListModel.cpp`
- `C:\WORKS\VS\turn81_full_buildfix2\qml\pages\LivePage.qml`

Observed code shape:

- `AppController::appendPendingLiveFrames()` around `AppController.cpp:1564`
  - `m_pendingLiveFrames.reserve(m_pendingLiveFrames.size() + frames.size())`
  - pushes all live frames into the pending queue.
  - no hard cap is visible on the main pending-analysis queue.
- `AppController::flushPendingLiveFrames()` around `AppController.cpp:1601`
  - processes pending frames on the app/controller side.
  - calls `ingestFrame()` per frame.
- `AppController::ingestFrame()` around `AppController.cpp:8685`
  - updates per-ID state.
  - calls `appendGraphSamples()`.
  - marks timing/value/alarm dirty.
  - calls `syncValueAlarmState()` for live frames.
- `FrameListModel.cpp`
  - visible list rows are capped, so the visible 700-row frame list is not the primary root cause by itself.
- `TypedIngressRuntime.cpp`
  - appends typed records to storage before emitting batches.
  - preserve this ordering: capture append remains the production truth.
- `TransportSession.cpp`
  - already reports pending live and sampled-view-drop style diagnostics.
  - extend this instead of hiding the problem.

## Required Architecture Change

Refactor VSM runtime so high-load capture and high-load UI are separate responsibilities:

- Capture/storage path:
  - Keep typed byte ingest and `capture.stream` append independent from UI projection.
  - Logging must continue even when Live UI is paused, sampled, or lagging.
  - Finalization must be reliable after stop/disconnect.
- Live projection path:
  - Add a bounded projection queue or replace the unbounded `m_pendingLiveFrames` behavior.
  - Process a fixed time/frame budget per GUI tick.
  - Drop/sample only projection work when needed.
  - Keep latest-per-ID state and recent visible rows useful without requiring every frame to be projected.
- Analysis/model/graph path:
  - Do not run full graph/model/alarm work for every frame when backlog is above threshold.
  - Use coalescing/latest-per-ID state, decimation, or deferred evaluation.
  - Keep diagnostics honest when projection is degraded.
- Diagnostics:
  - Expose at least:
    - capture bytes and record count
    - pending live projection frames
    - projection dropped/sampled frames
    - max pending projection backlog
    - UI flush budget hits
    - memory/process monitor when available in test harness
  - Keep these separate from typed parser errors and CSM `BOARD_HEALTH` counters.

## Required VSM User-Route Verification

The next validation must exercise the actual app route:

1. Launch `can_monitor_qml_reboot.exe`.
2. Select/connect COM7 through VSM.
3. Start VSM logging through the VSM UI or the same app command path the UI uses.
4. Send load through PCAN and Kvaser vendor APIs.
5. Monitor VSM process:
   - `Responding`
   - working set
   - private memory
   - CPU
   - session log progress
   - capture `.part` growth
6. During load, verify user operations:
   - Live tab remains responsive.
   - Diagnostics/Overview tab switching works.
   - Log state indicator updates.
   - Stop logging is accepted.
7. Stop VSM logging and require final files:
   - `capture.stream`
   - `capture.index`
   - `session.meta.json`
8. Parse the final `capture.stream`.
9. Compare final capture payload sequences against actual PCAN/Kvaser sent counts.

### PASS criteria

- VSM process remains alive and responsive.
- Memory remains bounded with no monotonic explosion.
- New capture directory is created for that run.
- `.part` files finalize into `capture.stream` and `capture.index`.
- typed parser:
  - CRC errors `0`
  - length errors `0`
  - typed sequence gaps `0`
  - resync dropped bytes `0`
- board health:
  - `can_drop=0`
  - CAN FIFO overflow `0`
- PCAN/Kvaser compare:
  - sent count equals VSM capture unique payload sequence count
  - missing `0`
  - duplicate `0`
  - bad payload `0`
  - wrong bus `0`
- UI projection:
  - projection sampling/drop is allowed only if displayed separately.
  - projection sampling/drop must not affect final capture truth.

### FAIL criteria

- No new capture directory.
- Capture remains only `.part`.
- VSM becomes `Responding=False`.
- Memory climbs unboundedly.
- UI cannot switch tabs or stop logging.
- Any typed CRC/length/gap/resync error.
- Any CSM CAN drop/FIFO overflow.
- Any sent-vs-captured sequence mismatch.

## Minimum Test Ladder

Use these in order and stop on first failure:

1. Current external apps only, 30 seconds.
2. API-driven `1500fps + 1500fps`, 64 IDs, 30 seconds.
3. API-driven `1500fps + 1500fps`, 64 IDs, 60 seconds.
4. API-driven `1500fps + 1500fps`, 64 IDs, 5 minutes.
5. Optional after physical/device recheck: higher than 1500fps per bus.

Do not use `2000fps + 2000fps` as the first acceptance load because direct stream already showed Kvaser-side missing frames at that level.

## Prompt For The VSM Thread

Paste this prompt into the VSM project chat:

```text
Read this file first and follow it exactly:
C:\WORKS\VS\turn81_full_buildfix2\docs\VSM_LOAD_UI_HANDOFF_20260610.md

Goal: fix VSM so the actual user route survives high-load dual-CAN capture. Do not start by modifying CSM firmware. The failure evidence points to VSM Live/UI projection and analysis backlog causing memory growth and `Responding=False`, while direct typed-stream integrity can pass.

Required work:
1. Inspect the handoff MD completely before editing.
2. Confirm the current failure path in VSM code, especially:
   - C:\WORKS\VS\turn81_full_buildfix2\src\backend\AppController.cpp
   - C:\WORKS\VS\turn81_full_buildfix2\src\backend\AppController.h
   - C:\WORKS\VS\turn81_full_buildfix2\src\backend\transport\TypedIngressRuntime.cpp
   - C:\WORKS\VS\turn81_full_buildfix2\src\backend\transport\TransportSession.cpp
   - C:\WORKS\VS\turn81_full_buildfix2\src\backend\FrameListModel.cpp
   - C:\WORKS\VS\turn81_full_buildfix2\qml\pages\LivePage.qml
3. Refactor so typed capture/storage remains lossless and independent from UI projection.
4. Bound or replace the unbounded live projection backlog. UI may sample/drop projection work, but final capture must remain complete.
5. Decimate/coalesce graph/model/alarm work under backlog instead of processing every frame on the UI/controller path.
6. Expose separate diagnostics for projection backlog/drop/sampling and do not mix them with typed CRC/gap or CSM CAN drop/FIFO evidence.
7. Build VSM Release.
8. Create or update a VSM user-route HIL harness that launches/controls VSM, connects COM7, starts VSM logging, sends PCAN/Kvaser load through vendor APIs, monitors VSM process responsiveness/memory, stops logging, verifies capture finalization, parses `capture.stream`, and compares PCAN/Kvaser sent sequences against VSM capture sequences.
9. Run the required ladder from the MD and report exact artifacts.

Acceptance:
- PASS only if the actual VSM app route passes, not just a direct COM7 reader.
- New capture directory must be created for each test.
- VSM must remain responsive, memory bounded, logs finalized.
- typed CRC/length/gap/resync errors must be 0.
- CSM `can_drop` and CAN FIFO overflow must be 0.
- PCAN/Kvaser sent sequence counts must exactly match VSM final capture unique sequence counts.
```

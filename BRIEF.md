# BRIEF.md

## Current Baseline
- Active VSM repository root: this folder, local path `C:\WORKS\VS\turn81_full_buildfix2`.
- VSM GitHub repository: `JO-DOHYUN/vsm-field-workstation`.
- CSM firmware workspace is separate; use it only for explicit firmware work or CSM PlatformIO verification.
- Runtime data root: `replay_data/` with logs under `replay_data/logs/` and snapshots under `replay_data/snapshots/`.
- Active model/rules baseline: `data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json`.
- Product direction: VSM is an evidence-first CAN monitor/logger/replay/decode/control workstation paired with the CSM typed evidence/control gateway.

## Must Preserve
- VMS live production path is CSM typed evidence stream only.
- COM open is not board alive; valid `CAPABILITY` and fresh `BOARD_HEALTH` are required for alive/control-capable state.
- Legacy 20-byte packet, CRC8, DLC, and `t_us` wrap are preserved for replay/import compatibility only.
- Typed evidence separation is mandatory: `CAN_RX_RAW`, `CAN_TX_RAW`, `ADC_SAMPLE`, `BOARD_HEALTH`, `BOARD_EVENT`, `CONTROL_ACK`, and `CAPABILITY`.
- Qt command write, `CONTROL_ACK`, `CAN_TX_RAW`, and feedback remain separate evidence; actual CAN TX success requires matching `CAN_TX_RAW`.
- Live and replay source semantics remain separate.
- Graph truth-first behavior, fixed-axis overview, peak meaning, nested zoom/back/root/clear remain intact.
- Current model/rules decode and control allowlist assumptions must remain traceable.

## Current Verified State
- Current workspace Release configure/build passed with Qt 6.10.2 + MSVC2022 when run through `VsDevCmd.bat`.
- Current workspace `ctest --test-dir out/build/x64-Release --output-on-failure` passed: 28/28 after the analysis truth stress verification slice, model-pack control policy/profile slice, live bus0/bus1 high-load path optimization, and debug/profiler/verification formalization slice.
- Current workspace Release exe startup smoke passed after project-local `replay_data` path migration, Storage/Replay/Evidence/Control/Transport runtime boundary split, ControlPage operator evidence workflow hardening, typed replay/logging diagnostics completion, Live/Replay/Control/Graph QML state-probe hardening, ControlRuntime operator state split, TransportSession live diagnostics, model-pack backed control policy/profile gating, live typed parser/UI queue optimization, analysis truth stress verification, and debug/profiler/verification formalization.
- Latest portable Field RC generated and portable startup smoke passed: `out/build/x64-Release/portable_field_rc_20260604`; manifest package id `field-rc-20260604-workspace-direct-edit`. Debug/verification deploy check also passed at `out/build/x64-Release/portable_debug_verify_check`.
- CSM PlatformIO build passed for `portenta_h7_m7_mid_mcp2515_j4_dual_csm`.
- GitHub Actions `platform-ci` passed on commit `b17e1c4`: `csm-firmware`, `vsm-qt`, portable deploy smoke.
- High-load live truth/UI boundary 1차 구현 is in place: display projection remains bounded, truth snapshot flush no longer hard-cuts frames, and live/replay analysis state keys are `bus + canId + ext + rtr` so performance work reduces display volume without changing timing/value/alarm/DLC/control evidence. See `docs/architecture/VSM_TRUTH_FIRST_LOAD_ARCHITECTURE_KO.md`.
- Typed foundation exists: parser, records, storage, typed replay reader, typed replay projection into legacy analysis frames.
- Replay now accepts both legacy `.bin` and typed capture sessions (`typed_capture_*.typed/capture.stream`), including typed session state, timeline, meta/index/events, type counts, seq gaps, partial/corrupt capture diagnostics, CAN RX projection checks, operator verdict, and DLC preservation verdict.
- Control foundation exists: heartbeat/session, 0x503 and 0x510/0x512/0x511/0x513 command burst, slew limiter, host TX queue, evidence separation, operator ready/block summary, ControlRuntime checklist/verdict roles, model-pack `control_policy`/bus role rules/limits, and ControlPage state probes that keep ACK separate from actual `CAN_TX_RAW` success.
- Runtime split foundation exists: `StorageRuntime` owns project-local data paths, `ReplayRuntime` owns replay open/cache/session paths, `EvidenceRuntime` owns board alive/control-capable state, `ControlAuditModel` owns request/write/ACK/CAN_TX_RAW/feedback/fault audit model state, `ControlRuntime` owns operator arm/test target/intent/counter/timing latch state, `TransportRuntime` owns `SerialWorker` thread lifecycle plus queued live serial/log/control operations, `TypedIngressRuntime` owns typed parser/storage/progress batching, `LegacyIngressRuntime` owns legacy 20B parser/recorder/progress, `HostTxRuntime` owns host TX FIFO/backpressure counters, `ControlCycleRuntime` owns heartbeat/session/control burst pacing, and `TransportSession` owns operator transport diagnostics for parser faults, host TX queue/backpressure, and live delay. `SerialWorker` is now serial open/read/write, timer orchestration, and signal bridging.
- Latest field typed log triage exists at `docs/field/2026-05-28_latest_typed_log_triage_KO.md`; `scripts/field_latest_capture_report.py` summarizes the newest project-local typed capture. Replay typed diagnostics now expose CAN bus/DLC, DLC preservation verdict, operator replay verdict, board health, board events, capability bus, timeline, meta, index, events, sidecar, and fault rows. QML smoke now probes actual graph checkbox-click scroll/color stability, graph wrapper toggle stability, ControlPage evidence authority, Replay typed diagnostics hooks, and Live page transport/field state hooks.
- CSM HIL from prior run showed bus0/bus1 RX and strict control TX audit matching, but any new hardware claim requires a fresh hardware run.

## Current Goal
- Continue VSM implementation in this standalone VSM repository root.
- Keep CSM and VSM contracts aligned around typed stream v1 and control evidence.
- Keep build/test verification reproducible without duplicate workspace edits or noisy successful logs.
- Work in one user-visible vertical slice per turn, not file-by-file cleanup. Each slice should include at least three of: operator UI change, evidence contract/model change, regression coverage, executable smoke.
- Do not add temporary `AppController` responsibilities that already belong to Storage/Replay/Transport/Control runtimes without an explicit boundary plan in the same slice.
- Resolve long-run VSM live high-load memory growth by moving the live capture hot path toward the capture-core memory architecture, not by UI throttling alone.

## Immediate Next Work
- Capture-core memory priority: remove `TypedRecordList`/owning `QByteArray` full-batch fanout from the live production hot path, introduce bounded slab/descriptor ownership, and prove memory plateau with the capture-core memory verification runbook. Start each slice with the owner/data-flow/drop-policy gate in `docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO.md` and the transition scan `scripts/check_vsm_boundary_rules.py`. See `docs/architecture/VSM_CAPTURE_CORE_MEMORY_ARCHITECTURE_KO.md` and `.agents/skills/capture-core-memory/SKILL.md`.
- Program RC status: prior Windows VSM portable RC exists at `portable_field_rc_20260604`; current workspace now has truth-first live analysis boundary plus analysis truth stress verification, verified by Release build, full 27/27 ctest, and exe startup smoke. Fresh HIL/vehicle run is still required before field-final claim.
- Documentation status: `START_HERE_KO`, completion plan, architecture note, release checklist, Android feasibility, field runbook, and analysis truth stress HIL runbook are aligned to the current 27/27 verified state.
- Immediate architecture state: `LiveTruthRuntime` owns worker-side truth snapshot/coalescing diagnostics, `TransportSession` exposes separate `live_truth` and `live_projection` rows, debug gateway mode is implemented through `scripts/vsm_debug_gateway.py`, and typed capture storage writes are buffered while preserving `capture.stream`/`capture.index` format.
- Debug/verification state: `PerformanceProbeRuntime` provides debug-only module timing/backlog counters, Settings exposes a debug/profiler/verify panel, and `scripts/vsm_verify.py` is the official project-local launcher catalog for user-route HIL, analysis-truth HIL, control smoke, debug gateway, and latest-capture reporting.
- Latest HIL status: 2026-06-15 analysis truth stress HIL passed at PCAN/Kvaser `1000fps + 1000fps` for 30s after fixing live graph truth history and snapshot export. Artifacts: `artifacts/vsm_analysis_truth_hil/vsm_analysis_truth_20260615_221209`; capture: `replay_data/logs/vsm_analysis_truth_20260615_221209.typed`.
- Field-only follow-up: fresh vehicle bus0/bus1 stability, graph real-data manual smoke, control-policy validation/result archive, and CSM firmware FIFO/drain improvement for `1500fps + 1500fps` remain outside automatic VSM completion unless explicitly taken as the next hardware/firmware slice.

## Read Next
- New account entry: [[START_HERE_KO]]
- Project map: [[INDEX]]
- Folder guide: [[docs/PROJECT_FOLDER_GUIDE_KO]]
- Completion plan: [[docs/COMPLETION_TO_RELEASE_PLAN_KO]]
- Production plan: [[docs/PLAN]]
- VSM architecture: [[docs/architecture/VSM_WORKSTATION_ARCHITECTURE_KO]]
- Truth-first load architecture: [[docs/architecture/VSM_TRUTH_FIRST_LOAD_ARCHITECTURE_KO]]
- VMS runtime split: [[docs/architecture/VMS_ARCHITECTURE_KO]]
- Typed protocol: [[docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO]]
- Control evidence contract: [[docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO]]
- Build runbook: [[docs/runbooks/BUILD_AND_VERIFY_KO]]
- Android feasibility: [[docs/android/ANDROID_FEASIBILITY_KO]]

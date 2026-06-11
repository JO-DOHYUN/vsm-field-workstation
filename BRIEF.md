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
- Current workspace `ctest --test-dir out/build/x64-Release --output-on-failure` passed: 22/22 after the latest model-pack control policy/profile slice and live bus0/bus1 high-load path optimization.
- Current workspace Release exe startup smoke passed after project-local `replay_data` path migration, Storage/Replay/Evidence/Control/Transport runtime boundary split, ControlPage operator evidence workflow hardening, typed replay/logging diagnostics completion, Live/Replay/Control/Graph QML state-probe hardening, ControlRuntime operator state split, TransportSession live diagnostics, model-pack backed control policy/profile gating, and live typed parser/UI queue optimization.
- Latest portable Field RC generated and portable startup smoke passed: `out/build/x64-Release/portable_field_rc_20260604`; manifest package id `field-rc-20260604-workspace-direct-edit`.
- CSM PlatformIO build passed for `portenta_h7_m7_mid_mcp2515_j4_dual_csm`.
- GitHub Actions `platform-ci` passed on commit `b17e1c4`: `csm-firmware`, `vsm-qt`, portable deploy smoke.
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

## Immediate Next Work
- Program RC status: Windows VSM portable RC is current at `portable_field_rc_20260604`; code/function closure is complete except field-only validation.
- Documentation status: `START_HERE_KO`, completion plan, architecture note, release checklist, Android feasibility, and field runbook are aligned to the current 22/22 verified state.
- Field-only follow-up: fresh HIL/vehicle bus0/bus1 stability, graph real-data manual smoke, and control-policy validation/result archive remain outside automatic program completion.

## Read Next
- New account entry: [[START_HERE_KO]]
- Project map: [[INDEX]]
- Folder guide: [[docs/PROJECT_FOLDER_GUIDE_KO]]
- Completion plan: [[docs/COMPLETION_TO_RELEASE_PLAN_KO]]
- Production plan: [[docs/PLAN]]
- VSM architecture: [[docs/architecture/VSM_WORKSTATION_ARCHITECTURE_KO]]
- VMS runtime split: [[docs/architecture/VMS_ARCHITECTURE_KO]]
- Typed protocol: [[docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO]]
- Control evidence contract: [[docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO]]
- Build runbook: [[docs/runbooks/BUILD_AND_VERIFY_KO]]
- Android feasibility: [[docs/android/ANDROID_FEASIBILITY_KO]]

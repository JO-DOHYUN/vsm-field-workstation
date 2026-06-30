# BRIEF.md

## Current Baseline
- Active VSM repository root: `C:\WORKS\VS\turn81_full_buildfix2`.
- VSM GitHub repository: `JO-DOHYUN/vsm-field-workstation`.
- CSM firmware workspace is separate; use it only for explicit firmware work.
- Runtime data root: `replay_data/`.
- Active model/rules baseline: `data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json`.
- Product direction: evidence-first CAN monitor/logger/replay/decode workstation paired with the CSM typed evidence gateway.

## Must Preserve
- Field/product default is Passive-Safe 2+1: `vsm-ui.exe + vsm-capture-core.exe`, optional debug/tap plane default OFF.
- Passive Product profile must not affect the vehicle bus: serial read-only, DTR asserted only as an Arduino CDC session gate when declared by CSM capability, RTS no-touch, host TX disabled, control disabled, COM-owning lab gateway disabled.
- Full Instrumented profile is bench/lab only and must not be used as passive product acceptance.
- COM open is not board alive; valid `CAPABILITY` and fresh `BOARD_HEALTH` are required.
- `capture.stream/index` is the only authoritative truth.
- UI/graph/decoded tail/analysis rows are bounded materialized views.
- Typed evidence separation is mandatory: CAN RX, CAN TX audit, voltage raw, board health/event, capability, control ack.
- Host-requested TX, `CONTROL_ACK`, `CAN_TX_RAW`, and feedback remain separate evidence.
- Actual CAN TX success requires matching `CAN_TX_RAW`; passive product blocks host TX entirely.
- Live and replay semantics remain separate.
- Legacy 20-byte/CRC8/DLC/`t_us` wrap remains replay/import compatibility only.

## Current Goal
- Apply CDC-maintained Passive Product architecture as the current VSM product direction.
- Close runtime profile ownership across SerialDrainRuntime, CaptureCoreProcessRuntime, IPC, CoreProcessClientRuntime, AppController, TransportSession, docs, and harness.
- Remove old-flow assumptions that treat debug gateway/control/full instrumentation as normal product behavior.
- Productize passive diagnostics as `vsm-debug-tap.exe`: a non-owning Core IPC sidecar, distinct from the lab-only COM-owning gateway.
- Keep build/test verification reproducible and do not claim vehicle passive safety without CSM capability plus hardware safety evidence.
- Treat CSM `BOARD_HEALTH v7` USB lifecycle/passive readback counters and `BOARD_EVENT` 29..35 as first-class field evidence.

## Immediate Next Work
- Finish code boundary cleanup for Passive Product default.
- Run `scripts/check_vsm_boundary_rules.py --mode transition` and targeted tests.
- Build Release after code/data-flow is internally complete.
- Verify `vsm-capture-core.exe --ready-json` reports `passive_product`.
- Verify UI/core launch uses `--profile passive_product`.
- Verify passive diagnostics starts `vsm-debug-tap.exe` without disconnecting Core or owning COM.
- Verify transport details expose `passive_safety_profile`.
- Verify transport details expose `passive_usb_lifecycle` and classify MCP listen-only/TXREQ violation as product-blocking.
- Commit the completed slice only after build/test pass or with explicit failed-command evidence.

## Read Next
- [[START_HERE_KO]]
- [[INDEX]]
- [[docs/architecture/VSM_PASSIVE_SAFE_2PLUS1_ARCHITECTURE_KO]]
- [[docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO]]
- [[docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO]]
- [[docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO]]
- [[docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO]]
- [[docs/ai_harness/BUILD_VERIFY_POLICY_KO]]

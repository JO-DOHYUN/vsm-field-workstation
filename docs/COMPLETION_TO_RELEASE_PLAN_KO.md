# VSM Completion To Release Plan

## 1. Current Position

VSM은 기능 데모가 아니라 CSM typed evidence/control gateway를 기준으로 동작하는 Windows 현장 검증 workstation으로 정리한다.

현재 완료된 기반:
- Qt/CMake Release build and full `ctest` gate: 22/22 passing.
- Legacy `.bin` replay/import compatibility.
- Typed live parser, typed record decode, append-only typed storage foundation.
- Typed replay reader and typed capture session loading through `capture.stream`, with session diagnostics, DLC verdict, timeline/meta/index/events/type-count checks.
- Control command encoder, host heartbeat/session command, VCU command burst, slew limiter, model-backed policy gate, and operator ControlPage workflow.
- Board alive/control evidence doctrine: COM open, `CONTROL_ACK`, `CAN_TX_RAW`, and feedback are separate states.
- Runtime ownership split: Storage/Replay/Evidence/Transport/TypedIngress/LegacyIngress/HostTx/ControlCycle/ControlAudit responsibilities are separated from the AppController facade.
- Latest portable Field RC: `out/build/x64-Release/portable_field_rc_20260604`, manifest package id `field-rc-20260604-workspace-direct-edit`.

현재 기준 소스는 이 VSM 단독 repository이며, GitHub 위치는 `JO-DOHYUN/vsm-field-workstation`이다.

## 2. Completion Definition

완성 판정은 다음 gate를 모두 통과해야 한다.

- VSM clean build, `ctest`, startup smoke, portable deploy smoke pass.
- CSM PlatformIO build pass for the active dual-channel profile.
- VSM can load current model baseline and decode live/replay CAN.
- VSM live connection does not call board alive until `CAPABILITY` and fresh `BOARD_HEALTH` are present.
- Control UI never calls `CONTROL_ACK` actual success; actual success requires matching `CAN_TX_RAW`.
- Typed recording can be replayed from `.typed/capture.stream`; legacy `.bin` still works.
- Bus role labels are resolved from capability/model/observed fingerprints/operator override, not hard-coded.
- ControlPage is Korean, stable, readable, and separates intent, queued, accepted, sent audit, feedback, and fault.
- GitHub repository contains VSM Qt source, docs, harness, tests, and CI as a standalone project. CSM firmware remains a separate project/gate.

## 3. Program Closure Work Packages

### P0: Latest Field RC Refresh
- Portable Field RC has been regenerated from the latest Release build.
- `RELEASE_MANIFEST.json` now matches the new folder, exe/model/SBOM/notice hashes, and `field-rc-20260604-workspace-direct-edit` package id.
- Portable exe startup smoke from the regenerated folder passed.

Acceptance:
- Portable folder starts clean and contains exe, Qt runtime, data, helpers, field runbook, SBOM/notice, and manifest.
- Manifest keeps fresh HIL/field items unverified.

### P1: Current-State Documentation Closure
- Keep verification counts and RC package references aligned to the latest `22/22` and `portable_field_rc_20260604` state.
- Keep code/function axes marked complete except for field-only validation.
- Keep MSI/signing/Android wireless as deferred or blocked, not product completion blockers for Windows portable RC.

Acceptance:
- New Codex account can identify the current state from `START_HERE_KO`, `BRIEF`, and architecture docs without chat history.

### P2: Non-HIL Program Smoke
- Use automated tests and synthetic/recorded typed data to cover bus0/bus1, DLC preservation, replay diagnostics, graph selection stability, and live transport diagnostics.
- Do not claim vehicle bus stability or vehicle control success from this gate.

Acceptance:
- Release build, full `ctest`, startup smoke, portable smoke, and CSM PlatformIO build pass in the current turn.

## 4. Deferred / Field-Only Items

- Fresh HIL/vehicle bus0/bus1 hot-plug stability.
- Fresh vehicle control TX success, judged only by matching `CAN_TX_RAW`.
- Android USB-OTG or wireless CSM transport feasibility.
- WiX MSI and code signing; portable folder remains the field artifact.

## 5. Verification Commands

VSM from standalone workspace:
```powershell
cmake --preset vs-release-qt6
cmake --build --preset build-release
ctest --test-dir out/build/x64-Release --output-on-failure
```

VSM from a fresh standalone clone:
```powershell
$env:CAN_MONITOR_QT_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
cmake --preset vs-release-qt6
cmake --build --preset build-release
ctest --preset test-release
```

CSM firmware:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm
```

HIL manual gate:
- CSM emits `CAPABILITY`.
- CSM emits fresh `BOARD_HEALTH`.
- VSM sees bus0/bus1 `CAN_RX_RAW`.
- VSM sends host command and receives `CONTROL_ACK`.
- Actual send is accepted only after matching `CAN_TX_RAW`.
- External feedback is checked through separate `CAN_RX_RAW`.

## 6. Risks Held

- Do not claim vehicle control completion from Pcan/Kvaser bench only.
- Do not remove legacy `.bin` replay/import.
- Do not hard-code bus0/bus1 as System/Drive.
- Do not treat voltage/health/control ack as fake CAN frames.
- Do not add closed-loop control before model-backed safety policy and feedback scale are locked.
- Do not bury CSM firmware dirty changes when publishing VSM; commit them explicitly or leave them visible.

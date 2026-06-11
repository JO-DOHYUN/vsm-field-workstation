# BUILD_VERIFY_POLICY_KO

이 문서는 VSM 단독 repository에서 Codex가 보고해야 하는 빌드/검증 기준이다.
상위 운영 맵은 [[../../AGENTS]], 현재 기준은 [[../../BRIEF]]를 따른다.

## VMS Baseline

- Windows + MSVC 2022 x64
- Qt 6.10.2 baseline
- Active VSM repository root: this folder, local path `C:\WORKS\VS\turn81_full_buildfix2`
- CSM firmware workspace is separate and used only for explicit CSM firmware gates.
- `CMakePresets.json` and local `CMakeUserPresets.json`
- `CAN_MONITOR_QT_PREFIX_PATH` or existing local Qt preset path

## Standard Commands

```powershell
cmake --preset vs-release-qt6
cmake --build --preset build-release
ctest --test-dir out/build/x64-Release --output-on-failure
```

Portable smoke when deploy behavior is touched:

```powershell
scripts\deploy_release.bat out\build\x64-Release out\build\x64-Release\portable_check
```

## Verification Ladder

Use the smallest level that proves the changed path, then escalate only when the touched surface requires it or a gate fails.

- Level 0: docs, constants, text-only, or narrow QML wording. Run `git diff --check` and targeted static search. Do not run Qt build unless imports/types changed.
- Level 1: one subsystem without runtime boundary movement. Build the changed target when needed and run only matching tests.
- Level 2: `AppController`, `SerialWorker`, `TypedRecords`, CMake/QML registration, control/transport boundary, new C++ file, or QML state role change. Run Release build plus the relevant `ctest -R` subset.
- Level 3: PR-ready state, runtime split completion, release/deploy change, or broad cross-subsystem slice. Run Release build, full `ctest`, and release exe startup smoke. Add CSM PlatformIO build only when shared protocol/control firmware compatibility is touched or explicitly requested.

Recommended subset map:

- typed/protocol: `typed_transport_foundation|typed_replay_reader|serial_worker_typed_ingest`
- control: `control_command_encoder|control_slew_limiter|app_controller_log_flow`
- replay/source: `replay_engine|app_controller_replay_flow|app_controller_analysis_source_flow`
- graph/QML/operator UI: `qml_shell_smoke|analysis_semantics`
- model/export: `model_pack_validator|app_controller_export_snapshot`

Executable smoke for Level 3:

```powershell
$exe = Resolve-Path 'out\build\x64-Release\can_monitor_qml_reboot.exe'
$p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 5
if ($p.HasExited) { throw "startup failed: $($p.ExitCode)" }
Stop-Process -Id $p.Id -Force
```

Optional CSM firmware gate:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm
```

## Reporting Rule

- Do not claim build/test/run success unless the command was run in the current turn.
- If only docs/protocol markdown changed, build may be skipped, but link/path/static consistency checks should be reported.
- If `AppController`, `SerialWorker`, `TypedRecords`, CMake, QML registration, or any new C++ file changes, run build and relevant tests.
- If HIL is not physically performed, state it as unverified.
- VSM 고부하/실차 수집 안정성은 direct COM typed-stream PASS로 대체하지 않는다. PASS는 VSM exe 실행, VSM command/UI path로 connect/start/stop logging, 새 `capture.stream` finalize, 최종 capture parse/sequence compare까지 통과해야 한다.
- Live/UI projection sampling/drop은 typed CRC/gap/resync, CSM `can_drop`, FIFO overflow와 다른 진단 축으로 보고해야 한다.
- Successful commands are summarized by command and result only. Do not paste include traces, deploy copy logs, or full passing test output.
- On failure, report the failing command, the first actionable compiler/test error, and the next smallest recovery step. Filter MSVC include noise before reporting.

## Vertical Slice Rule

- A turn goal should complete one operator-visible axis, not a list of incidental files.
- A completed slice must include at least three of: UI/user workflow change, evidence/model contract change, regression coverage, executable smoke.
- If a mid-turn user issue is higher severity, keep the current slice alive and add the issue only when it can be proven within the same verification level or one controlled escalation.

## Runtime Boundary Rule

- If a change adds storage, replay, transport, board health, or control audit responsibility that belongs to a runtime, include the runtime boundary plan and test surface in the same slice.
- Do not park new path/session/replay ownership in `AppController` as a convenience if `StorageRuntime`, `ReplayRuntime`, `TransportRuntime`, or `ControlAuditModel` is the intended owner.
- If temporary placement is unavoidable, document the owner, extraction target, rollback condition, and required verification before calling the slice complete.
- The next code cleanup priority is moving `RuntimePaths`, project-local log/replay path ownership, replay-open cache, and related session path decisions out of `AppController`.

## Build-Risk Surfaces

- `AppController.h/.cpp`
- `SerialWorker.h/.cpp`
- `TypedRecords.h/.cpp`
- `TypedTransportParser.h/.cpp`
- `ControlCommandEncoder.h/.cpp`
- CMake/QML module/import/type registration
- new signal/property/slot or `Q_PROPERTY`

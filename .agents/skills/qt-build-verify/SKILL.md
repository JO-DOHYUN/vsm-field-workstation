---
name: qt-build-verify
description: Use when building this Qt/CMake Windows app, fixing compile/link errors, running ctest, creating portable deploy folders, or doing startup smoke checks. Do not use for harness redesign or graph/replay semantics work unless build verification is the main task.
---

# qt-build-verify

## Start
1. Read `BRIEF.md`.
2. Identify whether the change touches build-risk files.
3. Use the canonical workspace and Windows build shell before running CMake.
4. Choose the smallest verification that proves the changed path.

## Verification Ladder
- Level 0: docs/text/constants/narrow QML wording. Use `git diff --check` plus targeted search; skip build unless imports, registered types, or generated files changed.
- Level 1: one subsystem. Build the affected target if needed and run the smallest matching `ctest -R` subset.
- Level 2: `AppController`, `SerialWorker`, `TypedRecords`, CMake, QML registration, new C++ files, control/transport boundaries, or QML state roles. Run Release build plus relevant subset tests.
- Level 3: PR-ready, release/deploy, or completed runtime boundary slice. Run Release build, full ctest, and release exe startup smoke. Add active CSM PlatformIO build only when shared protocol/control firmware compatibility is touched or explicitly requested.

Subset defaults:
- typed/protocol: `typed_transport_foundation|typed_replay_reader|serial_worker_typed_ingest`
- control: `control_command_encoder|control_slew_limiter|app_controller_log_flow`
- replay/source: `replay_engine|app_controller_replay_flow|app_controller_analysis_source_flow`
- graph/QML/operator UI: `qml_shell_smoke|analysis_semantics`
- model/export: `model_pack_validator|app_controller_export_snapshot`

## Workspace
- Active implementation/build workspace: this standalone VSM repository root, local path `C:\WORKS\VS\turn81_full_buildfix2`.
- Run Qt/CMake commands in the active VSM repository.
- Run CSM PlatformIO commands from the separate CSM firmware workspace only when the firmware gate is explicitly required.

## Commands
Windows rule:
- Do not run bare `cmake --build ...` from ordinary PowerShell for MSVC/Qt builds. It can find `cl.exe` but miss MSVC `INCLUDE`/`LIB`, causing false failures such as missing `type_traits` or `utility`.
- Always run configure/build/test through `VsDevCmd.bat`, or an already initialized Visual Studio developer shell.
- If `cmake` is not on `PATH`, use Visual Studio bundled CMake:
  `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

Release:
```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && set CAN_MONITOR_QT_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64&& ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset vs-release-qt6"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && set CAN_MONITOR_QT_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64&& ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset build-release"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --test-dir out/build/x64-Release --output-on-failure"
```

Debug:
```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && set CAN_MONITOR_QT_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64&& ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset vs-debug-qt6"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && set CAN_MONITOR_QT_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64&& ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset build-debug"
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --test-dir out/build/x64-Debug --output-on-failure"
```

Portable:
```powershell
scripts\deploy_release.bat out\build\x64-Release out\build\x64-Release\portable_typed_check
```

Startup smoke:
```powershell
$exe = Resolve-Path 'out\build\x64-Release\can_monitor_qml_reboot.exe'
$p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 5
if ($p.HasExited) { throw "startup failed: $($p.ExitCode)" }
Stop-Process -Id $p.Id -Force
```

CSM firmware:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm
```

## Rules
- Do not claim build/test/deploy success unless the command was run in this turn or clearly inherited as historical state.
- Keep unrelated UI/docs cleanup out of build-fix turns.
- If `QTP0004` appears, report it as known dev warning unless it blocks generation.
- If `CMakeLists.txt`, QML registration, or new C++ files changed, verify declaration/definition/CMake/QML registration together.
- Prefer subset-first verification while iterating; use full ctest only for Level 3 or when a smaller gate cannot prove the affected behavior.
- For QML usability issues such as graph selection, scroll position, visible DLC, ControlPage evidence states, or replay navigation, prefer a QML state probe/smoke test over manual visual claims.
- Keep token output small: summarize successful configure/build/deploy output; paste only failing command excerpts and final error lines.
- When MSVC `/showIncludes` output is noisy, filter to `FAILED`, `error`, `fatal`, `LNK`, and the final failing target before reporting.
- For GitHub Actions after push, one status check is enough unless it failed or the user explicitly asks to wait for the remote gate.

## Output
- commands run
- pass/fail result
- unverified surfaces
- next smallest recovery step if failing

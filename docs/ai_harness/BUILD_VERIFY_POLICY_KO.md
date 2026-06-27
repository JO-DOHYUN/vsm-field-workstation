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
- capture-core memory: `drain_byte_queue|typed_ingress_parser_only|typed_capture_writer_runtime|serial_worker_typed_ingest|analysis_worker_runtime|raw_ledger_runtime|transport_runtime_foundation`
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

## Capture-Core Memory Gate

Use `.agents/skills/capture-core-memory/SKILL.md` when touching live capture hot path, raw byte/slab ownership, bounded queue, typed frame fanout, writer handoff, analysis handoff, projection snapshot, or process memory telemetry.

For this surface, a normal Release build and full `ctest` prove only compile/regression safety. They do not prove the reported field failure is fixed.

Minimum completion evidence:

- Release build passes.
- Full `ctest` passes.
- Startup smoke passes if app/runtime wiring changed.
- `py -3 scripts/check_vsm_boundary_rules.py --mode transition` is run and its owner-boundary debt is reported for broad live/capture-core slices.
- If hardware/HIL is unavailable, report memory fix as structurally implemented but HIL-unverified.
- If hardware/HIL is available, run `docs/runbooks/VSM_CAPTURE_CORE_MEMORY_VERIFY_KO.md` matrix at least through 10m, and 1h before field-final claim.

Never claim capture-core memory completion if `TypedRecordList` remains the main live fanout object, if full typed batches can accumulate in Qt queued events outside telemetry, or if private memory grows linearly in a 10m/1h run.
Never claim long-run VSM architecture completion if UI receives raw/typed stream directly, if a `GetView`/view query performs full capture scan on demand, or if debug/gateway/tap writer paths execute in normal live mode.
Never claim final 2+1 architecture closure until `py -3 scripts/check_vsm_boundary_rules.py --mode strict` passes or every remaining finding is explicitly removed from the production path.

## Reporting Rule

- Do not claim build/test/run success unless the command was run in the current turn.
- If only docs/protocol markdown changed, build may be skipped, but link/path/static consistency checks should be reported.
- If `AppController`, `SerialWorker`, `TypedRecords`, CMake, QML registration, or any new C++ file changes, run build and relevant tests.
- If HIL is not physically performed, state it as unverified.
- VSM 고부하/실차 수집 안정성은 direct COM typed-stream PASS로 대체하지 않는다. PASS는 VSM exe 실행, VSM command/UI path로 connect/start/stop logging, 새 `capture.stream` finalize, 최종 capture parse/sequence compare까지 통과해야 한다.
- Debug gateway mode는 VSM crash/hang 시 raw serial evidence를 남기는 장치일 뿐 최종 PASS 대체물이 아니다. PASS는 gateway artifacts와 VSM 최종 `capture.stream`/`result.json` 둘 다 통과해야 한다.
- `scripts/vsm_verify.py`는 공식 실행기 카탈로그다. HIL/debug/report 스크립트를 추가하면 이 카탈로그와 deploy 포함 여부도 같이 갱신한다.
- `process_metrics.csv`는 프로세스 수준 메모리/CPU만 의미한다. 모듈별 병목 주장은 `PerformanceProbeRuntime` snapshot/counter 근거가 있을 때만 한다.
- Debug gateway raw capture와 PerformanceProbeRuntime은 역할이 다르다. gateway PASS로 UI/backend 병목이 없다고 말하지 말고, profiler PASS로 capture 무결성을 대체하지 않는다.
- Live/UI projection sampling/drop은 typed CRC/gap/resync, CSM `can_drop`, FIFO overflow와 다른 진단 축으로 보고해야 한다.
- 성능 때문에 사실값을 바꾸면 실패다. timing/value/alarm/DLC/control evidence는 sampled/coalesced UI projection에서 계산하지 않는다.
- 표시량 감소는 허용하지만 truth 계산 입력 유실은 허용하지 않는다. analysis queue overflow는 `truth_loss` 또는 `analysis_overrun`으로 명확히 보고해야 한다.
- 표시 지연, recent raw row 생략, graph bucket 축약은 UI에 명시해야 하며, 이 상태를 parser/storage/CAN success로 혼동해 보고하지 않는다.
- QML/UI는 raw stream consumer가 아니라 snapshot/diff renderer다. QML binding이 timing/value/alarm 계산이나 raw frame fanout을 유발하면 실패로 본다.
- UI는 Core-owned Data Plane의 query consumer다. `ViewChanged`는 cheap notification이어야 하며, heavy materialized view는 bounded `GetView(view_name, since_seq, limit)` 응답으로만 이동해야 한다.
- Live 고부하 경로에서 `LiveProjectionRuntime`/`LiveTruthRuntime` coalesced display rows를 timing/value/alarm truth 입력으로 다시 쓰면 실패다. truth 입력은 `AnalysisRuntime` 또는 그 후속 runtime이 전량 소비해야 한다.
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
- High-load live analysis belongs in a truth runtime, not in UI projection or raw `AppController::ingestFrame()` fanout. The intended key is `bus + canId + ext + rtr`.
- Debug capture that must survive VSM crash/hang belongs in a separate recorder/gateway process. Normal live mode must not execute gateway writer/forwarder code paths.

## Build-Risk Surfaces

- `AppController.h/.cpp`
- `SerialWorker.h/.cpp`
- `TypedRecords.h/.cpp`
- `TypedTransportParser.h/.cpp`
- `ControlCommandEncoder.h/.cpp`
- CMake/QML module/import/type registration
- new signal/property/slot or `Q_PROPERTY`

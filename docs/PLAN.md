# CAN Monitor Production-Readiness Master Plan

## 1. Product Bar and Invariants
- Product target: Windows desktop evidence-first CAN monitor/logger/replay/decode/control workstation for vehicle validation teams
- Invariants:
  - raw bridge semantics preserved
  - legacy 20-byte packet, CRC8, DLC, `t_us` wrap semantics preserved as replay/import compatibility mode
  - live production stream is CSM typed evidence stream only
  - COM open is not board alive; valid `CAPABILITY` is required
  - typed board stream bytes preserved as immutable production truth
  - live/replay meanings stay separated
  - graph truth, fixed-axis meaning, and peak preservation stay intact
  - recent-window graph and full-range overview graph remain separate tools
  - voltage/raw sensor evidence is never disguised as CAN
  - requested control TX, `CONTROL_ACK`, `CAN_TX_RAW`, and feedback remain separate evidence
  - requested control TX is never treated as actual sent until matching board `CAN_TX_RAW` audit exists

## 2. Current Gap Assessment
- Shared `CMakePresets.json`, CI skeleton, runtime paths, build metadata, logging categories, model-pack validation, and initial traceability docs now exist
- Automated coverage now exists and currently includes 19 passing tests across legacy packet parsing, model validation, persistence, typed transport/storage foundation, typed replay, replay, analysis, QML shell smoke, and AppController flows
- Typed board stream contract is now locked in code at foundation level: CRC16/resync parser, typed record decode helpers, fixture bytes, and append-only storage skeleton
- `AppController` is still a large monolith and remains phase-2 work
- Live typed-only transport hardening, typed replay parity beyond CAN_RX projection, capability/health UI, voltage graph sidecar, and safe control audit chain remain future work
- WiX MSI generation is still a hook/stub; portable deploy is verified, installer is not

## 3. Work Packages by Phase
### Phase 1: Build, Test, Logging, Validation Foundation
- Done: shared `CMakePresets.json`
- Done: Windows CI skeleton using shared presets
- Done: initial `ctest` coverage
- Done: `RuntimePaths`, `BuildMetadata`, and `AppLogging`
- Done: `ModelPackValidator`
- Done: packaging placeholders for SBOM / third-party notices / WiX hook

### Phase 2: Runtime Split
- Reduce `AppController` to QML facade + orchestration
- Introduce `TransportRuntime`, `StorageRuntime`, `ReplayRuntime`, `AnalysisRuntime`, `EvidenceRuntime`, `ControlRuntime`, `GraphRuntime`, `OperatorRuntime`, `UiStateStore`
- Done: add typed transport parser while retaining the legacy 20-byte parser
- Done: add typed record structs for `CAN_RX_RAW`, `CAN_TX_RAW`, `ADC_SAMPLE`, `BOARD_EVENT`, `BOARD_HEALTH`, and `CAPABILITY`
- Done: typed replay reader and AppController replay loading now accept typed capture sessions through `typed_capture_*.typed/capture.stream`
- Next: finish runtime split, keep live transport typed-only, and keep legacy `.bin` replay/import separate

### Phase 3: Persistence and Diagnostics Hardening
- Done: atomic write paths for BIN/meta/model snapshots
- Done foundation: typed stream storage skeleton for `capture.stream.part`, `capture.index.part`, `session.meta.json`, `events.jsonl`
- Better operator-facing diagnostics versus engineer logs
- Failure-state tables and degraded-mode visibility
- Done foundation: typed capture session can be loaded into replay as `CAN_RX_RAW` projection
- Next: capture/replay parity gates for full typed stream sessions, including offsets, seq, type counts, events, and first/last `mono_us`

### Phase 4: Packaging and Release Discipline
- Portable deploy folder as standard field artifact
- WiX MSI packaging
- Signing hook, artifact metadata, release checklist, and traceability gating

## 4. Test and Acceptance Matrix
- Unit:
  - `PacketParser`
  - `ModelPackValidator`
  - `UiStateStore`
  - `FilePersistence`
  - `TypedTransportParser`
  - `StorageRuntime`
  - `Recorder`
  - `ReplayEngine`
  - `SignalDecoder` / `TimingEvaluator` / `AlarmManager` semantics
- Component:
  - `AppController` replay flow
  - `AppController` log flow
  - `AppController` analysis-source flow
  - `AppController` export snapshot flow
- Manual acceptance retained this phase:
  - live connect/disconnect
  - BIN logging start/stop/save/discard
  - replay load/play/pause/seek
  - timing/value/alarm coherence
  - graph nested zoom and rollback flow
- Typed board acceptance:
  - typed stream CRC16/resync: foundation automated
  - exact framed bytes persisted append-only: foundation automated
  - typed replay parity
  - typed capture session folder/file open
  - capability/health/drop/fault visibility
  - voltage raw and calibrated sidecar separation
  - control audit chain before operator control UI

## 5. Release and Deploy Process
- Shared preset configure/build path is now the repository baseline
- CI runs Debug/Release configure/build/test and a portable deploy smoke step
- `deploy_release.bat` remains the portable release generator
- `package_wix_installer.bat` is introduced as the MSI-stage hook and clearly fails until `.wxs` authoring is added

## 6. Traceability and Coding Rules
- See:
  - `docs/BOARD_QT_FINAL_ARCHITECTURE.md`
  - `docs/quality/architecture_map.md`
  - `docs/quality/requirements_traceability.md`
  - `docs/quality/release_checklist.md`
  - `docs/quality/coding_rules.md`

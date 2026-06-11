# BRIEF.md

## Current Baseline
- Latest user-designated baseline: `turn81_full_buildfix2 (5엔코더4선일관성).zip`
- Current workspace: direct-edit project folder, no Git metadata in this folder
- Build and verification status checked in this turn:
  - shared `CMakePresets.json` now exists
  - local override presets now live in `CMakeUserPresets.json`
  - `local-vs-release-qt6` configure + `build-release-local` + `ctest --test-dir out/build/x64-Release --output-on-failure` passed
  - `local-vs-debug-qt6` configure + `build-debug-local` + `ctest --test-dir out/build/x64-Debug --output-on-failure` passed
  - `scripts\deploy_release.bat out\build\x64-Release out\build\x64-Release\portable_release_check` passed
  - `scripts\deploy_release.bat out\build\x64-Release out\build\x64-Release\portable_typed_check` passed for the typed-foundation build
  - `out/build/x64-Release/can_monitor_qml_reboot.exe` started and stayed alive for 5 seconds in a startup smoke check
  - `out/build/x64-Release/portable_typed_check/can_monitor_qml_reboot.exe` started and stayed alive for 5 seconds in a portable smoke check

## Program Completion Goal
- Move from feature-by-feature patching to program-level completion.
- Finish remaining ship blockers without regressing live/replay semantics, graph truth, or deployment reliability.
- Use the current implementation as the source of truth, then tighten docs, verification, and the last unresolved UX/runtime edges.
- New board/Qt convergence target: evidence-first control workstation using typed board records as production truth while preserving legacy 20-byte captures as compatibility mode.

## Main Goal
- Reach a state where the CAN monitor can be treated as a shippable integrated tool for:
  - live monitoring
  - BIN logging/save flow
  - replay load/seek/issue navigation
  - timing/value/alarm analysis
  - recent-window graph + full-range overview graph
  - Windows build/deploy handoff

## Subgoals
- Close the remaining `전체그래프` usability loop and verify that the nested zoom flow is genuinely complete.
- Re-run a practical completion sweep across live/logging/replay/analysis/deploy paths and convert stale planning notes into current truth.
- Start production-readiness work in safe tranches rather than one-shot broad rewrites.

## Active Constraints
- Existing `그래프` tab behavior for live/replay recent-window monitoring must remain intact.
- `전체그래프` must stay a fixed full-file overview; nested zoom must not trigger continuous heavy rebuild work.
- No feature deletions to hide performance or correctness issues.
- Build-risk edits must stay tightly scoped when they touch `AppController`, QML registration, `main.cpp`, or `CMakeLists.txt`.
- Values/model correctness and program structure/completion are still treated as separate tracks.

## Source of Truth
- Baseline source: `turn81_full_buildfix2 (5엔코더4선일관성).zip`
- Operating rules: `AGENTS.md`, `docs/CODEX_HARNESS_KO.md`, `.agents/skills/main-code-progress/SKILL.md`
- Board architecture handoff source: `J_ArdP7_AM2_CSM (3).zip`
- Board/Qt final decision doc: `docs/BOARD_QT_FINAL_ARCHITECTURE.md`
- Data-pack handoff present in workspace: `DATA_CHAT_HANDOFF_KO.md`
- Previous room handoff referenced by older BRIEF text:
  - `QT_코드수정4_임시_프로젝트전반검토_및_인수단계.md`
  - current workspace does not contain this file, so do not treat it as available source in this turn
- Current implementation in `src/`, `qml/`, `data/`, the current build trees, and the newly added docs/quality files is the working source of truth

## Current State Review
- Core app surface already exists:
  - live connect/disconnect
  - BIN logging start/stop/save
  - replay load/play/pause/seek
  - timing/value/alarm pages
  - recent-window graph page
  - full-range `전체그래프` page
  - snapshot/export entry points
  - Windows Release output/deploy helper files in `out/build/x64-Release`
- Current graph-overview implementation already contains:
  - overview drag selection
  - lower detail graph rebuild per selected range
  - nested selection history
  - one-step back / root rollback controls
  - selection state normalization when overview data disappears or is rebuilt
- Current UI stabilization work already includes:
  - single-screen default startup mode
  - manual display-scale selector restored as `90% / 100% / 110%`, with `100%` as the operator baseline and no auto-fit path
  - compact replay bar recovery after the failed over-wide layout attempt
  - page minimum-width rules decoupled from display zoom
  - graph axis padding/edge-label visibility cleanup plus QML hit-testing aligned to renderer padding
  - failed global vertical-scroll fallback removed
  - `전체그래프` returned to a fixed left signal list + right overview/detail stack
  - bottom help overlays that covered graph content removed
- Production-readiness tranche 1 now exists:
  - shared `CMakePresets.json`
  - Windows CI workflow skeleton
  - first automated tests and `ctest` coverage
  - `BuildMetadata`, `RuntimePaths`, and `AppLogging`
  - `ModelPackValidator`
  - packaging placeholder artifacts for SBOM / third-party notices / WiX hook
  - versioned `UiStateStore` with legacy session-key fallback
- Board/Qt typed evidence foundation tranche now exists:
  - `TypedTransportParser` parses `0xA5 0x5A` framed records with board-compatible CRC16-CCITT, bounded length handling, sequence-gap counters, CRC-failure counters, and SOF resync
  - `TypedRecords` decodes priority evidence payloads without converting voltage or board health into fake CAN frames
  - `StorageRuntime` can append exact typed frames to `capture.stream.part`, write sparse index entries, keep `session.meta.json.part` / `events.jsonl.part`, and finalize by dropping `.part`
  - typed fixture `tests/fixtures/typed_stream_v1.hex` locks CAN_RX_RAW + ADC_SAMPLE bytes against the board contract

## Completion Invariants
- raw bridge philosophy remains intact
- 20-byte packet / CRC8 / DLC / `t_us` wrap semantics remain intact
- legacy 20-byte packet semantics remain supported but are no longer the final production transport target
- typed board stream bytes are immutable truth for the next production architecture
- voltage, health, events, control acknowledgements, and CAN frames remain separate evidence types
- a requested control TX is not successful until `CAN_TX_RAW` confirms actual board hardware write
- live and replay semantics remain separated
- graph rendering must not change raw truth, peak meaning, or fixed-axis meaning
- live/replay recent-window graph and full-range overview graph remain separate tools
- full-range overview is computed once and remains visually fixed while replay moves only the cursor line
- nested zoom is an inspection layer on top of the fixed overview, not a replacement for it
- timing issues and alarm meaning must not be conflated

## Verified This Turn
- shared preset files are now committed and usable
- Debug and Release configure/build/test pass with local override presets
- `ctest` is no longer empty; twelve tests now run and pass:
  - `packet_parser`
  - `model_pack_validator`
  - `ui_state_store`
  - `file_persistence`
  - `typed_transport_foundation`
  - `recorder_flow`
  - `replay_engine`
  - `analysis_semantics`
  - `app_controller_replay_flow`
  - `app_controller_log_flow`
  - `app_controller_analysis_source_flow`
  - `app_controller_export_snapshot`
- portable deploy helper regenerates a release folder and now carries packaging notices/hooks
- Release exe startup smoke still passes

## Remaining Risks Seen Now
- `AppController` architecture split has not started yet; façade/orchestration-only target remains future work.
- WiX MSI generation is still a hook/stub stage; portable release is verified, installer artifact is not.
- `QTP0004` CMake dev warning remains for extra QML directories; it does not block build/test but should be cleaned later.
- The user still needs to visually confirm that `100%` layout, `전체그래프` sidebar, and replay bar density are acceptable after the recent UI recovery work.
- Full integrated operator flow is still not re-verified in this turn:
  - live serial connection and disconnect
  - logging start/stop/save
  - replay load and issue navigation
  - timing/value/alarm pane coherence under replay/live switching
  - full-range graph nested zoom undo/redo feel
  - MSI-style installer flow

## Working Features To Preserve
- live monitoring screen and recent frame views
- temporary BIN logging and save/cleanup flow
- replay cursor, seek, speed, and issue navigation
- timing/value/alarm table structure and filter/sort behavior
- existing `그래프` recent-window graph behavior
- separate `전체그래프` tab structure
- detail rebuild based on selected full-range interval rather than simply cropping the overview drawing

## Completion Plan
1. Board/Qt typed evidence foundation
   - typed transport parser with CRC16/resync tests is implemented
   - typed record structs and typed stream fixture are implemented
   - typed storage skeleton for append-only `capture.stream.part` is implemented
   - legacy 20-byte parser/replay/logging tests remain green
   - next: wire typed serial capture behind an explicit transport mode and add typed replay parity
2. Integrated regression sweep
   - re-check live/logging/replay/timing/value/alarm transitions
   - confirm replay/live state separation under navigation and pause/hold flows
   - verify snapshot/export and operator guidance text still match behavior
3. Graph close-out
   - verify the current nested zoom flow against real operator use
   - fix only the remaining confusion points or rollback friction if found
   - keep overview fixed and rebuild policy unchanged
4. Architecture split tranche
   - start extracting `AppController` runtime ownership without breaking QML surface
   - separate transport/storage/replay/analysis/evidence/control/graph/ui-state responsibilities
5. Packaging/traceability tranche
   - convert WiX hook into a real installer pipeline
   - grow tests, requirements traceability, and release checklist coverage

## Immediate Next Task
- Treat the current state as completion-phase foundation + first production-readiness tranche delivered, with board/Qt typed evidence architecture now selected as the next main direction.
- Next execution focus:
  - wire typed serial capture behind an explicit transport mode before any control UI work
  - add typed replay reader and parity tests for record count, sequence, offsets, type counts, and first/last `mono_us`
  - preserve legacy 20-byte live/logging/replay behavior while adding typed path side-by-side
  - keep graph fixes limited to concrete operator friction only
  - start control only after typed capture/replay parity, capability/health visibility, and safety audit chain are implemented

## Acceptance For This Completion Phase
- Verified items and unverified items are explicitly separated.
- Program completion work is organized as one main goal with limited subgoals, not scattered patch notes.
- No false claim of full completion is made before live/build/replay/graph/deploy checks are actually run.
- Production-readiness work is now backed by real presets/tests/docs rather than plan-only notes.

## Historical Note
- turn89 build-fix restored missing `unitSet` declarations/usage in `AppController.cpp` after the detail-zoom patch path regressed build safety.
- The recent UI recovery work preserved the `전체그래프` sidebar + overview/detail structure after a failed reflow attempt.
- current turn: production-readiness tranche 1 added shared presets, Windows CI skeleton, first `ctest` coverage, runtime paths/build metadata/session logging, model-pack validation, packaging placeholder artifacts, and verified Debug/Release configure-build-test plus portable deploy.

## Current Turn Addendum
- User-designated active model/rule baseline: `data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json`
- Bundled/default model path is now aligned to that turn77 merged baseline.
- `ModelPackValidator` no longer rejects repeated reserved/placeholder signal labels such as `not defined` / `예비`.
- Release build + `ctest` still pass after this change.
- Release startup smoke log now confirms: `Loaded model pack HYM HAMT2.0 R13 SYSTEM+DRIVE (turn77 final merged)`.
- Startup warning cleanup also landed:
  - Quick Controls now run under Fusion style for deterministic custom backgrounds
  - `HoverHandler` tooltip binding no longer emits `undefined -> bool` warnings
  - snapshot save dialog no longer emits a nonexistent selected-file warning on startup
- Session/UI persistence tranche also landed:
  - `AppController` session restore/save now goes through `UiStateStore`
  - stored UI state is versioned and serialized as a single blob
  - legacy scattered session keys still load as fallback for compatibility
  - invalid replay speed in restored state is normalized back to `1.0`
  - startup no longer reloads the bundled turn77 merged model twice when it is already active
- Save/persistence hardening tranche also landed:
  - new `FilePersistence` helper provides atomic JSON/text writes and staged file copy into final paths
  - recorder metadata JSON is now written atomically, and metadata write failure aborts log start instead of silently leaving partial temp state
  - recorder stop now emits a surfaced warning when model/rules snapshot copy fails
  - pending log finalize now copies temp files into final paths before removing sources, so partial save failure no longer loses the original temp BIN/meta/model files
  - Release/Debug build + `ctest` still pass after this change, and portable deploy + Release startup smoke were re-verified
- Recorder/worker automated coverage also landed:
  - `recorder_flow` now verifies successful BIN/meta/model snapshot output
  - invalid metadata target path now fails log start and cleans the temp BIN
  - missing model snapshot source now remains a surfaced stop-time warning
  - `SerialWorker` now has an automated signal-path check for recorder stop warnings
- Replay/analysis automated coverage also landed:
  - `replay_engine` now verifies replay file load, cursor publication, seek/step clamp behavior, and non-loop playback completion
  - `analysis_semantics` now verifies timing WARN/ERR escalation, timing alarm-key generation, value-alarm reserved/range handling, inactive-flag suppression, and detail-row threshold context
- AppController replay/operator integration coverage also landed:
  - `app_controller_replay_flow` now verifies custom model load, replay BIN load, rebuild to target frame, replay timing/value/alarm marker generation, and replay issue seek/focus flow through the real `AppController` surface
- AppController logging/operator integration coverage also landed:
  - `app_controller_log_flow` now verifies `startLog -> stopLog -> finalizePendingLogSave` artifact copy flow and `startLog -> stopLog -> discardPendingLog` temp cleanup flow through the real `AppController` surface
- AppController analysis-source/operator integration coverage also landed:
  - `app_controller_analysis_source_flow` now verifies connected-live + replay-loaded 상태에서 `liveUiPaused`, `pauseReplay`, `useLiveAnalysis` 전환에 따라 source별 filter/frame-filter 상태가 분리 보존되고 다시 복원되는 flow through the real `AppController` surface
- AppController export/operator snapshot coverage also landed:
  - `app_controller_export_snapshot` now verifies that snapshot JSON and Markdown exports carry the current analysis source, analysis context, active view summary, replay cursor summary, operator headline/action text, selected issue context, and generated replay issue markers through the real `AppController` surface
- Board/Qt convergence architecture is now selected:
  - final direction is `Evidence-first Control Workstation`
  - legacy 20-byte stream remains compatibility mode
  - typed board stream becomes production truth
  - typed transport foundation is now implemented and verified:
    - `TypedTransportParser + TypedRecords + StorageRuntime`
    - board-compatible CRC16-CCITT with init `0xFFFF`
    - CAN_RX_RAW, CAN_TX_RAW, ADC_SAMPLE, BOARD_EVENT, BOARD_HEALTH, and CAPABILITY payload decode helpers
    - append-only typed stream storage skeleton with sparse binary index
    - Release/Debug configure-build-test passed with 12/12 tests
    - Release direct startup smoke and portable startup smoke both passed for 5 seconds
  - next code tranche is typed serial capture mode + typed replay parity, still before any control UI
- Remaining manual check for the user:
  - verify explicit file-pick model selection and bundled-model reset from the UI
  - confirm decoded values/graphs now follow the turn77 merged baseline as expected

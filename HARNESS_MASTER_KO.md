---
kind: master
scope: harness
updated: 2026-07-01
read_when:
  - harness redesign
  - instruction conflict
  - skill boundary change
  - onboarding
---
# HARNESS_MASTER_KO

이 문서는 VSM harness의 상위 구조다. routine 코드 수정 때는 읽지 않고,
하네스/문서/skill 경계 변경 때만 읽는다.

## Harness Goal

Codex 작업이 과거 실패 이력이나 임시 패치 흐름으로 되돌아가지 않도록 현재 제품
기준을 짧고 명확하게 고정한다. 현재 제품 기준은 Passive-Safe 2+1이다.

## Product Baseline

- VSM field/product default:
  `vsm-ui.exe + vsm-capture-core.exe`.
- Optional debug:
  `vsm-debug-tap.exe`, default OFF, Core IPC read-only sidecar.
- CSM target:
  2-bus ACK-capable observe-only Passive Product firmware with host TX/control
  compiled out.
- Authoritative evidence:
  `capture.stream/index`.
- Hardware PASS:
  external analyzer/scope/DTC artifact verification required.

## Cross-Repo Rule

- VSM repo: `C:\WORKS\VS\turn81_full_buildfix2`.
- CSM repo: `C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM`.
- System-level passive, protocol, capability, board-health, USB/CAN lifecycle,
  and vehicle-impact-free work must inspect both repo states.
- Do not edit or build CSM from the VSM workspace.
- Do not edit or build VSM from the CSM workspace.
- Commit/push each repo independently.

## Verification Budget Rule

Builds are evidence, but unnecessary builds are waste and can disturb hardware.
Choose the smallest verification that proves the changed surface.

- Docs/harness-only: `git diff --check` plus targeted search. No build.
- VSM C++/QML/runtime change: Release build or affected target build plus
  relevant `ctest -R` subset.
- VSM completed runtime boundary/release slice: Release build, full ctest, and
  startup smoke.
- CSM docs-only: `git diff --check`. No PlatformIO build.
- CSM firmware/platformio/guard/protocol change: build only the affected env.
  Build passive alias/full env only when profile separation is part of the
  change.
- Upload is not a normal verification step. Upload only when explicitly
  requested and when the current hardware/vehicle context is safe for MCU reset
  and USB re-enumeration.

## Document Roles

- `AGENTS.md`: stable top-level rules, read order, routing.
- `START_HERE_KO.md`: current project entry and product identity.
- `BRIEF.md`: current baseline, must preserve, immediate next work.
- `INDEX.md`: document hub.
- `.agents/skills/*`: narrow reusable workflows.
- `docs/`: architecture, protocol, runbooks, acceptance contracts.
- `history/`: previous attempts, decisions, rollback notes.

## Mandatory Refactor Order

For live/capture/process/passive architecture work:

1. define data-flow path;
2. define owner / consumer / drop policy;
3. search owner violations;
4. add boundary DTO/interface;
5. move function behind the boundary;
6. delete old route;
7. add static/test guard.

File splitting without ownership closure is not accepted.

## Passive Product Forbidden Patterns

- Production VSM serial `ReadWrite` open outside `RuntimeProfile`.
- Passive Product RTS assert, host TX, control cycle, gateway TCP.
- UI consuming raw typed stream or full live `TypedRecordList`.
- AppController assembling transport runtime state from diagnostics payload.
- Debug/gateway/profiler writers running in normal production mode.
- One-bus passive product/acceptance.
- Claiming `verified_passive` from CSM capability fields without external proof.
- Treating ACK-observe capability as host TX/control capability, or treating a
  pre-session no-ACK state as a product TX failure.
- Treating build/upload success as vehicle-impact-free proof.

## Rollback Rule

If a harness change causes Codex to ignore current product identity, skip required
verification, overbuild without reason, upload during unsafe hardware context,
conflate debug evidence with capture truth, or route passive work through
lab/full-instrumented paths, revert that harness slice and restore this contract.

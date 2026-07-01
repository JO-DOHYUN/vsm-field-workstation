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
  2-bus RX-only Passive Product firmware.
- Authoritative evidence:
  `capture.stream/index`.
- Hardware PASS:
  external analyzer/scope/DTC artifact verification required.

## Document Roles

- `AGENTS.md`: stable top-level rules, read order, routing.
- `START_HERE_KO.md`: current project entry and product identity.
- `BRIEF.md`: current baseline, must preserve, immediate next work.
- `INDEX.md`: document hub.
- `.agents/skills/*`: narrow reusable workflows.
- `docs/`: architecture, protocol, runbooks, acceptance contracts.
- `history/`: previous attempts, decisions, rollback notes.

## Skill Routing

- Harness/doc boundary: `harness-maint`.
- Typed evidence/protocol/control gates: `typed-evidence`.
- Capture hot path, bounded queue, memory plateau: `capture-core-memory`.
- Build/test/startup smoke: `qt-build-verify`.
- Replay source semantics: `replay-semantics`.
- Graph renderer/performance: `graph-performance`.

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

## Rollback Rule

If a harness change causes Codex to ignore current product identity, skip required
verification, conflate debug evidence with capture truth, or route passive work
through lab/full-instrumented paths, revert that harness slice and restore this
contract.

## Current Required Links

- `docs/architecture/VSM_CSM_PRODUCT_IDENTITY_KO.md`
- `docs/architecture/PROJECT_CONSTITUTION_KO.md`
- `docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO.md`
- `docs/architecture/VSM_PASSIVE_SAFE_2PLUS1_ARCHITECTURE_KO.md`
- `docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO.md`
- `docs/reviews/passive_product_boundary_audit.md`
- `docs/hardware/CSM_PASSIVE_FRONTEND_REQUIREMENTS.md`
- `docs/hardware/CSM_PASSIVE_FRONTEND_ACCEPTANCE.md`

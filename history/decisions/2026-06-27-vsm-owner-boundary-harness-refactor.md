# 2026-06-27 VSM Owner Boundary Harness Refactor

## Reason
The VSM migration repeatedly split files and classes before fixing ownership.
That allowed old data flow to remain alive behind new names:

- full `TypedRecordList` and `FrameRecordList` fanout;
- `AppController` transport-state assembly;
- diagnostics payloads acting like runtime state;
- UI projection being confused with truth;
- storage `frameBytes` crossing into live display paths.

The harness now treats owner/data-flow definition as the first-class gate for
capture-core/live-path work.

## Decision
Add a mandatory owner-boundary contract and static transition scanner:

- `docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO.md`
- `scripts/check_vsm_boundary_rules.py`

Update the capture-core memory skill and top-level routing so future live-path
work starts with owner/consumer/drop policy, searches existing violations, adds
boundary DTOs first, moves behavior, deletes the old route, and adds a
regression guard.

## Expected Gain
- Codex work becomes harder to complete with only file/class reshuffling.
- Current transitional debt is visible before refactoring.
- Final 2+1 closure has a strict static gate in addition to build/HIL gates.

## Rollback Rule
Rollback this harness change only if the scanner blocks unrelated non-live
work or produces broad false positives that cannot be narrowed with path-based
rules. Do not rollback the owner-boundary contract itself without replacing it
with an equivalent stronger gate.

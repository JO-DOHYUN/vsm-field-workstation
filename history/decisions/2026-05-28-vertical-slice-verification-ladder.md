# 2026-05-28 Vertical Slice Verification Ladder

## Decision
- Codex VSM/CSM work will default to one operator-visible vertical slice per turn.
- Verification uses a four-level ladder instead of always running the full build and full test suite.
- QML/operator usability regressions must be backed by executable state probes or smoke tests when visual inspection is unreliable.
- Harness changes are limited to `BRIEF.md`, build verification policy, and the `qt-build-verify` skill.

## Reason
- Prior turns spent too many tokens on narrow file-by-file progress, noisy build logs, and repeated full verification.
- User-visible field behavior, especially graph usability, DLC display, control evidence state, and replay/log diagnosis, needs stronger executable checks.
- Full build/test remains necessary for PR-ready and runtime-boundary changes, but it is wasteful for every intermediate edit.

## Expected Gain
- Larger completed work units per turn.
- Less token waste from successful build/test output and MSVC include traces.
- Earlier proof of real operator workflow behavior through focused state probes.
- Clearer escalation from subset tests to full release gates.

## Rollback Rule
- Return to full build/full ctest on every code turn only if subset-first verification misses a regression twice in the same subsystem.
- Remove the vertical-slice rule only if it blocks urgent single-line hotfixes; document that exception in `BRIEF.md` before the turn starts.

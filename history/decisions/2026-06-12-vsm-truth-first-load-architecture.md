# Decision: Truth-First High-Load VSM Architecture

Date: 2026-06-12

## Decision

VSM high-load work will prioritize truth-first stream processing over UI projection shortcuts.
Performance work may reduce display frequency or displayed raw row count, but must not change factual timing/value/alarm/DLC/control evidence.

## Reason

The previous high-load mitigation bounded live projection and reduced UI pressure, but it also left live analysis dependent on snapshot/projection behavior and `canId`-only state in critical areas.
That can make a real 20 ms CAN period appear as 40/60 ms and can mix bus0/bus1 frames with the same ID.
For a monitoring workstation, this is worse than a delayed UI because it undermines factual observation.

## Expected Gain

- Memory use becomes bounded by observed keys, selected graph signals, and explicit rings rather than by frame count.
- UI can stay responsive under dual-CAN load without falsifying timing/value/alarm state.
- Log and debug capture become separate evidence paths instead of hidden live dependencies.

## Rollback Condition

If the new truth runtime cannot process target high-load without analysis overrun, do not fall back to silent sampling.
Rollback should restore the last known stable parser/storage path and mark VSM high-load user-route as failed until the runtime is optimized or the gateway/debug evidence proves another bottleneck.

## Harness Impact

Harness rules must reject:

- timing/value/alarm/DLC/control evidence derived from sampled UI projection
- direct COM-only PASS replacing VSM user-route PASS
- display drop counters being reported as parser/CSM drops
- hidden analysis queue overflow

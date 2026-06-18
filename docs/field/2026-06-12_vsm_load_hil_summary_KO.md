# 2026-06-12 VSM Load HIL Summary

## Summary

VSM high-load crash/hang route was refactored and verified.

Confirmed:

- VSM parser/storage route stayed clean: typed CRC/length/seq/resync faults were 0 in all listed runs.
- VSM memory stayed bounded during runs.
- UI projection drops were reported as projection/display degradation, not as parser or CSM CAN loss.
- Debug gateway mode generated raw serial artifacts and VSM final capture artifacts.
- Current hardware/load setup passes at `500fps + 500fps` API load plus existing background traffic.

Not confirmed:

- `1500fps + 1500fps` API load plus existing background traffic does not pass on the current CSM setup. Failure is CSM FIFO/sequence loss, not VSM parser corruption or VSM process crash.

## Code/Architecture Changes

- `LiveTruthRuntime` owns worker-side truth snapshot state keyed by `bus + canId + ext + rtr`.
- `TransportSession` exposes separate `live_truth` and `live_projection` diagnostics.
- `scripts/vsm_debug_gateway.py` records raw serial bytes before forwarding them to VSM over TCP.
- Gateway hot loop now writes/forwards raw bytes only; typed index is generated after normal gateway stop.
- `StorageRuntime` buffers typed stream/index writes while preserving file format and logical offsets.

## Validation Runs

| Run | Route | API load | Result | Key evidence |
| --- | --- | --- | --- | --- |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_110727` | debug gateway | none | PASS | VSM capture 17,388 records, parser faults 0, FIFO 0 |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_112036` | debug gateway | 500+500 fps | PASS | PCAN 15,000/15,000, Kvaser 15,000/15,000, FIFO 0 |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_111828` | direct VSM | 500+500 fps | PASS | PCAN 15,000/15,000, Kvaser 15,000/15,000, FIFO 0 |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_111923` | direct VSM | 750+750 fps | FAIL | FIFO +1, Kvaser missing 1 |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_111719` | direct VSM | 1000+1000 fps | FAIL | FIFO +13, PCAN missing 9, Kvaser missing 19 |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_111602` | direct VSM | 1500+1500 fps | FAIL | FIFO +12, PCAN missing 11, Kvaser missing 33 |
| `artifacts/vsm_user_route_hil/vsm_user_route_20260612_110813` | debug gateway | 1500+1500 fps | FAIL | gateway parser clean, VSM parser clean, FIFO +11, PCAN missing 16, Kvaser missing 61 |

## Interpretation

The VSM route is no longer the observed crash/hang bottleneck in these runs. At the failing loads:

- VSM final `capture.stream` finalized normally.
- VSM typed parser faults were 0.
- Gateway parser faults were 0 when gateway mode was used.
- VSM process remained responsive enough to stop logging.
- Missing PCAN/Kvaser sequences correlated with CSM FIFO increase.

This means the remaining loss at the tested high rates is below VSM, in the CSM receive/queue/USB export path or in the physical/current external traffic load condition.

## Next Gate

Before claiming `1500fps + 1500fps` PASS, run one of:

1. Stop existing external/background CAN traffic and rerun `1500fps + 1500fps`.
2. Improve CSM firmware FIFO/drain behavior, then rerun the same VSM user-route HIL.
3. Define the field RC load rating as the highest passing measured condition: current setup passes `500fps + 500fps` API load plus existing background traffic.

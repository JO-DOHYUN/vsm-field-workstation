# 2026-06-29 VSM Passive-Safe Runtime Profile

## Reason
Field vehicle testing showed that USB plug/unplug, gateway mode, and active transport/control paths can no longer be treated as harmless implementation details. VSM must default to a passive product profile that cannot affect the vehicle CAN/data flow.

## Decision
- Add `RuntimeProfile` as the shared policy owner.
- Default profile is `passive_product`.
- Passive Product serial is read-only, DTR/RTS no-touch, host TX disabled, control disabled, COM-owning gateway disabled.
- `full_instrumented` is bench/lab only.
- Expose profile status through Core materialized view and TransportSession diagnostics.
- Add static boundary scan rule for read/write serial and DTR/RTS hard asserts.

## Expected Gain
- UI, Core, IPC, and SerialDrainRuntime share one execution-policy contract.
- Accidental host/control/gateway calls are rejected before reaching the vehicle-facing runtime.
- Field users can confirm passive policy from transport details and process status artifacts.

## Rollback Rule
Only rollback if Passive Product fails to open a known read-only VSM/CSM typed stream and the replacement still preserves no host TX, no control, no DTR/RTS touch, and no COM-owning gateway in field/product mode.

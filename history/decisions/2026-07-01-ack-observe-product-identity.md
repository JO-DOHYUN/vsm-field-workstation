# 2026-07-01 ACK-Observe Product Identity

## Reason

The previous Passive Product wording treated no-ACK listen-only behavior as the
field product. That made Kvaser/PCAN single-node checks fail by design and mixed
ACK capability with host-originated CAN TX/control. Field behavior showed that
USB hotplug safety and ACK-capable observe mode must be handled as separate
contracts.

## Decision

The product identity is now 2-bus ACK-capable observe-only:

- Before stable host session: pre-session safe receive, no replay, no host
  TX/control.
- After session quarantine: ACK-capable observe mode is allowed.
- Host-originated CAN TX/control/downlink/test TX remains forbidden in Passive
  Product.
- Verified vehicle-impact-free PASS still requires external analyzer/scope/DTC
  evidence; CSM capability fields are claims/references only.

## Expected Gain

The firmware and VSM gates can support practical CAN monitoring behavior without
misclassifying ACK as control TX, while still making USB hotplug safety explicit.

## Rollback Rule

Rollback only if external analyzer/scope evidence proves ACK-observe itself is
unacceptable for the target vehicle. In that case introduce an explicit
listen-only diagnostic/product variant instead of silently changing Passive
Product semantics.

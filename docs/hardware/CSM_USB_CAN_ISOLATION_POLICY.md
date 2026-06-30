# CSM USB/CAN Isolation Policy

The observed vehicle disturbance on USB reconnect must be treated as a hardware
and lifecycle isolation problem until proven otherwise.

## Firmware Boundary

- USB open/close may only affect CDC/uplink/session state.
- USB open/close must not change MCP mode, built-in CAN monitor mode,
  transceiver normal/silent state, TX gate, reset policy, or CAN front-end
  drain.
- If firmware suspects USB power/reset disturbance, it must latch and report
  `USB_POWER_OR_RESET_SUSPECTED`.

## Hardware Boundary

- USB VBUS must not power the vehicle-facing CAN transceiver path unless the
  design explicitly proves this is safe.
- PC ground and vehicle ground coupling must be reviewed for common-mode
  disturbance.
- CAN transceivers must be reset-safe and power-off passive.
- Field SKU should prefer hardware-enforced silent RX over software-only
  listen-only where the selected transceiver supports it.

## Diagnostic Evidence

- CSM typed evidence: USB session events, DTR changes, host absent summary,
  passive readback, TXREQ violation, reset/power suspected event.
- VSM evidence: Core transport lifecycle, capture diagnostics, debug tap
  summary, `.part` recovery report.
- External evidence: analyzer, scope, and vehicle DTC reports.


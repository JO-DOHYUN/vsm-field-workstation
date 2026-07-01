# CSM USB/CAN Isolation Policy

The observed vehicle disturbance on USB reconnect must be treated as a hardware
and lifecycle isolation problem until proven otherwise.

## Firmware Boundary

- USB open/close may only affect CDC/uplink/session state.
- USB open/close must not replay old payload, enable host TX/control, or reset
  the CAN front-end. A controlled session transition may move the front-end from
  pre-session safe receive to ACK-observe after quarantine, and must move it back
  on session close.
- If firmware suspects USB power/reset disturbance, it must latch and report
  `USB_POWER_OR_RESET_SUSPECTED`.

## Hardware Boundary

- USB VBUS must not power the vehicle-facing CAN transceiver path unless the
  design explicitly proves this is safe.
- PC ground and vehicle ground coupling must be reviewed for common-mode
  disturbance.
- CAN transceivers must be reset-safe and power-off passive.
- Field SKU should provide hardware-enforced reset/power-off safe state and a
  controlled normal/observe enable path. ACK-observe is a product behavior after
  session stability; host-originated TX/control remains forbidden.

## Diagnostic Evidence

- CSM typed evidence: USB session events, DTR changes, host absent summary,
  passive readback, TXREQ violation, reset/power suspected event.
- VSM evidence: Core transport lifecycle, capture diagnostics, debug tap
  summary, `.part` recovery report.
- External evidence: analyzer, scope, and vehicle DTC reports.

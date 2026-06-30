# CSM Passive Front-End Acceptance

Software counters can reject unsafe states. They cannot alone prove that hotplug
or reset never disturbs CANH/CANL. Final PASS requires external physical
evidence.

## Required Artifacts

- Reference CAN analyzer log showing error frame delta 0.
- Oscilloscope or equivalent capture showing no dominant pulse or bus-level
  disturbance during USB plug/unplug, VSM connect/disconnect, debug tap on/off,
  MCU reset, bootloader, and upload.
- Vehicle DTC/adcu latch report showing delta 0.
- Schematic/BOM/netlist review result for both CAN lanes.
- Bench verification ID and external analyzer artifact ID referenced by CSM
  capability.

## Bench Test Matrix

- CAN connector only, USB absent.
- USB plug/unplug 100 cycles.
- VSM connect/disconnect 100 cycles.
- Debug Tap on/off 50 cycles.
- MCU reset 100 cycles.
- Bootloader/upload 20 cycles.
- PC sleep/wake or USB hub power-cycle 20 cycles.

## PASS Conditions

- Reference analyzer error frame increase: 0.
- Vehicle DTC/adcu latch increase: 0.
- CSM `CAN_TX_RAW` count in passive product: 0.
- MCP passive mode violation: 0.
- TXREQ violation: 0.
- USB power/reset suspected event: 0 or explained by controlled reset test only.
- Host absent payload replay after reconnect: 0.
- `.part` captures are classified as incomplete and never as normal PASS logs.

## VSM Display Rule

Until all required artifacts are present and verified, VSM must display
`Software Passive Prototype / Hardware Unverified`, not `verified_passive`.


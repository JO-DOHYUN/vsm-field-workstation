# CSM Passive Front-End Requirements

The production direction is a two-bus ACK-capable observe-only CAN probe.
Software listen-only is a pre-session or diagnostic state, not the final product
observe state.

## Product Profile

- Product bus count: exactly two required RX buses.
- `bus=0`: MCP2515/TJA1050 path, Classic CAN 500 kbps. Start pre-session safe,
  enter ACK-capable observe after stable host session.
- `bus=1`: Mid Carrier J4/U2 CAN path, Classic CAN 500 kbps. Start pre-session
  monitor/safe receive, enter ACK-capable observe after stable host session.
- Host downlink, host TX, control TX, test TX, and USB reconnect reset are not
  product features.
- Full Instrumented builds are bench/lab artifacts only.

## Firmware Requirements

- Host absent: drain CAN RX and discard frame payload; do not stage typed
  records or segment payloads.
- Host session open: increment `host_session_epoch` and `transport_epoch`, clear
  uplink/session payload state, emit capability, USB session event, host-absent
  summary, then board health.
- USB attach quarantine must not replay old CAN payload or enable host
  TX/control. It may switch from pre-session safe receive to ACK-observe only
  after quarantine completes.
- Readback guard must latch unexpected MCP mode or TXREQ violations.
- Capability hardware fields are claims/references and must not be interpreted
  as verified proof without external artifacts.

## Hardware Requirements

- Both CAN lanes need transceiver silent or standby default during power-off,
  reset, bootloader, and firmware boot.
- Passive field SKU must make host-originated drive/control TX unavailable or
  inaccessible in the field. A controlled normal/observe enable path is allowed
  only for ACK-capable observe after session stability.
- TXD must default recessive with fail-safe biasing and/or TX gate.
- No default CAN termination unless explicitly required by the vehicle harness.
- USB VBUS and PC ground must not back-power or disturb the vehicle CAN side.
- ESD/TVS/common-mode choke/shield/GND policy must be fixed in schematic.

## Schematic/BOM/Netlist Review Items

- Transceiver part number and vendor datasheet revision.
- Silent/STB/EN pin default net, pull value, reset state, and boot state.
- TXD net source, pull state, series resistance, and optional gate.
- RXD net isolation/protection path.
- CANH/CANL protection, common-mode choke, split termination policy.
- USB VBUS back-power prevention or galvanic isolation policy.
- Vehicle ground to PC ground connection and common-mode current path.
- Field SKU BOM marker proving host TX/control drive path is not populated or
  is physically inaccessible, and that observe-enable defaults safe on reset.

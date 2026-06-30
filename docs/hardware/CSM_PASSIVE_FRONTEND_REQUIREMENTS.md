# CSM Passive Front-End Requirements

The production direction is a two-bus passive CAN probe. Software listen-only is
required but not sufficient for vehicle-impact-free acceptance.

## Product Profile

- Product bus count: exactly two required RX buses.
- `bus=0`: MCP2515/TJA1050 path, Classic CAN 500 kbps, MCP listen-only.
- `bus=1`: Mid Carrier J4/U2 CAN path, Classic CAN 500 kbps, silent monitor RX.
- Host downlink, host TX, control TX, test TX, and USB reconnect reset are not
  product features.
- Full Instrumented builds are bench/lab artifacts only.

## Firmware Requirements

- Host absent: drain CAN RX and discard frame payload; do not stage typed
  records or segment payloads.
- Host session open: increment `host_session_epoch` and `transport_epoch`, clear
  uplink/session payload state, emit capability, USB session event, host-absent
  summary, then board health.
- USB attach quarantine must not reset MCP, CAN controller, transceiver mode, TX
  gate, or boot policy.
- Passive readback guard must latch MCP listen-only or TXREQ violations.
- Capability hardware fields are claims/references and must not be interpreted
  as verified proof without external artifacts.

## Hardware Requirements

- Both CAN lanes need transceiver silent or standby default during power-off,
  reset, bootloader, and firmware boot.
- Passive field SKU must make normal/drive-enable path unavailable or
  inaccessible in the field.
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
- Field SKU BOM marker proving active/normal enable path is not populated.


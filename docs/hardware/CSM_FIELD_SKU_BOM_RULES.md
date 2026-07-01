# CSM Field SKU BOM Rules

The passive field SKU must be impossible to accidentally use as an active CAN
device.

## Mandatory BOM Rules

- Two CAN RX lanes are mandatory for the current product.
- Host-originated active transmit/control path must be not populated or
  physically inaccessible on the field SKU. A controlled normal/observe enable
  path is allowed only for ACK-capable monitoring after session stability.
- TXD gate or fail-safe recessive default is mandatory.
- Silent/STB/EN default must keep the transceiver non-transmitting through
  power-off, reset, bootloader, and firmware boot.
- USB/PC side must not back-power the vehicle CAN side.
- Termination must be not populated by default unless the vehicle harness
  explicitly requires this unit to be an endpoint.

## BOM Review Fields

- `field_sku_id`
- transceiver part number per bus
- normal-enable population state per bus
- silent default resistor values per bus
- TXD fail-safe resistor/gate part per bus
- CANH/CANL protection parts per bus
- USB isolation/back-power prevention part
- bench verification ID
- external analyzer artifact ID

These fields may be referenced by CSM capability, but the capability is still a
runtime claim. VSM must validate matching external artifacts before PASS.

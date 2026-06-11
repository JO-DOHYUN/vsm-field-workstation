# TYPED_STREAM_PROTOCOL_V1_KO

이 문서는 VMS Qt와 CSM firmware가 공유하는 typed stream v1 계약이다.
기계가 참조하는 최소 계약은 [[../../shared/protocol/typed_stream_v1]] 및
[[../../shared/protocol/typed_record_ids.h]]에 둔다.

## Transport Frame

```text
SOF:         0xA5 0x5A
version:     u8, currently 1
record_type: u8
flags:       u8
seq:         u16_le
payload_len: u16_le
payload:     record-specific little-endian bytes
crc16:       u16_le, CRC-CCITT over version..payload, excluding SOF and crc field
```

Receiver recovery:
- scan for SOF
- validate `payload_len`
- validate CRC
- record byte drops, CRC failures, length failures, version warnings, and sequence gaps
- dispatch unknown record types as diagnostic evidence, not silent success

## Record IDs

```text
1  CAN_RX_RAW
2  CAN_TX_RAW
3  ENC_EDGE_RAW
4  ENC_DERIVED
5  ADC_SAMPLE
6  CONTROL_ACK
7  BOARD_EVENT
8  BOARD_HEALTH
9  CAPABILITY
10 HOST_CAN_TX_REQUEST
11 HOST_HEARTBEAT
12 HOST_CONTROL_SESSION
```

`HOST_CAN_TX_REQUEST`, `HOST_HEARTBEAT`, `HOST_CONTROL_SESSION` are host-to-board
downlink commands. Other listed records are board-to-host evidence records.

## CAN_RX_RAW / CAN_TX_RAW Payload

Size: 30 bytes.

```text
0..7    mono_us u64
8..11   can_id_flags u32: bit 0..28 id, bit 29 extended, bit 30 RTR
12      dlc_flags u8: low nibble DLC
13      bus u8
14..21  data[8]
22..25  total u32: RX total or TX success total
26..29  dropped_or_failed u32
```

`CAN_TX_RAW` is emitted only after hardware CAN write succeeds. This is the only
actual CAN TX success evidence.

Current bus roles must be resolved from `CAPABILITY`, model rules, observed CAN
IDs, or operator override. New VMS code must not hard-code `bus=0`/`bus=1` as
System/Drive.

## ADC_SAMPLE Payload

Size: 44 bytes.

```text
0..7    mono_us u64
8..11   sample_total u32
12..15  dropped_total u32
16      source_id u8
17      channel_count u8, max 8
18      resolution_bits u8
19      flags u8: bit0 raw valid, bit1 direct MCU ADC, bit6 saturated, bit7 read error
20..27  channel_id[8]
28..43  raw_u16[8]
```

Scaling is a VMS/profile responsibility. Raw ADC evidence remains valid even if
calibration changes. ADC/voltage evidence must not be projected as fake CAN.

## HOST_CAN_TX_REQUEST Payload

Host-to-board downlink payload size: 19 bytes.

```text
0..3    command_id u32
4       bus u8
5       frame_flags u8: bit0 extended, bit1 RTR
6..9    can_id u32
10      dlc u8, 0..8
11..18  data[8]
```

The matching board decision is `CONTROL_ACK`. Actual CAN success still requires
a matching `CAN_TX_RAW` audit.

## HOST_HEARTBEAT Payload

Host-to-board downlink payload size: 12 bytes.

```text
0..3    command_id u32
4..7    host_mono_ms u32
8..9    flags u16
10..11  reserved u16
```

VMS sends heartbeat before and during control. If heartbeat is stale, CSM rejects
new host TX with reason `HOST_TIMEOUT`.

## HOST_CONTROL_SESSION Payload

Host-to-board downlink payload size: 24 bytes.

```text
0..3    command_id u32
4       action u8: 0 disarm, 1 arm, 2 renew lease, 3 install neutral profile reserved
5       requested_bus u8: physical bus id or 0xFF for any configured control backend
6..7    flags u16
8..9    lease_ms u16: 0 board default, current VMS uses 2000 ms
10..11  reserved u16
12..15  policy_hash u32
16..19  model_pack_hash u32
20..23  aux u32
```

Heartbeat resume alone never auto-arms. VMS must explicitly send arm/session
after reconnect.

## CONTROL_ACK Payload

Size: 28 bytes.

```text
0..7    mono_us u64
8..11   command_id u32
12      status u8
13      reason u8
14      target_bus u8
15      dlc_flags u8
16..19  target_can_id_flags u32
20..23  counter u32
24..27  rejected_total u32
```

Status values:

```text
0 REJECTED
1 ACCEPTED
2 ACCEPTED_WRITTEN       reserved/optional
3 ACCEPTED_RATE_LIMITED  reserved/optional
```

Reason values:

```text
0  OK
1  BAD_LENGTH
2  BAD_BUS
3  UNSUPPORTED_FRAME
4  DLC_OUT_OF_RANGE
5  ID_NOT_ALLOWED
6  CAN_NOT_READY
7  CAN_WRITE_FAILED
8  BAD_PROTOCOL
9  SAFETY_NOT_ARMED
10 HOST_TIMEOUT
11 CONTROL_LEASE_EXPIRED
12 SAFETY_LOCKOUT
13 ESTOP_ASSERTED
14 FIELD_POWER_LOST
15 ENCODER_FAULT
16 QUEUE_FULL
17 TX_BUSY
18 BUS_OFF
19 ERROR_PASSIVE
20 ROLE_UNRESOLVED
21 POLICY_HASH_MISMATCH
22 NEUTRAL_PROFILE_MISSING
23 RATE_LIMITED
24 UNSUPPORTED_COMMAND
```

`CONTROL_ACK` is board decision evidence only. It is never final CAN TX success.
Actual CAN TX success requires matching `CAN_TX_RAW`.

## BOARD_HEALTH And CAPABILITY

Minimum VMS requirements:
- `CAPABILITY` must be received before board alive is true.
- `BOARD_HEALTH` must be fresh before control capable is true.
- CSM safety states are `0 Boot`, `1 MonitorOnly`, `2 Ready`, `3 Armed`,
  `4 ControlActive`, `5 HostTimeout`, `6 FaultLockout`, `7 Estop`.
- Control-capable UI may remain enabled only for states `1..4`, with fault flags
  clear and protocol/profile compatible.

## Replay Rule

Typed replay preserves original record order and `mono_us`.
Legacy `.bin` replay preserves legacy 20-byte CRC8/DLC/`t_us` wrap semantics and
is not a live production stream.

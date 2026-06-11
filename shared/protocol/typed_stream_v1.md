# typed_stream_v1

Canonical CSM/VMS typed binary contract.
Human-readable Korean architecture notes live in `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`.

## Frame

```text
sof0        u8  0xA5
sof1        u8  0x5A
version     u8  1
record_type u8
flags       u8
seq         u16_le
payload_len u16_le
payload     byte[payload_len]
crc16       u16_le
```

CRC-16/CCITT covers `version` through the final payload byte. SOF and CRC field are excluded.

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

## CAN_RX_RAW / CAN_TX_RAW

Payload length: 30 bytes.

```text
0   mono_us             u64_le
8   can_id_flags        u32_le
12  dlc_flags           u8
13  bus                 u8
14  data                u8[8]
22  total               u32_le
26  dropped_or_failed   u32_le
```

`CAN_TX_RAW` is emitted only after successful hardware CAN write.

## ADC_SAMPLE

Payload length: 44 bytes.

```text
0   mono_us          u64_le
8   sample_total     u32_le
12  dropped_total    u32_le
16  source_id        u8
17  channel_count    u8
18  resolution_bits  u8
19  flags            u8
20  channel_id       u8[8]
28  raw_u16          u16_le[8]
```

## HOST_CAN_TX_REQUEST

Host-to-board downlink payload length: 19 bytes.

```text
0   command_id   u32_le
4   bus          u8
5   flags        u8  bit0 extended, bit1 RTR
6   can_id       u32_le
10  dlc          u8
11  data         u8[8]
```

The matching board acceptance is `CONTROL_ACK`. The matching actual CAN success is `CAN_TX_RAW`.

## HOST_HEARTBEAT

Host-to-board downlink payload length: 12 bytes.

```text
0   command_id    u32_le
4   host_mono_ms  u32_le
8   flags         u16_le
10  reserved      u16_le
```

VMS sends heartbeat before and during control. CSM rejects control TX with `HOST_TIMEOUT` when heartbeat is stale.

## HOST_CONTROL_SESSION

Host-to-board downlink payload length: 24 bytes.

```text
0   command_id       u32_le
4   action           u8      0 disarm, 1 arm, 2 renew lease, 3 install neutral profile
5   requested_bus    u8      physical bus id or 0xFF for any configured control backend
6   flags            u16_le
8   lease_ms         u16_le  0 means board default, current VMS uses 2000 ms
10  reserved         u16_le
12  policy_hash      u32_le
16  model_pack_hash  u32_le
20  aux              u32_le
```

Heartbeat resume alone never auto-arms. VMS must send an explicit arm/session command after reconnect.

## CONTROL_ACK

Payload length: 28 bytes.

```text
mono_us             u64_le
command_id          u32_le
status              u8
reason              u8
target_bus          u8
dlc_flags           u8
target_can_id_flags u32_le
counter             u32_le
rejected_total      u32_le
```

`CONTROL_ACK` does not mean actual CAN TX success.

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

CSM safety state values:

```text
0 Boot
1 MonitorOnly
2 Ready
3 Armed
4 ControlActive
5 HostTimeout
6 FaultLockout
7 Estop
```

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
16 CAN_RX_SEGMENT
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

## CAN_RX_SEGMENT Payload

Size: `32 + frame_count * 30` bytes.

`CAN_RX_SEGMENT` is lossless packing for high-load CAN RX. It must not be treated
as compression, sampling, or summary data. VMS must expand every entry into the
same truth path used for `CAN_RX_RAW`: typed capture/replay, raw ledger,
analysis runtime, timing, value, alarm, DLC, and export.

Header:

```text
0..7    segment_seq64 u64
8..15   first_capture_seq64 u64
16..17  frame_count u16
18      entry_size u8: currently 30
19      flags u8: bit0 capture_seq64 valid
20..23  dropped_before_segment u32
24..27  fifo_before_segment u32
28..31  reserved u32
```

Frame entry:

```text
0..7    capture_seq64 u64
8..15   mono_us u64
16..19  can_id_flags u32: bit 0..28 id, bit 29 extended, bit 30 RTR
20      dlc_flags u8: low nibble DLC
21      bus u8
22..29  data[8]
```

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

`BOARD_HEALTH` 기본 payload는 52 bytes이며 기존 필드는 계속 유지한다. CSM CDC
backpressure 대응 이후의 확장 health payload는 192 bytes 이상일 수 있고, VMS는
아래 offset이 존재하면 USB uplink/segment 손실 진단으로 별도 표시한다.

```text
160..163  serial_enqueue_fail_total u32_le
164..167  serial_ring_clear_total u32_le
168..171  serial_ring_cleared_bytes_total u32_le
172..175  serial_backpressure_total u32_le
176..179  serial_tx_high_water_bytes u32_le
180..183  shared_can_queue_high_water u32_le
184..187  mcp_drain_budget_hit_total u32_le
188..191  can_segment_enqueue_fail_total u32_le
```

`serial_ring_clear_total`, `serial_ring_cleared_bytes_total`,
`serial_enqueue_fail_total`, `can_segment_enqueue_fail_total`는 capture truth loss
또는 truth loss 위험 진단이다. VMS는 이를 display sampling/projection drop과 섞지
말고 transport/CSM uplink 진단으로 노출한다.

USB lifecycle board events used by passive diagnostics:

```text
27 USB_CDC_SESSION_OPEN
28 USB_CDC_SESSION_CLOSE  reported on the next open; detail is previous
                          session duration ms saturated at 0xFFFF
29 USB_CDC_DTR_CHANGE
30 USB_HOST_ABSENT_CAN_DISCARD_SUMMARY
31 MCP_PASSIVE_MODE_READBACK
32 MCP_PASSIVE_MODE_VIOLATION
33 MCP_TXREQ_VIOLATION
34 TRANSCEIVER_SAFE_STATE_CHANGED
35 USB_POWER_OR_RESET_SUSPECTED
36 CAN_FRONTEND_PRESESSION_HOLD
37 CAN_FRONTEND_SESSION_READY
38 CAN_FRONTEND_SESSION_INIT_FAILED
```

`BOARD_HEALTH v7` extends v6 with passive lifecycle counters:
host-absent discard per bus, host-absent FIFO/MCP errors, host-absent duration,
MCP passive readback count, readback violation count, TXREQ violation count, and
USB CDC DTR change count. In the product profile, readback checks the configured
safe mode: no initialized CAN front-end before session-ready, ACK-observe normal mode
after session quarantine and event 37. VSM must treat unexpected mode/TXREQ violation as
product-blocking evidence, not as display sampling loss.

## CAPABILITY v6 Passive Evidence Claims

Newer Passive Product CSM firmware may emit a 272-byte `CAPABILITY` payload.
Offsets `224..268` carry passive hardware evidence claims and host-session
epoch counters. These fields are claims/references, not proof. VSM may use them
for mismatch detection and operator guidance, but `verified_passive` requires
external analyzer/scope/DTC artifact verification.

```text
224 passive_hardware_evidence_schema  u8
225 hardware_silent_strapped_bus0     u8
226 hardware_silent_strapped_bus1     u8
227 galvanic_isolated_bus0            u8
228 galvanic_isolated_bus1            u8
229 power_off_passive_bus0            u8
230 power_off_passive_bus1            u8
231 reset_safe_bus0                   u8
232 reset_safe_bus1                   u8
233 txd_gated_bus0                    u8
234 txd_gated_bus1                    u8
235 normal_enable_path_populated_bus0 u8
236 normal_enable_path_populated_bus1 u8
240 field_sku_id                      u32_le
244 external_analyzer_artifact_id     u32_le
248 hotplug_pass_count                u32_le
252 host_session_epoch                u32_le
256 transport_epoch                   u32_le
260 usb_attach_quarantine_total       u32_le
264 host_absent_gap_total             u32_le
268 pre_session_payload_replay_total  u32_le
```

The product is two-bus observe-only. A one-bus capability is a blocking mismatch, not
an accepted passive product. `USB_ATTACH_QUARANTINE` is CDC/uplink/session
cleanup only; CAN front-end passive drain must continue.

Typed capture session sidecars:

```text
capture.stream              accepted typed frame bytes
capture.index               sparse record index
events.jsonl                capture/session events
session.meta.json           session metadata
capture.diagnostics.json    live parser/storage counters captured at finalize
```

`capture.diagnostics.json`의 parser counters는 최종 `capture.stream`을 재파싱해 얻는
fault counter와 별개다. 저장 당시 live parser가 본 CRC/length/drop/seq 상태를
보존하기 위한 sidecar다.

## Replay Rule

Typed replay preserves original record order and `mono_us`.
Legacy `.bin` replay preserves legacy 20-byte CRC8/DLC/`t_us` wrap semantics and
is not a live production stream.

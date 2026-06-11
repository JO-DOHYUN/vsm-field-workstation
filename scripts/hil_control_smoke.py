#!/usr/bin/env python3
"""
VMS/CSM typed control HIL smoke.

This script is intentionally evidence-first:
- Serial write success is not CAN TX success.
- CONTROL_ACK is board decision evidence only.
- CAN_TX_RAW is the only actual board CAN TX success evidence.
"""

from __future__ import annotations

import argparse
import collections
import json
import struct
import sys
import time
from collections import deque
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - host environment guard
    print(f"pyserial is required: {exc}", file=sys.stderr)
    sys.exit(3)


SOF = b"\xA5\x5A"
PROTOCOL_VERSION = 1
RECORD_CAN_RX_RAW = 1
RECORD_CAN_TX_RAW = 2
RECORD_CONTROL_ACK = 6
RECORD_BOARD_EVENT = 7
RECORD_BOARD_HEALTH = 8
RECORD_CAPABILITY = 9
RECORD_HOST_CAN_TX_REQUEST = 10
RECORD_HOST_HEARTBEAT = 11
RECORD_HOST_CONTROL_SESSION = 12

HOST_CONTROL_DISARM = 0
HOST_CONTROL_ARM = 1
HOST_CONTROL_RENEW_LEASE = 2

CONTROL_IDS = (0x503, 0x510, 0x512, 0x511, 0x513)
ACK_STATUS = {
    0: "REJECTED",
    1: "ACCEPTED",
    2: "ACCEPTED_WRITTEN",
    3: "ACCEPTED_RATE_LIMITED",
}
ACK_REASON = {
    0: "OK",
    1: "BAD_LENGTH",
    2: "BAD_BUS",
    3: "UNSUPPORTED_FRAME",
    4: "DLC_OUT_OF_RANGE",
    5: "ID_NOT_ALLOWED",
    6: "CAN_NOT_READY",
    7: "CAN_WRITE_FAILED",
    8: "BAD_PROTOCOL",
    9: "SAFETY_NOT_ARMED",
    10: "HOST_TIMEOUT",
    11: "CONTROL_LEASE_EXPIRED",
    12: "SAFETY_LOCKOUT",
    13: "ESTOP_ASSERTED",
    14: "FIELD_POWER_LOST",
    15: "ENCODER_FAULT",
    16: "QUEUE_FULL",
    17: "TX_BUSY",
    18: "BUS_OFF",
    19: "ERROR_PASSIVE",
    20: "ROLE_UNRESOLVED",
    21: "POLICY_HASH_MISMATCH",
    22: "NEUTRAL_PROFILE_MISSING",
    23: "RATE_LIMITED",
    24: "UNSUPPORTED_COMMAND",
}


def u32_counter_delta(current: int, previous: int) -> int:
    if current >= previous:
        return current - previous
    return (1 << 32) - previous + current


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_typed_frame(record_type: int, seq: int, payload: bytes) -> bytes:
    header_payload = struct.pack(
        "<BBBHH",
        PROTOCOL_VERSION,
        record_type,
        0,
        seq & 0xFFFF,
        len(payload),
    ) + payload
    return SOF + header_payload + struct.pack("<H", crc16_ccitt(header_payload))


def build_host_can_tx_request(command_id: int, bus: int, can_id: int, data: bytes) -> bytes:
    if len(data) != 8:
        raise ValueError("control smoke currently sends DLC 8 frames only")
    payload = struct.pack("<IBBI", command_id, bus, 0, can_id) + bytes([8]) + data
    return build_typed_frame(RECORD_HOST_CAN_TX_REQUEST, command_id, payload)


def build_heartbeat(command_id: int) -> bytes:
    host_mono_ms = int(time.monotonic() * 1000.0) & 0xFFFFFFFF
    payload = struct.pack("<IIHH", command_id, host_mono_ms, 0, 0)
    return build_typed_frame(RECORD_HOST_HEARTBEAT, command_id, payload)


def build_control_session(command_id: int, action: int, bus: int, lease_ms: int) -> bytes:
    payload = struct.pack(
        "<IBBHHHIII",
        command_id,
        action & 0xFF,
        bus & 0xFF,
        0,
        lease_ms & 0xFFFF,
        0,
        0,
        0,
        0,
    )
    return build_typed_frame(RECORD_HOST_CONTROL_SESSION, command_id, payload)


def set_i16_le(data: bytearray, offset: int, value: int) -> None:
    value = max(-32768, min(32767, value))
    packed = struct.pack("<h", value)
    data[offset] = packed[0]
    data[offset + 1] = packed[1]


def adcu_payload(driving_mode: int, alive_counter: int) -> bytes:
    adcu_manual = bytes([0x80, 0, 0, 0, 0, 0x20, 0, 0])
    data = bytearray(adcu_manual)
    data[5] = (driving_mode & 0x07) << 5
    data[7] = (alive_counter & 0x0F) << 4
    return bytes(data)


def vcu_payload(signed_command: int, steering_deg: float, motor_mode: int) -> bytes:
    data = bytearray([((motor_mode & 0x03) << 6), 0, 0, 0, 0, 0, 0, 0])
    set_i16_le(data, 1, max(-10000, min(10000, signed_command)))
    set_i16_le(data, 3, round(max(-90.0, min(90.0, steering_deg)) * 10.0))
    return bytes(data)


def control_burst_payloads(pattern: str, burst_index: int) -> list[bytes]:
    if pattern == "ramp":
        signed_command = min(3000, burst_index * 250)
        steering_deg = min(30.0, burst_index * 1.0)
    else:
        signed_command = 0
        steering_deg = 0.0

    adcu_manual = adcu_payload(driving_mode=1, alive_counter=burst_index)
    motor_frame = vcu_payload(signed_command, steering_deg, motor_mode=1)
    return [adcu_manual, motor_frame, motor_frame, motor_frame, motor_frame]


def control_burst(base_command_id: int, bus: int, pattern: str, burst_index: int) -> list[tuple[int, bytes, bytes]]:
    payloads = control_burst_payloads(pattern, burst_index)
    return [
        (can_id, payloads[index], build_host_can_tx_request(base_command_id + index, bus, can_id, payloads[index]))
        for index, can_id in enumerate(CONTROL_IDS)
    ]


class TypedStreamProbe:
    def __init__(self, expected_tx_by_bus_id: dict[tuple[int, int], deque[bytes]] | None = None) -> None:
        self.buffer = bytearray()
        self.expected_tx_by_bus_id = expected_tx_by_bus_id if expected_tx_by_bus_id is not None else {}
        self.frames = 0
        self.crc_failures = 0
        self.length_failures = 0
        self.bytes_dropped = 0
        self.initial_resync_dropped = 0
        self.post_sync_dropped = 0
        self.type_counts: collections.Counter[int] = collections.Counter()
        self.ack_status: collections.Counter[int] = collections.Counter()
        self.ack_reason: collections.Counter[int] = collections.Counter()
        self.acks: list[tuple[int, int, int, int, int, int]] = []
        self.control_request_acks: list[tuple[int, int, int, int, int, int]] = []
        self.events: list[tuple[int, int, int]] = []
        self.tx_audits: list[tuple[int, int, int, bytes]] = []
        self.tx_payload_matches = 0
        self.tx_payload_mismatches = 0
        self.tx_payload_unexpected = 0
        self.tx_payload_mismatch_examples: list[str] = []
        self.rx_by_bus_id: collections.Counter[tuple[int, int]] = collections.Counter()
        self.rx_times_by_bus_id: dict[tuple[int, int], list[int]] = collections.defaultdict(list)
        self.rx_mono_us: list[int] = []
        self.rx_control_candidates = 0
        self.health_snapshots: list[tuple[int, int, int, int, int, int]] = []
        self.last_health: tuple[int, int, int, int, int, int] | None = None
        self.last_capability: tuple[int, int, int, int, int] | None = None

    def feed(self, chunk: bytes) -> None:
        self.buffer.extend(chunk)
        while len(self.buffer) >= 11:
            sof = self.buffer.find(SOF)
            if sof < 0:
                drop = max(0, len(self.buffer) - 1)
                del self.buffer[:drop]
                self.bytes_dropped += drop
                if self.frames == 0:
                    self.initial_resync_dropped += drop
                else:
                    self.post_sync_dropped += drop
                return
            if sof > 0:
                del self.buffer[:sof]
                self.bytes_dropped += sof
                if self.frames == 0:
                    self.initial_resync_dropped += sof
                else:
                    self.post_sync_dropped += sof
            if len(self.buffer) < 11:
                return

            payload_len = self.buffer[7] | (self.buffer[8] << 8)
            if payload_len > 4096:
                del self.buffer[0]
                self.length_failures += 1
                self.bytes_dropped += 1
                continue

            frame_len = 11 + payload_len
            if len(self.buffer) < frame_len:
                return

            frame = bytes(self.buffer[:frame_len])
            expected_crc = frame[9 + payload_len] | (frame[10 + payload_len] << 8)
            actual_crc = crc16_ccitt(frame[2 : 9 + payload_len])
            if expected_crc != actual_crc:
                del self.buffer[0]
                self.crc_failures += 1
                self.bytes_dropped += 1
                continue

            record_type = frame[3]
            payload = frame[9 : 9 + payload_len]
            self.frames += 1
            self.type_counts[record_type] += 1
            self._decode_record(record_type, payload)
            del self.buffer[:frame_len]

    def _decode_record(self, record_type: int, payload: bytes) -> None:
        if record_type == RECORD_CONTROL_ACK and len(payload) >= 28:
            command_id = struct.unpack_from("<I", payload, 8)[0]
            status = payload[12]
            reason = payload[13]
            bus = payload[14]
            dlc_flags = payload[15]
            can_id = struct.unpack_from("<I", payload, 16)[0] & 0x1FFFFFFF
            self.ack_status[status] += 1
            self.ack_reason[reason] += 1
            ack = (command_id, status, reason, bus, dlc_flags, can_id)
            self.acks.append(ack)
            if can_id in CONTROL_IDS:
                self.control_request_acks.append(ack)
        elif record_type == RECORD_BOARD_EVENT and len(payload) >= 16:
            code, detail, counter = struct.unpack_from("<HHI", payload, 8)
            if code or detail:
                self.events.append((code, detail, counter))
        elif record_type in (RECORD_CAN_RX_RAW, RECORD_CAN_TX_RAW) and len(payload) >= 30:
            mono_us = struct.unpack_from("<Q", payload, 0)[0]
            can_id = struct.unpack_from("<I", payload, 8)[0] & 0x1FFFFFFF
            dlc = payload[12] & 0x0F
            bus = payload[13]
            data = bytes(payload[14 : 14 + min(dlc, 8)])
            if record_type == RECORD_CAN_RX_RAW:
                key = (bus, can_id)
                self.rx_by_bus_id[key] += 1
                self.rx_mono_us.append(mono_us)
                if len(self.rx_times_by_bus_id[key]) < 10000:
                    self.rx_times_by_bus_id[key].append(mono_us)
            if can_id in CONTROL_IDS:
                if record_type == RECORD_CAN_TX_RAW:
                    self.tx_audits.append((bus, can_id, mono_us, data))
                    expected_queue = self.expected_tx_by_bus_id.get((bus, can_id))
                    if expected_queue is not None and expected_queue:
                        expected = expected_queue.popleft()
                        if data == expected:
                            self.tx_payload_matches += 1
                        else:
                            self.tx_payload_mismatches += 1
                            if len(self.tx_payload_mismatch_examples) < 8:
                                self.tx_payload_mismatch_examples.append(
                                    f"bus={bus} can=0x{can_id:X} expected={expected.hex(' ').upper()} actual={data.hex(' ').upper()}"
                                )
                    elif self.expected_tx_by_bus_id:
                        self.tx_payload_unexpected += 1
                else:
                    self.rx_control_candidates += 1
        elif record_type == RECORD_BOARD_HEALTH and len(payload) >= 52:
            mono_us = struct.unpack_from("<Q", payload, 0)[0]
            can_rx_total = struct.unpack_from("<I", payload, 8)[0]
            queue_depth = struct.unpack_from("<I", payload, 24)[0]
            safety_state = payload[44]
            flags = payload[47]
            fault_flags = struct.unpack_from("<I", payload, 48)[0]
            snapshot = (mono_us, can_rx_total, queue_depth, safety_state, flags, fault_flags)
            self.health_snapshots.append(snapshot)
            self.last_health = snapshot
        elif record_type == RECORD_CAPABILITY and len(payload) >= 36:
            protocol = payload[8]
            profile_major = payload[9]
            profile_minor = payload[10]
            supports_tx_audit = payload[25]
            supports_health = payload[29]
            self.last_capability = (protocol, profile_major, profile_minor, supports_tx_audit, supports_health)

    def health_rx_parity(self) -> dict[str, int] | None:
        if len(self.health_snapshots) < 2:
            return None
        first = self.health_snapshots[0]
        last = self.health_snapshots[-1]
        first_mono, first_total = first[0], first[1]
        last_mono, last_total = last[0], last[1]
        board_delta = u32_counter_delta(last_total, first_total)
        stream_delta = sum(1 for mono_us in self.rx_mono_us if first_mono < mono_us <= last_mono)
        return {
            "first_health_mono_us": first_mono,
            "last_health_mono_us": last_mono,
            "first_board_can_rx_total": first_total,
            "last_board_can_rx_total": last_total,
            "board_delta": board_delta,
            "stream_delta": stream_delta,
            "missing": board_delta - stream_delta,
            "health_samples": len(self.health_snapshots),
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="VMS/CSM typed control smoke")
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--bus", type=int, default=1)
    parser.add_argument("--bursts", type=int, default=10)
    parser.add_argument("--period-ms", type=float, default=20.0)
    parser.add_argument("--intra-frame-gap-ms", type=float, default=1.0)
    parser.add_argument("--duration-s", type=float, default=6.0)
    parser.add_argument("--settle-ms", type=int, default=2500)
    parser.add_argument("--lease-ms", type=int, default=2000)
    parser.add_argument("--lease-renew-ms", type=int, default=250)
    parser.add_argument("--read-only", action="store_true")
    parser.add_argument("--motion-pattern", choices=("neutral", "ramp"), default="neutral")
    parser.add_argument("--no-arm-first", action="store_true")
    parser.add_argument("--expect-tx", action="store_true", help="return failure when no CAN_TX_RAW audit is seen")
    parser.add_argument("--report-periods", action="store_true", help="print CAN_TX_RAW per-ID interval statistics")
    parser.add_argument("--report-rx", action="store_true", help="print top CAN_RX_RAW IDs and interval statistics")
    parser.add_argument("--top-rx", type=int, default=16)
    parser.add_argument("--min-rx-total", type=int, default=0)
    parser.add_argument(
        "--min-rx-bus",
        action="append",
        default=[],
        metavar="BUS:COUNT",
        help="require at least COUNT CAN_RX_RAW records on BUS, can be repeated",
    )
    parser.add_argument("--check-rx-health-parity", action="store_true")
    parser.add_argument("--max-rx-health-missing", type=int, default=3)
    parser.add_argument("--max-queue-depth", type=int, default=-1)
    parser.add_argument("--max-fault-flags", type=lambda value: int(value, 0), default=-1)
    parser.add_argument("--json-out")
    return parser.parse_args()


def parse_min_rx_bus(values: list[str]) -> dict[int, int]:
    parsed: dict[int, int] = {}
    for value in values:
        try:
            bus_text, count_text = value.split(":", 1)
            parsed[int(bus_text, 0)] = int(count_text, 0)
        except ValueError as exc:
            raise SystemExit(f"invalid --min-rx-bus {value!r}; expected BUS:COUNT") from exc
    return parsed


def format_period_stats(tx_audits: list[tuple[int, int, int, bytes]]) -> list[str]:
    grouped: dict[tuple[int, int], list[int]] = collections.defaultdict(list)
    for bus, can_id, mono_us, _data in tx_audits:
        grouped[(bus, can_id)].append(mono_us)

    lines: list[str] = []
    for (bus, can_id), values in sorted(grouped.items()):
        if len(values) < 2:
            lines.append(f"period bus={bus} can=0x{can_id:X} count={len(values)} intervals=0")
            continue
        intervals_ms = [(values[i] - values[i - 1]) / 1000.0 for i in range(1, len(values))]
        avg = sum(intervals_ms) / len(intervals_ms)
        sorted_intervals = sorted(intervals_ms)
        p95 = sorted_intervals[min(len(sorted_intervals) - 1, int(round((len(sorted_intervals) - 1) * 0.95)))]
        first = ",".join(f"{x:.2f}" for x in intervals_ms[:12])
        lines.append(
            f"period bus={bus} can=0x{can_id:X} count={len(values)} "
            f"avg={avg:.2f}ms min={min(intervals_ms):.2f}ms max={max(intervals_ms):.2f}ms "
            f"p95={p95:.2f}ms first=[{first}]"
        )
    return lines


def format_rx_stats(probe: TypedStreamProbe, top_n: int) -> list[str]:
    lines: list[str] = []
    for (bus, can_id), count in probe.rx_by_bus_id.most_common(max(0, top_n)):
        values = probe.rx_times_by_bus_id.get((bus, can_id), [])
        if len(values) < 2:
            lines.append(f"rx bus={bus} can=0x{can_id:X} count={count} intervals=0")
            continue
        intervals_ms = [(values[i] - values[i - 1]) / 1000.0 for i in range(1, len(values))]
        intervals_ms = [value for value in intervals_ms if value >= 0.0]
        if not intervals_ms:
            lines.append(f"rx bus={bus} can=0x{can_id:X} count={count} intervals=no-positive")
            continue
        sorted_intervals = sorted(intervals_ms)
        p95 = sorted_intervals[min(len(sorted_intervals) - 1, int(round((len(sorted_intervals) - 1) * 0.95)))]
        p99 = sorted_intervals[min(len(sorted_intervals) - 1, int(round((len(sorted_intervals) - 1) * 0.99)))]
        lines.append(
            f"rx bus={bus} can=0x{can_id:X} count={count} "
            f"avg={sum(intervals_ms) / len(intervals_ms):.2f}ms "
            f"min={min(intervals_ms):.2f}ms max={max(intervals_ms):.2f}ms "
            f"p95={p95:.2f}ms p99={p99:.2f}ms "
            f">30={sum(1 for value in intervals_ms if value > 30.0)} "
            f">50={sum(1 for value in intervals_ms if value > 50.0)} "
            f">100={sum(1 for value in intervals_ms if value > 100.0)}"
        )
    return lines


def main() -> int:
    args = parse_args()
    min_rx_by_bus = parse_min_rx_bus(args.min_rx_bus)
    expected_tx_by_bus_id: dict[tuple[int, int], deque[bytes]] = collections.defaultdict(deque)
    probe = TypedStreamProbe(expected_tx_by_bus_id)
    sent = 0

    with serial.Serial() as ser:
        ser.port = args.port
        ser.baudrate = args.baud
        ser.timeout = 0.02
        ser.write_timeout = 1.0
        ser.dtr = True
        ser.rts = True
        ser.open()
        time.sleep(args.settle_ms / 1000.0)
        ser.reset_input_buffer()

        start = time.monotonic()
        next_send = start + 0.5
        next_heartbeat = start + 0.1
        next_lease_renew = start + 0.25
        burst_index = 0
        if not args.read_only and not args.no_arm_first:
            ser.write(build_heartbeat(3998))
            ser.write(build_control_session(3999, HOST_CONTROL_ARM, 0xFF, args.lease_ms))
            next_send = time.monotonic() + 0.05
            next_heartbeat = time.monotonic() + 0.05
            next_lease_renew = time.monotonic() + max(args.lease_renew_ms, 50) / 1000.0

        while time.monotonic() - start < args.duration_s:
            if not args.read_only and not args.no_arm_first and time.monotonic() >= next_heartbeat:
                ser.write(build_heartbeat((0x70000000 + burst_index) & 0xFFFFFFFF))
                next_heartbeat += 0.05
            if not args.read_only and not args.no_arm_first and time.monotonic() >= next_lease_renew:
                ser.write(build_control_session((0x71000000 + burst_index) & 0xFFFFFFFF,
                                                HOST_CONTROL_RENEW_LEASE,
                                                0xFF,
                                                args.lease_ms))
                next_lease_renew += max(args.lease_renew_ms, 50) / 1000.0
            if not args.read_only and burst_index < args.bursts and time.monotonic() >= next_send:
                for can_id, payload, frame in control_burst(4000 + burst_index * 10, args.bus, args.motion_pattern, burst_index):
                    expected_tx_by_bus_id[(args.bus, can_id)].append(payload)
                    ser.write(frame)
                    sent += 1
                    if args.intra_frame_gap_ms > 0:
                        time.sleep(args.intra_frame_gap_ms / 1000.0)
                burst_index += 1
                next_send += args.period_ms / 1000.0

            waiting = ser.in_waiting
            if waiting:
                probe.feed(ser.read(waiting))
            time.sleep(0.002)

    print(f"port={args.port} baud={args.baud} sent={sent} pattern={args.motion_pattern}")
    print(
        "stream "
        f"frames={probe.frames} crc_fail={probe.crc_failures} "
        f"len_fail={probe.length_failures} dropped={probe.bytes_dropped} "
        f"initial_resync={probe.initial_resync_dropped} post_sync_drop={probe.post_sync_dropped} "
        f"remaining={len(probe.buffer)}"
    )
    print("types=" + ", ".join(f"{key}:{value}" for key, value in sorted(probe.type_counts.items())))
    if probe.last_capability:
        protocol, major, minor, tx_audit, health = probe.last_capability
        print(
            f"capability=protocol:{protocol} profile:{major}.{minor} "
            f"can_tx_raw:{tx_audit} board_health:{health}"
        )
    if probe.last_health:
        _mono_us, can_rx_total, queue_depth, safety_state, flags, fault_flags = probe.last_health
        print(
            f"health=can_rx_total:{can_rx_total} queue_depth:{queue_depth} "
            f"safety_state:{safety_state} flags:0x{flags:02X} fault_flags:0x{fault_flags:08X}"
        )
    print(
        "ack_status="
        + ", ".join(f"{ACK_STATUS.get(key, key)}:{value}" for key, value in sorted(probe.ack_status.items()))
    )
    print(
        "ack_reason="
        + ", ".join(f"{ACK_REASON.get(key, key)}:{value}" for key, value in sorted(probe.ack_reason.items()))
    )
    print(f"tx_audit_count={len(probe.tx_audits)} rx_control_candidates={probe.rx_control_candidates}")
    rx_total = probe.type_counts[RECORD_CAN_RX_RAW]
    rx_by_bus = collections.Counter()
    for (bus, _can_id), count in probe.rx_by_bus_id.items():
        rx_by_bus[bus] += count
    print(
        "rx_total="
        + str(rx_total)
        + " rx_by_bus="
        + ", ".join(f"bus{bus}:{count}" for bus, count in sorted(rx_by_bus.items()))
    )
    rx_health_parity = probe.health_rx_parity()
    if rx_health_parity:
        print(
            "rx_health_parity="
            f"board_delta:{rx_health_parity['board_delta']} "
            f"stream_delta:{rx_health_parity['stream_delta']} "
            f"missing:{rx_health_parity['missing']} "
            f"samples:{rx_health_parity['health_samples']}"
        )
    expected_remaining = sum(len(queue) for queue in expected_tx_by_bus_id.values())
    print(
        f"tx_payload_match={probe.tx_payload_matches} "
        f"tx_payload_mismatch={probe.tx_payload_mismatches} "
        f"tx_payload_unexpected={probe.tx_payload_unexpected} "
        f"expected_remaining={expected_remaining}"
    )
    if probe.acks:
        first = [
            f"cmd={cmd} {ACK_STATUS.get(status, status)} {ACK_REASON.get(reason, reason)} "
            f"bus={bus} dlc={dlc_flags & 0x0F} can=0x{can_id:X}"
            for cmd, status, reason, bus, dlc_flags, can_id in probe.acks[:10]
        ]
        print("first_acks=" + " | ".join(first))
    if probe.events:
        first_events = [f"code={code} detail={detail} counter={counter}" for code, detail, counter in probe.events[:10]]
        print("first_events=" + " | ".join(first_events))
    if probe.tx_audits:
        first_tx = [
            f"bus={bus} can=0x{can_id:X} mono_us={mono_us} data={data.hex(' ').upper()}"
            for bus, can_id, mono_us, data in probe.tx_audits[:10]
        ]
        print("first_tx_audits=" + " | ".join(first_tx))
    if probe.tx_payload_mismatch_examples:
        print("tx_payload_mismatch_examples=" + " | ".join(probe.tx_payload_mismatch_examples))
    if args.report_periods and probe.tx_audits:
        for line in format_period_stats(probe.tx_audits):
            print(line)
    if args.report_rx:
        for line in format_rx_stats(probe, args.top_rx):
            print(line)

    json_report = {
        "port": args.port,
        "baud": args.baud,
        "sent": sent,
        "pattern": args.motion_pattern,
        "frames": probe.frames,
        "crc_failures": probe.crc_failures,
        "length_failures": probe.length_failures,
        "bytes_dropped": probe.bytes_dropped,
        "initial_resync_dropped": probe.initial_resync_dropped,
        "post_sync_dropped": probe.post_sync_dropped,
        "remaining_buffer": len(probe.buffer),
        "type_counts": {str(key): value for key, value in sorted(probe.type_counts.items())},
        "rx_total": rx_total,
        "rx_by_bus": {str(bus): count for bus, count in sorted(rx_by_bus.items())},
        "rx_by_bus_id": {f"{bus}:0x{can_id:X}": count for (bus, can_id), count in sorted(probe.rx_by_bus_id.items())},
        "rx_health_parity": rx_health_parity,
        "tx_audit_count": len(probe.tx_audits),
        "tx_payload_match": probe.tx_payload_matches,
        "tx_payload_mismatch": probe.tx_payload_mismatches,
        "tx_payload_unexpected": probe.tx_payload_unexpected,
        "expected_remaining": expected_remaining,
        "ack_status": {str(key): value for key, value in sorted(probe.ack_status.items())},
        "ack_reason": {str(key): value for key, value in sorted(probe.ack_reason.items())},
        "last_capability": probe.last_capability,
        "last_health": probe.last_health,
        "events": probe.events[:32],
    }
    if args.json_out:
        out_path = Path(args.json_out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(json_report, indent=2, sort_keys=True), encoding="utf-8")
        print(f"json_out={out_path}")

    if probe.crc_failures or probe.length_failures or probe.post_sync_dropped:
        print("FAIL: typed stream transport errors were observed")
        return 2
    if not probe.type_counts[RECORD_CAPABILITY] or not probe.type_counts[RECORD_BOARD_HEALTH]:
        print("FAIL: CAPABILITY and BOARD_HEALTH were not both observed")
        return 2
    if args.min_rx_total and rx_total < args.min_rx_total:
        print(f"FAIL: CAN_RX_RAW total {rx_total} is lower than required {args.min_rx_total}")
        return 2
    for bus, minimum in sorted(min_rx_by_bus.items()):
        observed = rx_by_bus.get(bus, 0)
        if observed < minimum:
            print(f"FAIL: CAN_RX_RAW bus {bus} count {observed} is lower than required {minimum}")
            return 2
    if args.check_rx_health_parity:
        if not rx_health_parity:
            print("FAIL: not enough BOARD_HEALTH samples for RX health parity")
            return 2
        if rx_health_parity["missing"] > args.max_rx_health_missing:
            print(
                "FAIL: board CAN RX total increased more than observed CAN_RX_RAW stream "
                f"missing={rx_health_parity['missing']} limit={args.max_rx_health_missing}"
            )
            return 2
    if args.max_queue_depth >= 0 and probe.last_health and probe.last_health[2] > args.max_queue_depth:
        print(f"FAIL: board queue_depth {probe.last_health[2]} exceeds limit {args.max_queue_depth}")
        return 2
    if args.max_fault_flags >= 0 and probe.last_health and (probe.last_health[5] & ~args.max_fault_flags) != 0:
        print(f"FAIL: board fault_flags 0x{probe.last_health[5]:08X} exceeds allowed mask 0x{args.max_fault_flags:08X}")
        return 2
    if args.expect_tx and not probe.tx_audits:
        print("FAIL: no CAN_TX_RAW audit observed; CAN TX success is not proven")
        return 2
    rejected_control = [ack for ack in probe.control_request_acks if ack[1] == 0]
    if rejected_control:
        first = rejected_control[0]
        print(
            "FAIL: control request rejected "
            f"cmd={first[0]} reason={ACK_REASON.get(first[2], first[2])} bus={first[3]} can=0x{first[5]:X}"
        )
        return 2
    if args.expect_tx and len(probe.tx_audits) < sent:
        print(f"FAIL: CAN_TX_RAW audit count {len(probe.tx_audits)} is lower than sent control requests {sent}")
        return 2
    if args.expect_tx and (probe.tx_payload_mismatches or expected_remaining):
        print(
            "FAIL: CAN_TX_RAW payload parity failed "
            f"mismatch={probe.tx_payload_mismatches} remaining={expected_remaining}"
        )
        return 2
    if probe.ack_reason.get(10):
        print("BLOCKED: board rejected HOST_CAN_TX_REQUEST with HOST_TIMEOUT")
        return 2
    if probe.ack_reason.get(11):
        print("BLOCKED: board rejected HOST_CAN_TX_REQUEST with CONTROL_LEASE_EXPIRED")
        return 2

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

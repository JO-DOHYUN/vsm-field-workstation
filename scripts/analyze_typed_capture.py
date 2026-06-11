#!/usr/bin/env python3
"""Summarize VSM typed evidence capture sessions.

The report is evidence-first: CAN_RX, CAN_TX audit, BOARD_HEALTH, and
BOARD_EVENT are reported separately. It never treats ACK or health records as
CAN frames.
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import statistics
import struct


SOF = b"\xA5\x5A"
TYPE_NAMES = {
    1: "CAN_RX_RAW",
    2: "CAN_TX_RAW",
    5: "ADC_SAMPLE",
    6: "CONTROL_ACK",
    7: "BOARD_EVENT",
    8: "BOARD_HEALTH",
    9: "CAPABILITY",
    10: "HOST_CAN_TX_REQUEST",
    11: "HOST_HEARTBEAT",
    12: "HOST_CONTROL_SESSION",
}


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


def interval_stats_ms(times_us: list[int]) -> str:
    if len(times_us) < 2:
        return "single"
    values = [(times_us[index] - times_us[index - 1]) / 1000.0 for index in range(1, len(times_us))]
    values = [value for value in values if value >= 0.0]
    if not values:
        return "no-positive-intervals"
    sorted_values = sorted(values)
    p95 = sorted_values[int(0.95 * (len(sorted_values) - 1))]
    p99 = sorted_values[int(0.99 * (len(sorted_values) - 1))]
    return (
        f"avg={statistics.mean(values):.2f}ms min={min(values):.2f}ms "
        f"p95={p95:.2f}ms p99={p99:.2f}ms max={max(values):.2f}ms "
        f">30={sum(1 for value in values if value > 30.0)} "
        f">50={sum(1 for value in values if value > 50.0)} "
        f">100={sum(1 for value in values if value > 100.0)}"
    )


def parse_capture(session: pathlib.Path) -> dict:
    stream = session / "capture.stream"
    data = stream.read_bytes()
    pos = 0
    seq_prev: int | None = None
    records = 0
    counters: collections.Counter[int] = collections.Counter()
    can_rx: dict[tuple[int, int], list[tuple[int, bytes]]] = collections.defaultdict(list)
    can_tx: dict[tuple[int, int], list[tuple[int, bytes]]] = collections.defaultdict(list)
    events: collections.Counter[tuple[int, int]] = collections.Counter()
    health: list[tuple[int, int, int, int, int, int]] = []
    seq_gaps = 0
    crc_failures = 0
    length_failures = 0
    bytes_dropped = 0

    while pos + 11 <= len(data):
        sof = data.find(SOF, pos)
        if sof < 0:
            bytes_dropped += len(data) - pos
            break
        if sof > pos:
            bytes_dropped += sof - pos
            pos = sof

        version, record_type, flags, seq, payload_len = struct.unpack_from("<BBBHH", data, pos + 2)
        del version, flags
        if payload_len > 4096:
            length_failures += 1
            pos += 1
            continue
        frame_len = 11 + payload_len
        if pos + frame_len > len(data):
            break
        frame = data[pos : pos + frame_len]
        expected_crc = struct.unpack_from("<H", frame, 9 + payload_len)[0]
        actual_crc = crc16_ccitt(frame[2 : 9 + payload_len])
        if expected_crc != actual_crc:
            crc_failures += 1
            pos += 1
            continue

        records += 1
        counters[record_type] += 1
        if seq_prev is not None and ((seq_prev + 1) & 0xFFFF) != seq:
            seq_gaps += 1
        seq_prev = seq

        payload = frame[9 : 9 + payload_len]
        if record_type in (1, 2) and payload_len >= 30:
            mono_us = struct.unpack_from("<Q", payload, 0)[0]
            can_id = struct.unpack_from("<I", payload, 8)[0] & 0x1FFFFFFF
            dlc = payload[12] & 0x0F
            bus = payload[13]
            frame_data = payload[14 : 14 + min(dlc, 8)]
            target = can_tx if record_type == 2 else can_rx
            target[(bus, can_id)].append((mono_us, bytes(frame_data)))
        elif record_type == 7 and payload_len >= 16:
            code = struct.unpack_from("<H", payload, 8)[0]
            detail = struct.unpack_from("<H", payload, 10)[0]
            events[(code, detail)] += 1
        elif record_type == 8 and payload_len >= 52:
            mono_us = struct.unpack_from("<Q", payload, 0)[0]
            can_rx_total = struct.unpack_from("<I", payload, 8)[0]
            drop_total = struct.unpack_from("<I", payload, 12)[0]
            fifo_total = struct.unpack_from("<I", payload, 16)[0]
            serial_total = struct.unpack_from("<I", payload, 20)[0]
            queue_depth = struct.unpack_from("<I", payload, 24)[0]
            health.append((mono_us, can_rx_total, drop_total, fifo_total, serial_total, queue_depth))

        pos += frame_len

    return {
        "session": session,
        "stream_bytes": len(data),
        "records": records,
        "counters": counters,
        "seq_gaps": seq_gaps,
        "crc_failures": crc_failures,
        "length_failures": length_failures,
        "bytes_dropped": bytes_dropped,
        "can_rx": can_rx,
        "can_tx": can_tx,
        "events": events,
        "health": health,
    }


def print_report(report: dict, top: int) -> None:
    print(f"\n== {report['session']} ==")
    print(
        f"stream_bytes={report['stream_bytes']} records={report['records']} "
        f"seq_gaps={report['seq_gaps']} crc={report['crc_failures']} "
        f"length={report['length_failures']} dropped_bytes={report['bytes_dropped']}"
    )
    print("types=" + ", ".join(f"{TYPE_NAMES.get(key, key)}:{value}" for key, value in sorted(report["counters"].items())))

    health = report["health"]
    if health:
        first = health[0]
        last = health[-1]
        print(
            "health "
            f"can_rx_delta={last[1] - first[1]} drop_total={last[2]} "
            f"fifo_delta={last[3] - first[3]} fifo_total={last[3]} "
            f"serial_delta={last[4] - first[4]} max_queue={max(item[5] for item in health)}"
        )

    if report["events"]:
        print("events top=" + ", ".join(
            f"code={code}/detail=0x{detail:04X}:{count}"
            for (code, detail), count in report["events"].most_common(10)
        ))

    print("top CAN_RX:")
    rows = []
    for (bus, can_id), values in report["can_rx"].items():
        times = [value[0] for value in values]
        first_payload = values[0][1].hex(" ").upper()
        last_payload = values[-1][1].hex(" ").upper()
        rows.append((len(values), bus, can_id, interval_stats_ms(times), first_payload, last_payload))
    for count, bus, can_id, stats, first_payload, last_payload in sorted(rows, reverse=True)[:top]:
        print(f"  count={count:7d} bus={bus} id=0x{can_id:X} {stats} payload={first_payload}->{last_payload}")

    tx_count = sum(len(values) for values in report["can_tx"].values())
    print(f"CAN_TX_RAW audit count={tx_count}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", help="typed session directories or capture.stream files")
    parser.add_argument("--top", type=int, default=24)
    args = parser.parse_args()

    for raw in args.paths:
        path = pathlib.Path(raw)
        session = path.parent if path.name == "capture.stream" else path
        print_report(parse_capture(session), args.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

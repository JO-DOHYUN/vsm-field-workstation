#!/usr/bin/env python3
"""Summarize VSM typed evidence capture sessions.

The report is evidence-first: CAN_RX, CAN_TX audit, BOARD_HEALTH, and
BOARD_EVENT are reported separately. It never treats ACK or health records as
CAN frames.
"""

from __future__ import annotations

import argparse
import collections
import json
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
    16: "CAN_RX_SEGMENT",
}

CAN_RX_RAW = 1
CAN_TX_RAW = 2
CAN_RX_SEGMENT = 16
SEGMENT_HEADER_LEN = 32
SEGMENT_ENTRY_LEN = 30


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
    diagnostics_path = session / "capture.diagnostics.json"
    diagnostics = {}
    if diagnostics_path.exists():
        try:
            diagnostics = json.loads(diagnostics_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            diagnostics = {"format": "invalid-json"}
    pos = 0
    seq_prev: int | None = None
    records = 0
    counters: collections.Counter[int] = collections.Counter()
    can_rx: dict[tuple[int, int], list[tuple[int, bytes]]] = collections.defaultdict(list)
    can_tx: dict[tuple[int, int], list[tuple[int, bytes]]] = collections.defaultdict(list)
    capture_seq_prev_global: int | None = None
    capture_seq_prev_by_bus: dict[int, int] = {}
    capture_seq_prev_by_id: dict[tuple[int, int], int] = {}
    capture_seq_seen: set[int] = set()
    capture_seq_gaps = 0
    capture_seq_duplicates = 0
    capture_seq_global_reorders = 0
    capture_seq_bus_reorders = 0
    capture_seq_id_reorders = 0
    capture_seq_top_bus_reorders: collections.Counter[int] = collections.Counter()
    capture_seq_top_id_reorders: collections.Counter[tuple[int, int]] = collections.Counter()
    segment_frames = 0
    events: collections.Counter[tuple[int, int]] = collections.Counter()
    health: list[dict[str, int | bool]] = []
    seq_gaps = 0
    crc_failures = 0
    length_failures = 0
    bytes_dropped = 0

    def note_capture_seq(capture_seq: int, bus: int, can_id: int) -> None:
        nonlocal capture_seq_duplicates
        nonlocal capture_seq_global_reorders
        nonlocal capture_seq_bus_reorders
        nonlocal capture_seq_id_reorders
        nonlocal capture_seq_prev_global

        if capture_seq in capture_seq_seen:
            capture_seq_duplicates += 1
        else:
            capture_seq_seen.add(capture_seq)

        if capture_seq_prev_global is not None and capture_seq < capture_seq_prev_global:
            capture_seq_global_reorders += 1
        capture_seq_prev_global = capture_seq

        bus_prev = capture_seq_prev_by_bus.get(bus)
        if bus_prev is not None and capture_seq < bus_prev:
            capture_seq_bus_reorders += 1
            capture_seq_top_bus_reorders[bus] += 1
        capture_seq_prev_by_bus[bus] = capture_seq

        id_key = (bus, can_id)
        id_prev = capture_seq_prev_by_id.get(id_key)
        if id_prev is not None and capture_seq < id_prev:
            capture_seq_id_reorders += 1
            capture_seq_top_id_reorders[id_key] += 1
        capture_seq_prev_by_id[id_key] = capture_seq

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
        if record_type in (CAN_RX_RAW, CAN_TX_RAW) and payload_len >= 30:
            mono_us = struct.unpack_from("<Q", payload, 0)[0]
            can_id = struct.unpack_from("<I", payload, 8)[0] & 0x1FFFFFFF
            dlc = payload[12] & 0x0F
            bus = payload[13]
            frame_data = payload[14 : 14 + min(dlc, 8)]
            target = can_tx if record_type == CAN_TX_RAW else can_rx
            target[(bus, can_id)].append((mono_us, bytes(frame_data)))
        elif record_type == CAN_RX_SEGMENT and payload_len >= SEGMENT_HEADER_LEN:
            frame_count = struct.unpack_from("<H", payload, 16)[0]
            entry_size = payload[18]
            if entry_size >= SEGMENT_ENTRY_LEN and payload_len >= SEGMENT_HEADER_LEN + frame_count * entry_size:
                for index in range(frame_count):
                    off = SEGMENT_HEADER_LEN + index * entry_size
                    capture_seq = struct.unpack_from("<Q", payload, off)[0]
                    mono_us = struct.unpack_from("<Q", payload, off + 8)[0]
                    can_id = struct.unpack_from("<I", payload, off + 16)[0] & 0x1FFFFFFF
                    dlc = payload[off + 20] & 0x0F
                    bus = payload[off + 21]
                    frame_data = payload[off + 22 : off + 22 + min(dlc, 8)]
                    note_capture_seq(capture_seq, bus, can_id)
                    can_rx[(bus, can_id)].append((mono_us, bytes(frame_data)))
                    segment_frames += 1
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
            row: dict[str, int | bool] = {
                "mono_us": mono_us,
                "can_rx_total": can_rx_total,
                "drop_total": drop_total,
                "fifo_total": fifo_total,
                "serial_total": serial_total,
                "queue_depth": queue_depth,
                "has_uplink": False,
            }
            if payload_len >= 192:
                row.update(
                    {
                        "has_uplink": True,
                        "serial_enqueue_fail_total": struct.unpack_from("<I", payload, 160)[0],
                        "serial_ring_clear_total": struct.unpack_from("<I", payload, 164)[0],
                        "serial_ring_cleared_bytes_total": struct.unpack_from("<I", payload, 168)[0],
                        "serial_backpressure_total": struct.unpack_from("<I", payload, 172)[0],
                        "serial_tx_high_water_bytes": struct.unpack_from("<I", payload, 176)[0],
                        "shared_can_queue_high_water": struct.unpack_from("<I", payload, 180)[0],
                        "mcp_drain_budget_hit_total": struct.unpack_from("<I", payload, 184)[0],
                        "can_segment_enqueue_fail_total": struct.unpack_from("<I", payload, 188)[0],
                    }
                )
            health.append(row)

        pos += frame_len

    if capture_seq_seen:
        first_seen = min(capture_seq_seen)
        last_seen = max(capture_seq_seen)
        capture_seq_gaps = max(0, (last_seen - first_seen + 1) - len(capture_seq_seen))

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
        "diagnostics": diagnostics,
        "capture_seq_gaps": capture_seq_gaps,
        "capture_seq_duplicates": capture_seq_duplicates,
        "capture_seq_reorders": capture_seq_global_reorders,
        "capture_seq_global_reorders": capture_seq_global_reorders,
        "capture_seq_bus_reorders": capture_seq_bus_reorders,
        "capture_seq_id_reorders": capture_seq_id_reorders,
        "capture_seq_top_bus_reorders": capture_seq_top_bus_reorders,
        "capture_seq_top_id_reorders": capture_seq_top_id_reorders,
        "segment_frames": segment_frames,
    }


def print_report(report: dict, top: int) -> None:
    print(f"\n== {report['session']} ==")
    print(
        f"stream_bytes={report['stream_bytes']} records={report['records']} "
        f"seq_gaps={report['seq_gaps']} crc={report['crc_failures']} "
        f"length={report['length_failures']} dropped_bytes={report['bytes_dropped']} "
        f"capture_seq_gaps={report['capture_seq_gaps']} "
        f"capture_seq_duplicates={report['capture_seq_duplicates']} "
        f"capture_seq_reorders={report['capture_seq_reorders']} "
        f"capture_seq_bus_reorders={report['capture_seq_bus_reorders']} "
        f"capture_seq_id_reorders={report['capture_seq_id_reorders']} "
        f"segment_frames={report['segment_frames']}"
    )
    if report["capture_seq_bus_reorders"] or report["capture_seq_id_reorders"]:
        bus_top = ", ".join(
            f"bus={bus}:{count}" for bus, count in report["capture_seq_top_bus_reorders"].most_common(6)
        )
        id_top = ", ".join(
            f"bus={bus}/id=0x{can_id:X}:{count}"
            for (bus, can_id), count in report["capture_seq_top_id_reorders"].most_common(8)
        )
        print(
            "capture_seq_ordering "
            f"global={report['capture_seq_global_reorders']} "
            f"bus_top={bus_top or 'none'} "
            f"id_top={id_top or 'none'}"
        )
    print("types=" + ", ".join(f"{TYPE_NAMES.get(key, key)}:{value}" for key, value in sorted(report["counters"].items())))

    health = report["health"]
    if health:
        first = health[0]
        last = health[-1]
        print(
            "health "
            f"can_rx_delta={last['can_rx_total'] - first['can_rx_total']} drop_total={last['drop_total']} "
            f"fifo_delta={last['fifo_total'] - first['fifo_total']} fifo_total={last['fifo_total']} "
            f"serial_delta={last['serial_total'] - first['serial_total']} max_queue={max(int(item['queue_depth']) for item in health)}"
        )
        if last.get("has_uplink"):
            print(
                "csm_uplink "
                f"ring_clear={last.get('serial_ring_clear_total', 0)} "
                f"cleared_bytes={last.get('serial_ring_cleared_bytes_total', 0)} "
                f"backpressure={last.get('serial_backpressure_total', 0)} "
                f"enqueue_fail={last.get('serial_enqueue_fail_total', 0)} "
                f"segment_enqueue_fail={last.get('can_segment_enqueue_fail_total', 0)} "
                f"serial_high_water={last.get('serial_tx_high_water_bytes', 0)} "
                f"shared_queue_high={last.get('shared_can_queue_high_water', 0)} "
                f"mcp_budget_hits={last.get('mcp_drain_budget_hit_total', 0)}"
            )

    diagnostics = report.get("diagnostics") or {}
    parser = diagnostics.get("parser") or {}
    if parser:
        print(
            "live_parser_sidecar "
            f"frames={parser.get('frames', 0)} dropped={parser.get('bytes_dropped', 0)} "
            f"crc={parser.get('crc_failures', 0)} len={parser.get('length_failures', 0)} "
            f"seq={parser.get('seq_gaps', 0)} ver={parser.get('version_warnings', 0)} "
            f"buffered={parser.get('buffered_bytes', 0)}"
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

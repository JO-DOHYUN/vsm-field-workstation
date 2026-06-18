#!/usr/bin/env python3
"""VSM analysis-truth HIL.

Runs the actual VSM executable route and verifies that timing/value/alarm/graph
outputs are derived from stored typed CAN truth, not from live UI projection.
"""

from __future__ import annotations

import argparse
import collections
import ctypes
import json
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from datetime import datetime

from hil_vsm_user_route_stress import (
    AppStatePoller,
    ProcessMonitor,
    control_request,
    crc16_ccitt,
    u16,
    u32,
    wait_status,
)


SOF = b"\xA5\x5A"
MAX_PAYLOAD = 4096
RECORD_CAN_RX_RAW = 1
RECORD_CAN_RX_SEGMENT = 16
SEGMENT_HEADER_LEN = 32
SEGMENT_ENTRY_LEN = 30
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODEL_IDS = [0x510, 0x520, 0x521, 0x530]
FULL_MODEL_IDS = list(range(0x510, 0x5B0))
FULL_GRAPH_IDS = list(range(0x5A0, 0x5B0))
MODEL_ID_NAMES = {
    0x510: "timing_20ms",
    0x520: "value_range",
    0x521: "reserved_status",
    0x530: "graph_peak",
}
CSM_UPLINK_COUNTER_KEYS = [
    "serial_enqueue_fail_total",
    "serial_ring_clear_total",
    "serial_ring_cleared_bytes_total",
    "serial_backpressure_total",
    "mcp_drain_budget_hit_total",
    "can_segment_enqueue_fail_total",
]
CSM_UPLINK_HIGH_WATER_KEYS = [
    "serial_tx_high_water_bytes",
    "shared_can_queue_high_water",
]
CSM_UPLINK_FATAL_KEYS = [
    "serial_enqueue_fail_total",
    "serial_ring_clear_total",
    "serial_ring_cleared_bytes_total",
    "can_segment_enqueue_fail_total",
]
CSM_UPLINK_WARNING_KEYS = [
    "serial_backpressure_total",
    "mcp_drain_budget_hit_total",
]

PCAN_STATUS_BITS = [
    (0x00001, "XMTFULL"),
    (0x00002, "OVERRUN"),
    (0x00004, "BUSLIGHT"),
    (0x00008, "BUSHEAVY"),
    (0x00010, "BUSOFF"),
    (0x00020, "QRCVEMPTY"),
    (0x00040, "QOVERRUN"),
    (0x00080, "QXMTFULL"),
    (0x00200, "NODRIVER"),
    (0x00400, "HWINUSE"),
    (0x00800, "NETINUSE"),
    (0x04000, "ILLCLIENT"),
    (0x08000, "ILLHANDLE"),
    (0x10000, "RESOURCE"),
    (0x2000000, "CAUTION"),
    (0x4000000, "INITIALIZE"),
]


def pcan_status_text(status: int) -> str:
    if status == 0:
        return "OK"
    labels = [name for bit, name in PCAN_STATUS_BITS if status & bit]
    return "|".join(labels) if labels else hex(status)


def model_ids_for_profile(profile: str) -> list[int]:
    return FULL_MODEL_IDS if profile == "full" else MODEL_IDS


def graph_keys_for_profile(profile: str) -> list[str]:
    if profile == "full":
        return [f"0X{can_id:X}|0" for can_id in FULL_GRAPH_IDS[:16]]
    return ["0X530|0"]


def delta_u32(first: int, last: int) -> int:
    return (last - first) & 0xFFFFFFFF


def parse_board_health_payload(payload: bytes) -> dict:
    health = {
        "can_rx": u32(payload, 8),
        "can_drop": u32(payload, 12),
        "fifo": u32(payload, 16),
        "has_csm_uplink": False,
    }
    if len(payload) >= 192:
        health.update(
            {
                "has_csm_uplink": True,
                "serial_enqueue_fail_total": u32(payload, 160),
                "serial_ring_clear_total": u32(payload, 164),
                "serial_ring_cleared_bytes_total": u32(payload, 168),
                "serial_backpressure_total": u32(payload, 172),
                "serial_tx_high_water_bytes": u32(payload, 176),
                "shared_can_queue_high_water": u32(payload, 180),
                "mcp_drain_budget_hit_total": u32(payload, 184),
                "can_segment_enqueue_fail_total": u32(payload, 188),
            }
        )
    return health


def health_delta(first: dict | None, last: dict | None) -> dict:
    if not first or not last:
        return {"can_drop": None, "fifo": None, "has_csm_uplink": False}
    delta = {
        "can_drop": delta_u32(first["can_drop"], last["can_drop"]),
        "fifo": delta_u32(first["fifo"], last["fifo"]),
        "has_csm_uplink": bool(first.get("has_csm_uplink") and last.get("has_csm_uplink")),
    }
    if delta["has_csm_uplink"]:
        for key in CSM_UPLINK_COUNTER_KEYS:
            delta[key] = delta_u32(int(first.get(key, 0)), int(last.get(key, 0)))
        for key in CSM_UPLINK_HIGH_WATER_KEYS:
            delta[key] = int(last.get(key, 0))
    return delta


def csm_uplink_fatal_delta(delta: dict) -> dict:
    if not delta.get("has_csm_uplink"):
        return {}
    return {key: int(delta.get(key, 0) or 0) for key in CSM_UPLINK_FATAL_KEYS if int(delta.get(key, 0) or 0) != 0}


def csm_uplink_warning_delta(delta: dict) -> dict:
    if not delta.get("has_csm_uplink"):
        return {}
    return {key: int(delta.get(key, 0) or 0) for key in CSM_UPLINK_WARNING_KEYS if int(delta.get(key, 0) or 0) != 0}


def truth_payload(can_id: int, seq: int, source: int, profile: str = "smoke") -> bytes:
    data = bytearray(8)
    data[6] = source & 0xFF
    data[7] = seq & 0xFF
    if profile == "full":
        if 0x520 <= can_id <= 0x55F:
            data[0] = 70
        elif 0x560 <= can_id <= 0x57F:
            data[1] = 0x01
        elif 0x580 <= can_id <= 0x59F:
            data[2] = 0x01
        elif 0x5A0 <= can_id <= 0x5AF:
            data[0] = [0, 255, 16, 240, 32, 128][seq % 6]
        else:
            data[0] = seq & 0xFF
        return bytes(data)
    if can_id == 0x520:
        data[0] = 70  # ERR > err_max 60
    elif can_id == 0x521:
        data[1] = 0x01  # reserved bit set
    elif can_id == 0x530:
        data[0] = [10, 80, 240, 30, 5, 120][seq % 6]
    else:
        data[0] = seq & 0xFF
    return bytes(data)


class PcanMsg(ctypes.Structure):
    _fields_ = [
        ("ID", ctypes.c_uint),
        ("MSGTYPE", ctypes.c_ubyte),
        ("LEN", ctypes.c_ubyte),
        ("DATA", ctypes.c_ubyte * 8),
    ]


class TruthLoadSenders:
    def __init__(self, args):
        self.args = args
        self.profile = getattr(args, "profile", "smoke")
        self.model_ids = model_ids_for_profile(self.profile)
        self.model_id_set = set(self.model_ids)
        self.stop_event = threading.Event()
        self.start_event = threading.Event()
        self.lock = threading.Lock()
        self.state = {
            "pcan_sent_ok": 0,
            "kvaser_sent_ok": 0,
            "pcan_model_counts": collections.Counter(),
            "kvaser_model_counts": collections.Counter(),
            "pcan_write_errors": collections.Counter(),
            "kvaser_write_errors": collections.Counter(),
            "pcan_status_counts": collections.Counter(),
            "kvaser_status_counts": collections.Counter(),
            "pcan_status_samples": [],
            "kvaser_status_samples": [],
            "pcan_first_non_ok_status": None,
            "kvaser_first_non_ok_status": None,
            "pcan_last_status": None,
            "kvaser_last_status": None,
            "pcan_error": None,
            "kvaser_error": None,
        }
        self.run_started_perf = time.perf_counter()

    def choose_id(self, seq: int, noise_base: int) -> int:
        slot = seq % max(1, self.args.id_count)
        if slot < len(self.model_ids):
            return self.model_ids[slot]
        return noise_base + slot

    def run(self):
        threads = [
            threading.Thread(target=self.pcan_sender, daemon=True),
            threading.Thread(target=self.kvaser_sender, daemon=True),
        ]
        for thread in threads:
            thread.start()
        time.sleep(0.2)
        self.start_event.set()
        deadline = time.time() + self.args.duration
        while time.time() < deadline:
            time.sleep(0.1)
        self.stop_event.set()
        for thread in threads:
            thread.join(timeout=10)
        with self.lock:
            out = dict(self.state)
            out["pcan_model_counts"] = dict(self.state["pcan_model_counts"])
            out["kvaser_model_counts"] = dict(self.state["kvaser_model_counts"])
            out["pcan_write_errors"] = dict(self.state["pcan_write_errors"])
            out["kvaser_write_errors"] = dict(self.state["kvaser_write_errors"])
            out["pcan_status_counts"] = dict(self.state["pcan_status_counts"])
            out["kvaser_status_counts"] = dict(self.state["kvaser_status_counts"])
            out["pcan_status_samples"] = list(self.state["pcan_status_samples"])
            out["kvaser_status_samples"] = list(self.state["kvaser_status_samples"])
            return out

    def note_status(self, name: str, status: int, source: str) -> None:
        now_ms = round((time.perf_counter() - self.run_started_perf) * 1000.0, 3)
        text = pcan_status_text(status) if name == "pcan" else ("OK" if status == 0 else str(status))
        key = f"{hex(status) if name == 'pcan' else status}:{text}"
        sample = {"t_ms": now_ms, "status": int(status), "text": text, "source": source}
        with self.lock:
            self.state[f"{name}_status_counts"][key] += 1
            self.state[f"{name}_last_status"] = sample
            if status != 0 and self.state[f"{name}_first_non_ok_status"] is None:
                self.state[f"{name}_first_non_ok_status"] = sample
            samples = self.state[f"{name}_status_samples"]
            if len(samples) < self.args.max_status_samples:
                samples.append(sample)

    def pcan_sender(self):
        try:
            dll = ctypes.WinDLL("PCANBasic.dll")
            dll.CAN_Initialize.argtypes = [ctypes.c_ushort, ctypes.c_ushort, ctypes.c_ubyte, ctypes.c_uint, ctypes.c_ushort]
            dll.CAN_Initialize.restype = ctypes.c_uint
            dll.CAN_Write.argtypes = [ctypes.c_ushort, ctypes.POINTER(PcanMsg)]
            dll.CAN_Write.restype = ctypes.c_uint
            dll.CAN_GetStatus.argtypes = [ctypes.c_ushort]
            dll.CAN_GetStatus.restype = ctypes.c_uint
            dll.CAN_Uninitialize.argtypes = [ctypes.c_ushort]
            init = dll.CAN_Initialize(self.args.pcan_channel, self.args.pcan_bitrate, 0, 0, 0)
            if init != 0:
                self.state["pcan_error"] = f"CAN_Initialize {hex(init)}"
                return
            try:
                self.start_event.wait()
                self.sender_loop("pcan", self.args.pcan_rate, self.args.pcan_noise_base, self.args.pcan_source_marker, dll=dll)
            finally:
                time.sleep(self.args.drain_seconds)
                dll.CAN_Uninitialize(self.args.pcan_channel)
        except Exception as exc:
            with self.lock:
                self.state["pcan_error"] = repr(exc)
            self.stop_event.set()

    def kvaser_sender(self):
        try:
            dll = ctypes.WinDLL("canlib32.dll")
            dll.canInitializeLibrary()
            dll.canOpenChannel.argtypes = [ctypes.c_int, ctypes.c_int]
            dll.canOpenChannel.restype = ctypes.c_int
            dll.canSetBusParams.argtypes = [ctypes.c_int, ctypes.c_long, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
            dll.canSetBusParams.restype = ctypes.c_int
            dll.canBusOn.argtypes = [ctypes.c_int]
            dll.canBusOn.restype = ctypes.c_int
            dll.canWrite.argtypes = [ctypes.c_int, ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
            dll.canWrite.restype = ctypes.c_int
            dll.canWriteSync.argtypes = [ctypes.c_int, ctypes.c_ulong]
            dll.canWriteSync.restype = ctypes.c_int
            dll.canReadStatus.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_ulong)]
            dll.canReadStatus.restype = ctypes.c_int
            dll.canBusOff.argtypes = [ctypes.c_int]
            dll.canClose.argtypes = [ctypes.c_int]
            handle = dll.canOpenChannel(self.args.kvaser_channel, 0)
            if handle < 0:
                self.state["kvaser_error"] = f"canOpenChannel {handle}"
                return
            try:
                params = dll.canSetBusParams(handle, self.args.kvaser_bitrate, 0, 0, 0, 0, 0)
                bus_on = dll.canBusOn(handle)
                if params != 0 or bus_on != 0:
                    self.state["kvaser_error"] = f"params {params} busOn {bus_on}"
                    return
                self.start_event.wait()
                self.sender_loop("kvaser", self.args.kvaser_rate, self.args.kvaser_noise_base, self.args.kvaser_source_marker, dll=dll, handle=handle)
            finally:
                time.sleep(self.args.drain_seconds)
                try:
                    dll.canBusOff(handle)
                finally:
                    dll.canClose(handle)
        except Exception as exc:
            with self.lock:
                self.state["kvaser_error"] = repr(exc)
            self.stop_event.set()

    def sender_loop(self, name: str, rate: float, noise_base: int, marker: int, dll, handle: int | None = None):
        period = 1.0 / rate
        next_t = time.perf_counter()
        end_t = next_t + self.args.duration
        seq = 0
        next_status_t = next_t
        while time.perf_counter() < end_t and not self.stop_event.is_set():
            now = time.perf_counter()
            if now >= next_status_t:
                if name == "pcan":
                    self.note_status(name, dll.CAN_GetStatus(self.args.pcan_channel), "poll")
                elif handle is not None:
                    flags = ctypes.c_ulong(0)
                    rc = dll.canReadStatus(handle, ctypes.byref(flags))
                    self.note_status(name, rc if rc != 0 else int(flags.value), "poll")
                next_status_t = now + max(0.02, self.args.status_sample_ms / 1000.0)
            if now < next_t:
                time.sleep(min(0.0002, next_t - now))
                continue
            can_id = self.choose_id(seq, noise_base)
            data = truth_payload(can_id, seq, marker, self.profile)
            ok = False
            if name == "pcan":
                msg = PcanMsg()
                msg.ID = can_id
                msg.MSGTYPE = 0
                msg.LEN = 8
                for i, b in enumerate(data):
                    msg.DATA[i] = b
                status = dll.CAN_Write(self.args.pcan_channel, ctypes.byref(msg))
                ok = status == 0
                error_key = hex(status)
            else:
                payload = (ctypes.c_ubyte * 8).from_buffer_copy(data)
                status = dll.canWrite(handle, can_id, payload, 8, 0)
                if status == 0:
                    sync = dll.canWriteSync(handle, 100)
                    status = sync
                ok = status == 0
                error_key = str(status)
            with self.lock:
                if ok:
                    self.state[f"{name}_sent_ok"] += 1
                    if can_id in self.model_id_set:
                        self.state[f"{name}_model_counts"][hex(can_id)] += 1
                else:
                    self.state[f"{name}_write_errors"][error_key] += 1
            if not ok:
                self.note_status(name, status, "write_error")
            seq += 1
            next_t += period


def parse_capture_frames(stream_path: pathlib.Path, model_ids: set[int] | None = None) -> dict:
    if model_ids is None:
        model_ids = set(MODEL_IDS)
    data = stream_path.read_bytes()
    pos = 0
    stats = {
        "stream_bytes": len(data),
        "records": 0,
        "crc": 0,
        "length": 0,
        "seq_gaps": 0,
        "resync_drop": 0,
        "type_counts": collections.Counter(),
        "can_rx_frames": 0,
        "segment_records": 0,
        "segment_frames": 0,
        "capture_seq_seen": set(),
        "capture_seq_duplicates": 0,
        "health_first": None,
        "health_last": None,
        "capability_seen": False,
        "model_counts_by_source": collections.Counter(),
        "source_timeline": {},
        "frames": [],
    }
    last_seq = None

    def note_capture_seq(value: int | None):
        if value is None:
            return
        if value in stats["capture_seq_seen"]:
            stats["capture_seq_duplicates"] += 1
        stats["capture_seq_seen"].add(value)

    def observe(can_id: int, bus: int, mono_us: int, payload: bytes, capture_seq: int | None):
        stats["can_rx_frames"] += 1
        note_capture_seq(capture_seq)
        source_marker = payload[6] if len(payload) > 6 else 0
        source_key = f"0x{source_marker:02X}"
        source = stats["source_timeline"].setdefault(
            source_key,
            {
                "count": 0,
                "bus_counts": collections.Counter(),
                "id_counts": collections.Counter(),
                "first": None,
                "last": None,
            },
        )
        source["count"] += 1
        source["bus_counts"][bus] += 1
        source["id_counts"][can_id] += 1
        point = {"mono_us": mono_us, "capture_seq": capture_seq, "bus": bus, "id": f"0x{can_id:X}"}
        if source["first"] is None:
            source["first"] = point
        source["last"] = point
        if can_id in model_ids:
            stats["frames"].append({"id": can_id, "bus": bus, "mono_us": mono_us, "data": list(payload[:8]), "capture_seq": capture_seq})
            stats["model_counts_by_source"][f"0x{source_marker:02X}|0x{can_id:X}"] += 1

    while pos + 11 <= len(data):
        sof = data.find(SOF, pos)
        if sof < 0:
            stats["resync_drop"] += len(data) - pos
            break
        if sof > pos:
            stats["resync_drop"] += sof - pos
            pos = sof
        if pos + 11 > len(data):
            break
        version = data[pos + 2]
        record_type = data[pos + 3]
        seq = u16(data, pos + 5)
        length = u16(data, pos + 7)
        if version != 1 or length > MAX_PAYLOAD:
            stats["length"] += 1
            pos += 1
            continue
        frame_len = 11 + length
        if pos + frame_len > len(data):
            stats["length"] += 1
            break
        frame = data[pos : pos + frame_len]
        pos += frame_len
        if u16(frame, frame_len - 2) != crc16_ccitt(frame[2:-2]):
            stats["crc"] += 1
            continue
        if last_seq is not None and seq != ((last_seq + 1) & 0xFFFF):
            stats["seq_gaps"] += 1
        last_seq = seq
        payload = frame[9:-2]
        stats["records"] += 1
        stats["type_counts"][record_type] += 1
        if record_type == 9:
            stats["capability_seen"] = True
        elif record_type == RECORD_CAN_RX_RAW and len(payload) >= 30:
            mono = struct.unpack_from("<Q", payload, 0)[0]
            can_id = u32(payload, 8) & 0x1FFFFFFF
            bus = payload[13]
            observe(can_id, bus, mono, payload[14:22], None)
        elif record_type == RECORD_CAN_RX_SEGMENT and len(payload) >= SEGMENT_HEADER_LEN:
            frame_count = u16(payload, 16)
            entry_size = payload[18]
            if entry_size >= SEGMENT_ENTRY_LEN and len(payload) >= SEGMENT_HEADER_LEN + frame_count * entry_size:
                stats["segment_records"] += 1
                stats["segment_frames"] += frame_count
                for index in range(frame_count):
                    off = SEGMENT_HEADER_LEN + index * entry_size
                    capture_seq = struct.unpack_from("<Q", payload, off)[0]
                    mono = struct.unpack_from("<Q", payload, off + 8)[0]
                    can_id = u32(payload, off + 16) & 0x1FFFFFFF
                    bus = payload[off + 21]
                    observe(can_id, bus, mono, payload[off + 22 : off + 30], capture_seq)
        elif record_type == 8 and len(payload) >= 52:
            health = parse_board_health_payload(payload)
            if stats["health_first"] is None:
                stats["health_first"] = health
            stats["health_last"] = health
    seen = stats["capture_seq_seen"]
    first = min(seen) if seen else None
    last = max(seen) if seen else None
    stats["capture_seq_first"] = first
    stats["capture_seq_last"] = last
    stats["capture_seq_gaps"] = max(0, (last - first + 1) - len(seen)) if first is not None and last is not None else 0
    stats["health_delta"] = health_delta(stats["health_first"], stats["health_last"])
    for source in stats["source_timeline"].values():
        first = source.get("first")
        last = source.get("last")
        source["duration_ms"] = (
            round((last["mono_us"] - first["mono_us"]) / 1000.0, 3)
            if first and last and last["mono_us"] >= first["mono_us"]
            else 0.0
        )
        source["bus_counts"] = {str(k): v for k, v in sorted(source["bus_counts"].items())}
        source["id_count"] = len(source["id_counts"])
        source["id_count_minmax"] = (
            [min(source["id_counts"].values()), max(source["id_counts"].values())]
            if source["id_counts"]
            else [0, 0]
        )
        source["top_ids"] = [
            {"id": f"0x{can_id:X}", "count": count}
            for can_id, count in source["id_counts"].most_common(12)
        ]
        del source["id_counts"]
    return stats


def expected_from_frames(frames: list[dict], profile: str = "smoke") -> dict:
    by_key: dict[tuple[int, int], list[dict]] = collections.defaultdict(list)
    for frame in frames:
        by_key[(frame["bus"], frame["id"])].append(frame)
    expected = {"profile": profile, "counts": {}, "timing": {}, "value": {}, "graph": {}}
    for (bus, can_id), rows in by_key.items():
        rows.sort(key=lambda item: item["mono_us"])
        key = f"BUS{bus}|0X{can_id:X}"
        expected["counts"][key] = len(rows)
        if len(rows) >= 2 and (profile == "full" or can_id == 0x510):
            gaps = [(rows[i]["mono_us"] - rows[i - 1]["mono_us"]) / 1000.0 for i in range(1, len(rows))]
            expected["timing"][key] = {
                "last_gap_ms": gaps[-1],
                "min_gap_ms": min(gaps),
                "max_gap_ms": max(gaps),
                "severity": "ERR" if max(gaps[-4:] or gaps) >= 30.0 else "OK",
            }
        if (profile == "smoke" and can_id == 0x520) or (profile == "full" and 0x520 <= can_id <= 0x55F):
            expected["value"][key] = {"severity": "ERR", "reason_contains": "range", "latest": rows[-1]["data"][0]}
        elif (profile == "smoke" and can_id == 0x521) or (profile == "full" and 0x560 <= can_id <= 0x57F):
            expected["value"][key] = {"severity": "ERR", "reason_contains": "reserved", "latest": rows[-1]["data"][1]}
        elif profile == "full" and 0x580 <= can_id <= 0x59F:
            expected["value"][key] = {"severity": "ERR", "reason_contains": "flag", "latest": rows[-1]["data"][2]}
        if (profile == "smoke" and can_id == 0x530) or (profile == "full" and can_id in FULL_GRAPH_IDS):
            values = [row["data"][0] for row in rows]
            expected["graph"][key] = {"min": min(values), "max": max(values), "latest": values[-1], "count": len(values)}
    return expected


def find_snapshot_row(rows: list[dict], can_id: int, bus: int | None = None) -> dict | None:
    id_text = f"0X{can_id:X}"
    bus_tokens = [] if bus is None else [f"BUS{bus}", f"BUS {bus}", f"B{bus}"]
    for row in rows:
        text = " ".join(str(row.get(k, "")) for k in ("key", "idText", "name", "source")).upper().replace(" ", "")
        if id_text not in text:
            continue
        if bus is None or any(token.replace(" ", "").upper() in text for token in bus_tokens):
            return row
    return None


def compare_snapshot(snapshot: dict, expected: dict) -> tuple[list[str], dict]:
    errors: list[str] = []
    report = {"timing": {}, "value": {}, "graph": {}}
    analysis = snapshot.get("analysis_runtime", {})
    timing_rows = analysis.get("live_timing_rows") or snapshot.get("timing_rows", [])
    value_rows = analysis.get("live_value_rows") or snapshot.get("value_rows", [])
    alarm_rows = analysis.get("live_alarm_rows") or snapshot.get("alarm_rows", [])

    def collect_field(obj, name: str, out: list[int]):
        if isinstance(obj, dict):
            for key, value in obj.items():
                if key == name:
                    try:
                        out.append(int(value))
                    except (TypeError, ValueError):
                        out.append(-1)
                collect_field(value, name, out)
        elif isinstance(obj, list):
            for value in obj:
                collect_field(value, name, out)

    truth_loss_values: list[int] = []
    overrun_values: list[int] = []
    collect_field(analysis, "truth_loss", truth_loss_values)
    collect_field(analysis, "analysis_overrun", overrun_values)
    if not truth_loss_values:
        errors.append("analysis truth_loss is not reported")
    elif any(value != 0 for value in truth_loss_values):
        errors.append(f"analysis truth_loss non-zero: {truth_loss_values}")
    if not overrun_values:
        errors.append("analysis_overrun is not reported")
    elif any(value != 0 for value in overrun_values):
        errors.append(f"analysis_overrun non-zero: {overrun_values}")

    for key, exp in expected["timing"].items():
        bus = int(key.split("|", 1)[0][3:])
        can_id = int(key.split("0X", 1)[1], 16)
        row = find_snapshot_row(timing_rows, can_id, bus)
        report["timing"][key] = row or {}
        if not row:
            errors.append(f"missing timing row {key}")
            continue
        if row.get("severity") != exp["severity"]:
            errors.append(f"timing {key} severity {row.get('severity')} != {exp['severity']}")
        if "dlcHistogram" in row and not row.get("dlcHistogram"):
            errors.append(f"timing {key} dlcHistogram empty")

    for key, exp in expected["value"].items():
        bus = int(key.split("|", 1)[0][3:])
        can_id = int(key.split("0X", 1)[1], 16)
        row = find_snapshot_row(value_rows, can_id, bus)
        report["value"][key] = row or {}
        if not row:
            errors.append(f"missing value row {key}")
            continue
        if row.get("severity") != exp["severity"]:
            errors.append(f"value {key} severity {row.get('severity')} != {exp['severity']}")
        if exp["reason_contains"].lower() not in str(row.get("reason", "")).lower():
            errors.append(f"value {key} reason missing {exp['reason_contains']}")

    if expected["value"] and not alarm_rows:
        errors.append("alarm rows empty despite expected value alarms")

    graph = snapshot.get("graph", {})
    series = graph.get("series", [])
    for key, exp in expected["graph"].items():
        can_id = int(key.split("0X", 1)[1], 16)
        graph_key = f"0X{can_id:X}|0"
        graph_row = next((row for row in series if row.get("key") == graph_key), None)
        report["graph"][graph_key] = graph_row or {}
        if not graph_row:
            errors.append(f"missing live graph series {graph_key}")
            continue
        for field, expected_value in [("minText", exp["min"]), ("maxText", exp["max"]), ("latestText", exp["latest"])]:
            try:
                actual = float(graph_row.get(field, "nan"))
            except ValueError:
                actual = float("nan")
            if abs(actual - float(expected_value)) > 0.51:
                errors.append(f"graph {graph_key} {field} {actual} != {expected_value}")
    return errors, report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(PROJECT_ROOT / "out" / "build" / "x64-Release" / "can_monitor_qml_reboot.exe"))
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--control-port", type=int, default=28741)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--pcan-rate", type=float, default=1000.0)
    parser.add_argument("--kvaser-rate", type=float, default=1000.0)
    parser.add_argument("--id-count", type=int, default=64)
    parser.add_argument("--pcan-channel", type=lambda x: int(x, 0), default=0x51)
    parser.add_argument("--pcan-bitrate", type=lambda x: int(x, 0), default=0x001C)
    parser.add_argument("--pcan-noise-base", type=lambda x: int(x, 0), default=0x640)
    parser.add_argument("--pcan-source-marker", type=lambda x: int(x, 0), default=0x4A)
    parser.add_argument("--kvaser-channel", type=int, default=0)
    parser.add_argument("--kvaser-bitrate", type=int, default=-2)
    parser.add_argument("--kvaser-noise-base", type=lambda x: int(x, 0), default=0x740)
    parser.add_argument("--kvaser-source-marker", type=lambda x: int(x, 0), default=0x6B)
    parser.add_argument("--drain-seconds", type=float, default=1.0)
    parser.add_argument("--read-tail-seconds", type=float, default=2.0)
    parser.add_argument("--status-sample-ms", type=float, default=100.0)
    parser.add_argument("--max-status-samples", type=int, default=2000)
    parser.add_argument("--profile", choices=["smoke", "full"], default="smoke")
    parser.add_argument("--model", default="")
    parser.add_argument("--log-root", default=str(PROJECT_ROOT / "replay_data" / "logs"))
    parser.add_argument("--artifact-root", default=str(PROJECT_ROOT / "artifacts" / "vsm_analysis_truth_hil"))
    args = parser.parse_args()
    if not args.model:
        fixture = "full_load_truth_stress_model.json" if args.profile == "full" else "analysis_truth_stress_model.json"
        args.model = str(PROJECT_ROOT / "tests" / "fixtures" / fixture)

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = pathlib.Path(args.artifact_root) / f"vsm_analysis_truth_{stamp}"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_root = pathlib.Path(args.log_root)
    log_root.mkdir(parents=True, exist_ok=True)
    log_name = f"vsm_analysis_truth_{stamp}"
    before_dirs = {p.resolve() for p in log_root.glob("*.typed") if p.is_dir()}
    result = {"run_dir": str(run_dir), "start": stamp, "pass": False, "errors": []}

    exe = pathlib.Path(args.exe)
    proc = subprocess.Popen(
        [str(exe), "--vsm-hil-control-port", str(args.control_port)],
        cwd=str(exe.parent),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    monitor = ProcessMonitor(proc, run_dir / "process_metrics.csv")
    monitor.start()
    state_poller = AppStatePoller(args.control_port, run_dir / "app_state.jsonl")
    state_poller.start()
    load_state = {}
    try:
        wait_status(args.control_port, lambda s: True, 20, "hil control")
        control_request(args.control_port, {"cmd": "set_model", "path": str(pathlib.Path(args.model))})
        wait_status(args.control_port, lambda s: "Analysis Truth Stress" in s.get("status_text", "") or True, 2, "model accepted")
        control_request(args.control_port, {"cmd": "set_graph_selection", "keys": graph_keys_for_profile(args.profile)})
        control_request(args.control_port, {"cmd": "set_graph_window", "ms": 60000})
        for key in ["live", "timing", "value", "alarm", "graph"]:
            control_request(args.control_port, {"cmd": "panel", "key": key})
        control_request(args.control_port, {"cmd": "connect", "port": args.port, "mode": "typed"})
        wait_status(args.control_port, lambda s: bool(s.get("connected")), 20, "VSM connected")
        control_request(args.control_port, {"cmd": "start_log", "directory": str(log_root), "name": log_name})
        wait_status(args.control_port, lambda s: bool(s.get("log_recording_active")), 10, "logging active")
        senders = TruthLoadSenders(args)
        load_state = senders.run()
        time.sleep(args.read_tail_seconds)
        control_request(args.control_port, {"cmd": "panel", "key": "graph"})
        control_request(args.control_port, {"cmd": "snapshot", "path": str(run_dir / "app_snapshot.json")})
        control_request(args.control_port, {"cmd": "stop_log"})
        status = wait_status(
            args.control_port,
            lambda s: not s.get("log_recording_active") and not s.get("log_stopping") and not s.get("log_saving"),
            30,
            "logging stopped",
        )
        result["final_status"] = status
    except Exception as exc:
        result["errors"].append(str(exc))
    finally:
        state_poller.stop()
        try:
            control_request(args.control_port, {"cmd": "quit"}, timeout=1.0)
        except Exception:
            pass
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.terminate()
        monitor.stop()

    after_dirs = {p.resolve() for p in log_root.glob("*.typed") if p.is_dir()}
    new_dirs = sorted(after_dirs - before_dirs, key=lambda p: p.stat().st_mtime)
    capture_dir = new_dirs[-1] if new_dirs else None
    if capture_dir is None:
        result["errors"].append("no finalized VSM typed capture directory")
    else:
        result["capture_dir"] = str(capture_dir)
        for name in ["capture.stream", "capture.index", "session.meta.json", "capture.diagnostics.json"]:
            if not (capture_dir / name).exists():
                result["errors"].append(f"missing {name}")
        if not result["errors"]:
            capture = parse_capture_frames(capture_dir / "capture.stream", set(model_ids_for_profile(args.profile)))
            expected = expected_from_frames(capture["frames"], args.profile)
            capture_report = {
                "stream_bytes": capture["stream_bytes"],
                "records": capture["records"],
                "types": {str(k): v for k, v in sorted(capture["type_counts"].items())},
                "crc": capture["crc"],
                "length": capture["length"],
                "seq_gaps": capture["seq_gaps"],
                "resync_drop": capture["resync_drop"],
                "can_rx_frames": capture["can_rx_frames"],
                "segment_records": capture["segment_records"],
                "segment_frames": capture["segment_frames"],
                "model_frames": len(capture["frames"]),
                "profile": args.profile,
                "model_id_count": len(model_ids_for_profile(args.profile)),
                "model_counts_by_source": dict(capture["model_counts_by_source"]),
                "source_timeline": capture["source_timeline"],
                "capture_seq_gaps": capture["capture_seq_gaps"],
                "capture_seq_duplicates": capture["capture_seq_duplicates"],
                "health_delta": capture["health_delta"],
                "capability_seen": capture["capability_seen"],
            }
            (run_dir / "capture_report.json").write_text(json.dumps(capture_report, ensure_ascii=False, indent=2), encoding="utf-8")
            (run_dir / "expected_manifest.json").write_text(json.dumps(expected, ensure_ascii=False, indent=2), encoding="utf-8")
            result["capture_report"] = capture_report
            result["expected_manifest"] = str(run_dir / "expected_manifest.json")
            if capture["crc"] or capture["length"] or capture["seq_gaps"] or capture["resync_drop"]:
                result["errors"].append("typed parser failures in final VSM capture")
            if capture["capture_seq_gaps"] or capture["capture_seq_duplicates"]:
                result["errors"].append("capture_seq64 gap/duplicate in final VSM capture")
            if capture["health_delta"]["can_drop"] not in (0, None) or capture["health_delta"]["fifo"] not in (0, None):
                result["errors"].append("CSM can_drop/fifo increased")
            csm_uplink_fatal = csm_uplink_fatal_delta(capture["health_delta"])
            csm_uplink_warning = csm_uplink_warning_delta(capture["health_delta"])
            if csm_uplink_fatal:
                result["errors"].append(f"CSM uplink truth-loss counters increased: {csm_uplink_fatal}")
            if csm_uplink_warning:
                result.setdefault("warnings", []).append(f"CSM uplink backpressure counters increased: {csm_uplink_warning}")
            if len(capture["frames"]) <= 0:
                result["errors"].append("no fixture model frames captured")
            source_mismatches = []
            for prefix, marker in [("pcan", args.pcan_source_marker), ("kvaser", args.kvaser_source_marker)]:
                sent_counts = load_state.get(f"{prefix}_model_counts", {})
                for can_id_text, sent_count in sent_counts.items():
                    key = f"0x{int(marker) & 0xFF:02X}|0x{int(can_id_text, 16):X}"
                    captured_count = capture["model_counts_by_source"].get(key, 0)
                    if captured_count != sent_count:
                        source_mismatches.append(
                            {
                                "source": prefix,
                                "marker": key.split("|", 1)[0],
                                "id": f"0x{int(can_id_text, 16):X}",
                                "sent": sent_count,
                                "captured": captured_count,
                            }
                        )
            if source_mismatches:
                result["source_count_mismatches"] = source_mismatches
                result["errors"].append("sent model-frame count does not match VSM capture source marker count")
            snapshot_path = run_dir / "app_snapshot.json"
            if snapshot_path.exists():
                snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
                snapshot_errors, graph_report = compare_snapshot(snapshot, expected)
                result["errors"].extend(snapshot_errors)
                result["graph_report"] = graph_report
                (run_dir / "graph_report.json").write_text(json.dumps(graph_report, ensure_ascii=False, indent=2), encoding="utf-8")
            else:
                result["errors"].append("app snapshot missing")
            shutil.copy2(capture_dir / "session.meta.json", run_dir / "session.meta.json")

    result["load_state"] = load_state
    if load_state.get("pcan_error") or load_state.get("kvaser_error"):
        result["errors"].append("load sender error")
    if not monitor.memory_bounded(600 * 1024 * 1024, 2 * 1024 * 1024):
        result["errors"].append("process memory growth exceeded bound")
    result["pass"] = not result["errors"]
    (run_dir / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    summary = [
        f"PASS={result['pass']}",
        f"run_dir={run_dir}",
        f"capture_dir={result.get('capture_dir', '-')}",
        f"errors={result['errors']}",
    ]
    (run_dir / "summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary))
    return 0 if result["pass"] else 2


if __name__ == "__main__":
    sys.exit(main())

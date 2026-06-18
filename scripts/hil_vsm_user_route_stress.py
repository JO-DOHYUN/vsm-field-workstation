#!/usr/bin/env python3
"""VSM actual user-route high-load HIL.

This harness launches the VSM app, controls it through the test-only localhost
control channel, starts/stops VSM logging, sends optional PCAN/Kvaser load, and
verifies only the final capture produced by VSM.
"""

from __future__ import annotations

import argparse
import collections
import ctypes
import hashlib
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


SOF = b"\xA5\x5A"
MAX_PAYLOAD = 4096
RECORD_CAN_RX_RAW = 1
RECORD_CAN_RX_SEGMENT = 16
SEGMENT_HEADER_LEN = 32
SEGMENT_ENTRY_LEN = 30
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
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
    "serial_ring_clear_total",
    "serial_ring_cleared_bytes_total",
    "can_segment_enqueue_fail_total",
]
CSM_UPLINK_WARNING_KEYS = [
    "serial_enqueue_fail_total",
    "serial_backpressure_total",
    "mcp_drain_budget_hit_total",
]


if sys.platform.startswith("win"):
    class ProcessMemoryCountersEx(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("PageFaultCount", ctypes.c_ulong),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
            ("PrivateUsage", ctypes.c_size_t),
        ]
else:
    ProcessMemoryCountersEx = None


def windows_process_memory(pid: int) -> tuple[int, int]:
    if not sys.platform.startswith("win") or ProcessMemoryCountersEx is None:
        return (0, 0)
    access = 0x1000 | 0x0010  # PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel32.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.POINTER(ProcessMemoryCountersEx), ctypes.c_ulong]
    psapi.GetProcessMemoryInfo.restype = ctypes.c_int
    handle = kernel32.OpenProcess(access, 0, int(pid))
    if not handle:
        return (0, 0)
    try:
        counters = ProcessMemoryCountersEx()
        counters.cb = ctypes.sizeof(ProcessMemoryCountersEx)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return (0, 0)
        return (int(counters.PrivateUsage), int(counters.WorkingSetSize))
    finally:
        kernel32.CloseHandle(handle)


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def u16(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def u32(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def d32(first: int, last: int) -> int:
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
        "can_drop": d32(first["can_drop"], last["can_drop"]),
        "fifo": d32(first["fifo"], last["fifo"]),
        "has_csm_uplink": bool(first.get("has_csm_uplink") and last.get("has_csm_uplink")),
    }
    if delta["has_csm_uplink"]:
        for key in CSM_UPLINK_COUNTER_KEYS:
            delta[key] = d32(int(first.get(key, 0)), int(last.get(key, 0)))
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


def make_payload(source: int, seq: int, idx: int) -> bytes:
    return struct.pack("<IHBB", seq & 0xFFFFFFFF, (~seq) & 0xFFFF, source, idx & 0xFF)


def decode_payload(data: bytes, source: int, id_count: int) -> int | None:
    if len(data) != 8 or data[6] != source:
        return None
    seq, inv = struct.unpack_from("<IH", data, 0)
    if inv != ((~seq) & 0xFFFF):
        return -1
    if data[7] != (seq % id_count):
        return -1
    return seq


class PcanMsg(ctypes.Structure):
    _fields_ = [
        ("ID", ctypes.c_uint),
        ("MSGTYPE", ctypes.c_ubyte),
        ("LEN", ctypes.c_ubyte),
        ("DATA", ctypes.c_ubyte * 8),
    ]


class CanLoadSenders:
    def __init__(self, args):
        self.args = args
        self.stop_event = threading.Event()
        self.start_event = threading.Event()
        self.lock = threading.Lock()
        self.state = {
            "pcan_sent_ok": 0,
            "pcan_sent_sequences": [],
            "pcan_write_errors": collections.Counter(),
            "pcan_error": None,
            "kvaser_sent_ok": 0,
            "kvaser_sent_sequences": [],
            "kvaser_write_errors": collections.Counter(),
            "kvaser_sync_errors": collections.Counter(),
            "kvaser_error": None,
        }

    def pcan_sender(self):
        try:
            dll = ctypes.WinDLL("PCANBasic.dll")
            dll.CAN_Initialize.argtypes = [ctypes.c_ushort, ctypes.c_ushort, ctypes.c_ubyte, ctypes.c_uint, ctypes.c_ushort]
            dll.CAN_Initialize.restype = ctypes.c_uint
            dll.CAN_Write.argtypes = [ctypes.c_ushort, ctypes.POINTER(PcanMsg)]
            dll.CAN_Write.restype = ctypes.c_uint
            dll.CAN_Uninitialize.argtypes = [ctypes.c_ushort]
            init = dll.CAN_Initialize(self.args.pcan_channel, self.args.pcan_bitrate, 0, 0, 0)
            if init != 0:
                self.state["pcan_error"] = f"CAN_Initialize {hex(init)}"
                return
            try:
                self.start_event.wait()
                period = 1.0 / self.args.pcan_rate
                next_t = time.perf_counter()
                end_t = next_t + self.args.duration
                seq = 0
                while time.perf_counter() < end_t and not self.stop_event.is_set():
                    now = time.perf_counter()
                    if now < next_t:
                        time.sleep(min(0.0002, next_t - now))
                        continue
                    idx = seq % self.args.id_count
                    msg = PcanMsg()
                    msg.ID = self.args.pcan_base_id + idx
                    msg.MSGTYPE = 0
                    msg.LEN = 8
                    data = make_payload(self.args.pcan_source_marker, seq, idx)
                    for i, b in enumerate(data):
                        msg.DATA[i] = b
                    status = dll.CAN_Write(self.args.pcan_channel, ctypes.byref(msg))
                    with self.lock:
                        if status == 0:
                            self.state["pcan_sent_ok"] += 1
                            self.state["pcan_sent_sequences"].append(seq)
                        else:
                            self.state["pcan_write_errors"][hex(status)] += 1
                    seq += 1
                    next_t += period
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
                period = 1.0 / self.args.kvaser_rate
                next_t = time.perf_counter()
                end_t = next_t + self.args.duration
                seq = 0
                while time.perf_counter() < end_t and not self.stop_event.is_set():
                    now = time.perf_counter()
                    if now < next_t:
                        time.sleep(min(0.0002, next_t - now))
                        continue
                    idx = seq % self.args.id_count
                    data = (ctypes.c_ubyte * 8).from_buffer_copy(
                        make_payload(self.args.kvaser_source_marker, seq, idx)
                    )
                    status = dll.canWrite(handle, self.args.kvaser_base_id + idx, ctypes.byref(data), 8, 0)
                    with self.lock:
                        if status == 0:
                            self.state["kvaser_sent_ok"] += 1
                            self.state["kvaser_sent_sequences"].append(seq)
                        else:
                            self.state["kvaser_write_errors"][str(status)] += 1
                    seq += 1
                    next_t += period
                sync = dll.canWriteSync(handle, int(max(1000, self.args.drain_seconds * 1000)))
                if sync != 0:
                    with self.lock:
                        self.state["kvaser_sync_errors"][str(sync)] += 1
            finally:
                time.sleep(self.args.drain_seconds)
                dll.canBusOff(handle)
                dll.canClose(handle)
        except Exception as exc:
            with self.lock:
                self.state["kvaser_error"] = repr(exc)
            self.stop_event.set()

    def run(self):
        threads = []
        if self.args.source in {"both", "pcan"}:
            threads.append(threading.Thread(target=self.pcan_sender))
        if self.args.source in {"both", "kvaser"}:
            threads.append(threading.Thread(target=self.kvaser_sender))
        for thread in threads:
            thread.start()
        self.start_event.set()
        for thread in threads:
            thread.join()
        with self.lock:
            return {
                key: (list(value) if isinstance(value, list) else dict(value) if isinstance(value, collections.Counter) else value)
                for key, value in self.state.items()
            }


class ProcessMonitor:
    def __init__(self, process: subprocess.Popen, path: pathlib.Path):
        self.process = process
        self.path = path
        self.stop_event = threading.Event()
        self.samples = []
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=3)
        with self.path.open("w", encoding="utf-8") as f:
            f.write("t_s,pid,alive,private_bytes,working_set_bytes,cpu_percent\n")
            for row in self.samples:
                f.write(",".join(str(x) for x in row) + "\n")

    def run(self):
        start = time.time()
        psutil_proc = None
        try:
            import psutil  # type: ignore
            psutil_proc = psutil.Process(self.process.pid)
            psutil_proc.cpu_percent(None)
        except Exception:
            psutil_proc = None
        while not self.stop_event.is_set():
            alive = self.process.poll() is None
            private_bytes = 0
            working_set = 0
            cpu = 0.0
            if psutil_proc is not None and alive:
                try:
                    mem = psutil_proc.memory_full_info()
                    private_bytes = int(getattr(mem, "private", getattr(mem, "uss", 0)))
                    working_set = int(psutil_proc.memory_info().rss)
                    cpu = float(psutil_proc.cpu_percent(None))
                except Exception:
                    pass
            elif alive:
                private_bytes, working_set = windows_process_memory(self.process.pid)
            self.samples.append((round(time.time() - start, 3), self.process.pid, int(alive), private_bytes, working_set, round(cpu, 2)))
            time.sleep(1.0)

    def memory_bounded(self, max_growth_bytes: int, max_slope_bytes_per_min: int) -> bool:
        values = [(float(t), int(private)) for t, _, alive, private, _, _ in self.samples if int(alive) and int(private) > 0]
        if len(values) < 3:
            return True
        warmup_s = min(10.0, max(values[-1][0] * 0.25, 0.0))
        stable_values = [value for value in values if value[0] >= warmup_s]
        if len(stable_values) < 3:
            stable_values = values
        if stable_values[-1][1] - stable_values[0][1] > max_growth_bytes:
            return False
        half = stable_values[len(stable_values) // 2 :]
        if len(half) >= 2:
            dt_min = (half[-1][0] - half[0][0]) / 60.0
            if dt_min >= 1.0:
                slope = (half[-1][1] - half[0][1]) / dt_min
                if slope > max_slope_bytes_per_min:
                    return False
        return True


class AppStatePoller:
    def __init__(self, port: int, path: pathlib.Path, interval_s: float = 1.0):
        self.port = port
        self.path = path
        self.interval_s = interval_s
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.start_t = time.time()

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=3)

    def sample(self, label: str):
        row = {
            "t_s": round(time.time() - self.start_t, 3),
            "label": label,
            "ok": False,
        }
        try:
            response = control_request(self.port, {"cmd": "status"}, timeout=1.5)
            row.update({"ok": True, "status": response.get("status", {})})
        except Exception as exc:
            row.update({"error": str(exc)})
        with self.lock:
            with self.path.open("a", encoding="utf-8") as f:
                f.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
        return row

    def run(self):
        while not self.stop_event.is_set():
            self.sample("poll")
            self.stop_event.wait(self.interval_s)


def control_request(port: int, payload: dict, timeout: float = 5.0) -> dict:
    data = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall(data)
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
    return json.loads(buf.decode("utf-8"))


def wait_file_stable(path: pathlib.Path, timeout_s: float = 10.0, stable_s: float = 0.25) -> pathlib.Path:
    deadline = time.time() + timeout_s
    last_size = -1
    stable_since: float | None = None
    while time.time() < deadline:
        if path.exists():
            size = path.stat().st_size
            if size > 0 and size == last_size:
                if stable_since is None:
                    stable_since = time.time()
                if time.time() - stable_since >= stable_s:
                    return path
            else:
                last_size = size
                stable_since = None
        time.sleep(0.05)
    raise TimeoutError(f"file not finalized: {path}")


def wait_tcp_endpoint(host: str, port: int, timeout_s: float, process: subprocess.Popen | None = None) -> None:
    deadline = time.time() + timeout_s
    last_error = ""
    while time.time() < deadline:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"gateway exited before TCP ready: code={process.returncode}")
        try:
            with socket.create_connection((host, port), timeout=0.3):
                return
        except OSError as exc:
            last_error = str(exc)
        time.sleep(0.2)
    raise TimeoutError(f"timeout waiting for tcp://{host}:{port}: {last_error}")


class ControlSmokeDriver:
    def __init__(self, args, path: pathlib.Path):
        self.args = args
        self.path = path
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.errors: list[str] = []
        self.commands_sent = 0

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=10)
        for payload in [
            {"cmd": "control_release_all"},
            {"cmd": "control_neutral"},
            {"cmd": "control_arm", "armed": False},
        ]:
            self.send(payload, "shutdown")

    def send(self, payload: dict, label: str):
        row = {"t": time.time(), "label": label, "payload": payload, "ok": False}
        try:
            response = control_request(self.args.control_port, payload, timeout=3.0)
            row.update({"ok": bool(response.get("ok", False)), "response": response})
            if not row["ok"]:
                self.errors.append(f"{label}: {response.get('error', 'not ok')}")
        except Exception as exc:
            row.update({"error": str(exc)})
            self.errors.append(f"{label}: {exc}")
        with self.path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
        self.commands_sent += 1
        return row

    def wait_control_ready(self, timeout_s: float) -> bool:
        deadline = time.time() + timeout_s
        while time.time() < deadline and not self.stop_event.is_set():
            try:
                status = control_request(self.args.control_port, {"cmd": "status"}, timeout=2.0).get("status", {})
                row = {
                    "t": time.time(),
                    "label": "wait_control_ready",
                    "ok": bool(status.get("control_ready")),
                    "status": status,
                }
                with self.path.open("a", encoding="utf-8") as f:
                    f.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
                if status.get("control_ready"):
                    return True
            except Exception as exc:
                self.errors.append(f"wait_control_ready: {exc}")
            self.stop_event.wait(0.25)
        return False

    def wait_control_armed(self, timeout_s: float) -> bool:
        deadline = time.time() + timeout_s
        while time.time() < deadline and not self.stop_event.is_set():
            try:
                status = control_request(self.args.control_port, {"cmd": "status"}, timeout=2.0).get("status", {})
                if status.get("control_armed"):
                    return True
                if status.get("control_ready"):
                    self.send({"cmd": "control_arm", "armed": True}, "arm_retry")
            except Exception as exc:
                self.errors.append(f"wait_control_armed: {exc}")
            self.stop_event.wait(0.25)
        return False

    def run(self):
        if self.args.control_bus >= 0:
            self.send({"cmd": "control_target", "bus": self.args.control_bus, "rpm": self.args.control_rpm}, "target")
        else:
            self.send({"cmd": "control_target", "rpm": self.args.control_rpm}, "target")
        self.send({"cmd": "panel", "key": "control"}, "panel_control")
        self.wait_control_ready(min(10.0, max(2.0, self.args.duration * 0.4)))
        self.send({"cmd": "control_arm", "armed": True}, "arm")
        self.wait_control_armed(5.0)

        keys = ["w", "d", "w", "a", "s", "x"]
        end_t = time.time() + self.args.duration
        index = 0
        while time.time() < end_t and not self.stop_event.is_set():
            key = keys[index % len(keys)]
            index += 1
            if key == "x":
                self.send({"cmd": "control_neutral"}, "neutral")
                self.stop_event.wait(max(0.05, self.args.control_step_seconds))
                continue
            self.send({"cmd": "control_press", "key": key}, f"press_{key}")
            self.stop_event.wait(max(0.05, self.args.control_step_seconds))
            self.send({"cmd": "control_release", "key": key}, f"release_{key}")
            self.stop_event.wait(0.08)

    def summary(self) -> dict:
        return {
            "enabled": True,
            "commands_sent": self.commands_sent,
            "errors": self.errors,
            "artifact": str(self.path),
        }


def wait_status(port: int, predicate, timeout_s: float, label: str) -> dict:
    end_t = time.time() + timeout_s
    last = {}
    while time.time() < end_t:
        try:
            last = control_request(port, {"cmd": "status"})
            status = last.get("status", {})
            if predicate(status):
                return status
        except Exception as exc:
            last = {"error": str(exc)}
        time.sleep(0.25)
    raise TimeoutError(f"timeout waiting for {label}: {last}")


def parse_capture(path: pathlib.Path, args) -> dict:
    data = path.read_bytes()
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
        "capture_seq_gaps": 0,
        "capture_seq_duplicates": 0,
        "capture_seq_reorders": 0,
        "capture_seq_first": None,
        "capture_seq_last": None,
        "pcan_rx_seqs": set(),
        "pcan_dups": 0,
        "pcan_bad_payload": 0,
        "pcan_wrong_bus": 0,
        "kvaser_rx_seqs": set(),
        "kvaser_dups": 0,
        "kvaser_bad_payload": 0,
        "kvaser_wrong_bus": 0,
        "health_first": None,
        "health_last": None,
        "capability_seen": False,
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    last_seq = None
    last_capture_seq = None
    capture_seq_seen: set[int] = set()

    def note_capture_seq(capture_seq: int | None) -> None:
        nonlocal last_capture_seq
        if capture_seq is None:
            return
        if stats["capture_seq_first"] is None:
            stats["capture_seq_first"] = capture_seq
        if capture_seq in capture_seq_seen:
            stats["capture_seq_duplicates"] += 1
        else:
            capture_seq_seen.add(capture_seq)
        if last_capture_seq is not None and capture_seq < last_capture_seq:
            stats["capture_seq_reorders"] += 1
        last_capture_seq = capture_seq
        stats["capture_seq_last"] = capture_seq

    def observe_can_rx_frame(can_id: int, bus: int, payload_data: bytes, capture_seq: int | None = None) -> None:
        stats["can_rx_frames"] += 1
        note_capture_seq(capture_seq)
        if args.pcan_base_id <= can_id < args.pcan_base_id + args.id_count:
            decoded = decode_payload(payload_data, args.pcan_source_marker, args.id_count)
            if decoded is None:
                return
            if bus != args.pcan_expected_bus:
                stats["pcan_wrong_bus"] += 1
            if decoded < 0:
                stats["pcan_bad_payload"] += 1
            elif decoded in stats["pcan_rx_seqs"]:
                stats["pcan_dups"] += 1
            else:
                stats["pcan_rx_seqs"].add(decoded)
        elif args.kvaser_base_id <= can_id < args.kvaser_base_id + args.id_count:
            decoded = decode_payload(payload_data, args.kvaser_source_marker, args.id_count)
            if decoded is None:
                return
            if bus != args.kvaser_expected_bus:
                stats["kvaser_wrong_bus"] += 1
            if decoded < 0:
                stats["kvaser_bad_payload"] += 1
            elif decoded in stats["kvaser_rx_seqs"]:
                stats["kvaser_dups"] += 1
            else:
                stats["kvaser_rx_seqs"].add(decoded)

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
            can_id = u32(payload, 8) & 0x1FFFFFFF
            bus = payload[13]
            payload_data = payload[14:22]
            observe_can_rx_frame(can_id, bus, payload_data)
        elif record_type == RECORD_CAN_RX_SEGMENT and len(payload) >= SEGMENT_HEADER_LEN:
            frame_count = u16(payload, 16)
            entry_size = payload[18]
            if entry_size >= SEGMENT_ENTRY_LEN and len(payload) >= SEGMENT_HEADER_LEN + frame_count * entry_size:
                stats["segment_records"] += 1
                stats["segment_frames"] += frame_count
                for index in range(frame_count):
                    off = SEGMENT_HEADER_LEN + index * entry_size
                    capture_seq = struct.unpack_from("<Q", payload, off)[0]
                    can_id = u32(payload, off + 16) & 0x1FFFFFFF
                    bus = payload[off + 21]
                    payload_data = payload[off + 22 : off + 30]
                    observe_can_rx_frame(can_id, bus, payload_data, capture_seq)
        elif record_type == 8 and len(payload) >= 52:
            health = parse_board_health_payload(payload)
            if stats["health_first"] is None:
                stats["health_first"] = health
            stats["health_last"] = health
    first = stats["health_first"]
    last = stats["health_last"]
    if capture_seq_seen:
        first_seen = min(capture_seq_seen)
        last_seen = max(capture_seq_seen)
        stats["capture_seq_first"] = first_seen
        stats["capture_seq_last"] = last_seen
        stats["capture_seq_gaps"] = max(0, (last_seen - first_seen + 1) - len(capture_seq_seen))
    stats["health_delta"] = health_delta(first, last)
    return stats


def missing_count(seqs: set[int], sent_ok: int) -> int:
    return sum(1 for seq in range(sent_ok) if seq not in seqs)


def missing_sequence_count(sent_sequences: list[int], rx_sequences: set[int]) -> int:
    return sum(1 for seq in sent_sequences if seq not in rx_sequences)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(PROJECT_ROOT / "out" / "build" / "x64-Release" / "can_monitor_qml_reboot.exe"))
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--control-port", type=int, default=28731)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--no-api-load", action="store_true")
    parser.add_argument("--source", choices=["both", "pcan", "kvaser"], default="both")
    parser.add_argument("--control-smoke", action="store_true")
    parser.add_argument("--control-rpm", type=int, default=800)
    parser.add_argument("--control-bus", type=int, default=-1)
    parser.add_argument("--control-step-seconds", type=float, default=0.35)
    parser.add_argument("--pcan-rate", type=float, default=1500.0)
    parser.add_argument("--kvaser-rate", type=float, default=1500.0)
    parser.add_argument("--id-count", type=int, default=64)
    parser.add_argument("--pcan-channel", type=lambda x: int(x, 0), default=0x51)
    parser.add_argument("--pcan-bitrate", type=lambda x: int(x, 0), default=0x001C)
    parser.add_argument("--pcan-base-id", type=lambda x: int(x, 0), default=0x620)
    parser.add_argument("--pcan-expected-bus", type=int, default=0)
    parser.add_argument("--pcan-source-marker", type=lambda x: int(x, 0), default=-1)
    parser.add_argument("--kvaser-channel", type=int, default=0)
    parser.add_argument("--kvaser-bitrate", type=int, default=-2)
    parser.add_argument("--kvaser-base-id", type=lambda x: int(x, 0), default=0x720)
    parser.add_argument("--kvaser-expected-bus", type=int, default=0)
    parser.add_argument("--kvaser-source-marker", type=lambda x: int(x, 0), default=-1)
    parser.add_argument("--drain-seconds", type=float, default=1.0)
    parser.add_argument("--read-tail-seconds", type=float, default=5.0)
    parser.add_argument("--log-root", default=str(PROJECT_ROOT / "replay_data" / "logs"))
    parser.add_argument("--artifact-root", default=str(PROJECT_ROOT / "artifacts" / "vsm_user_route_hil"))
    parser.add_argument("--debug-gateway", action="store_true")
    parser.add_argument("--strict-source-compare", action="store_true")
    parser.add_argument("--gateway-port", type=int, default=18477)
    parser.add_argument("--gateway-script", default=str(PROJECT_ROOT / "scripts" / "vsm_debug_gateway.py"))
    parser.add_argument("--gateway-baud", type=int, default=921600)
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    if args.pcan_source_marker < 0:
        args.pcan_source_marker = 0x40 | (int(time.time() * 1000) & 0x0F)
    if args.kvaser_source_marker < 0:
        args.kvaser_source_marker = 0x60 | ((int(time.time() * 1000) >> 4) & 0x0F)
    run_dir = (pathlib.Path(args.artifact_root) / f"vsm_user_route_{stamp}").resolve()
    run_dir.mkdir(parents=True, exist_ok=True)
    log_root = pathlib.Path(args.log_root).resolve()
    log_root.mkdir(parents=True, exist_ok=True)
    log_name = f"vsm_user_route_{stamp}"
    before_dirs = {p.resolve() for p in log_root.glob("*.typed") if p.is_dir()}

    gateway_proc = None
    gateway_stdout = None
    gateway_stderr = None
    gateway_dir = None
    gateway_stop_file = None
    connect_port = args.port
    result = {
        "run_dir": str(run_dir),
        "start": stamp,
        "pass": False,
        "errors": [],
        "source_warnings": [],
        "load_markers": {
            "pcan_source_marker": args.pcan_source_marker,
            "kvaser_source_marker": args.kvaser_source_marker,
        },
    }
    if args.debug_gateway:
        try:
            gateway_dir = run_dir / "gateway"
            gateway_dir.mkdir(parents=True, exist_ok=True)
            gateway_stop_file = gateway_dir / "gateway.stop"
            gateway_stop_file.unlink(missing_ok=True)
            gateway_stdout = (run_dir / "gateway_stdout.log").open("w", encoding="utf-8")
            gateway_stderr = (run_dir / "gateway_stderr.log").open("w", encoding="utf-8")
            gateway_proc = subprocess.Popen(
                [
                    sys.executable,
                    str(pathlib.Path(args.gateway_script)),
                    "--port",
                    args.port,
                    "--baud",
                    str(args.gateway_baud),
                    "--listen-host",
                    "127.0.0.1",
                    "--listen-port",
                    str(args.gateway_port),
                    "--out-dir",
                    str(gateway_dir),
                    "--stop-file",
                    str(gateway_stop_file),
                ],
                cwd=str(PROJECT_ROOT),
                stdout=gateway_stdout,
                stderr=gateway_stderr,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            wait_tcp_endpoint("127.0.0.1", args.gateway_port, 20.0, gateway_proc)
            connect_port = f"tcp://127.0.0.1:{args.gateway_port}"
            result["debug_gateway"] = {
                "enabled": True,
                "dir": str(gateway_dir),
                "physical_port": args.port,
                "vsm_endpoint": connect_port,
            }
        except Exception as exc:
            result["errors"].append(f"debug gateway startup failed: {exc}")
            (run_dir / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
            (run_dir / "summary.md").write_text(
                f"PASS=False\nrun_dir={run_dir}\ncapture_dir=-\nerrors={result['errors']}\n",
                encoding="utf-8",
            )
            if gateway_proc is not None:
                gateway_proc.terminate()
                try:
                    gateway_proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    gateway_proc.kill()
            if gateway_stdout is not None:
                gateway_stdout.close()
            if gateway_stderr is not None:
                gateway_stderr.close()
            print((run_dir / "summary.md").read_text(encoding="utf-8"), end="")
            return 2

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
    control_driver = None
    load_state = {}
    try:
        wait_status(args.control_port, lambda s: True, 20, "hil control")
        state_poller.sample("control_ready")
        control_request(args.control_port, {"cmd": "connect", "port": connect_port, "mode": "typed"})
        wait_status(args.control_port, lambda s: bool(s.get("connected")), 20, "VSM connected")
        state_poller.sample("connected")
        control_request(args.control_port, {"cmd": "start_log", "directory": str(log_root), "name": log_name})
        wait_status(args.control_port, lambda s: bool(s.get("log_recording_active")), 10, "VSM logging active")
        state_poller.sample("logging_started")
        if args.control_smoke:
            control_driver = ControlSmokeDriver(args, run_dir / "control_smoke.jsonl")
            control_driver.start()

        if args.no_api_load:
            end_t = time.time() + args.duration
            while time.time() < end_t:
                control_request(args.control_port, {"cmd": "panel", "key": "live"})
                time.sleep(min(2.0, max(0.0, end_t - time.time())))
        else:
            senders = CanLoadSenders(args)
            load_state = senders.run()

        if control_driver is not None:
            control_driver.stop()
        time.sleep(args.read_tail_seconds)
        app_state_path = run_dir / "app_state.json"
        control_request(args.control_port, {"cmd": "snapshot", "path": str(app_state_path)})
        wait_file_stable(app_state_path, timeout_s=15.0)
        control_request(args.control_port, {"cmd": "stop_log"})
        status = wait_status(
            args.control_port,
            lambda s: not s.get("log_recording_active") and not s.get("log_stopping") and not s.get("log_saving"),
            30,
            "VSM logging stopped",
        )
        result["final_status"] = status
        state_poller.sample("logging_stopped")
    except Exception as exc:
        result["errors"].append(str(exc))
    finally:
        if control_driver is not None:
            control_driver.stop()
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
        if gateway_proc is not None:
            if gateway_stop_file is not None:
                gateway_stop_file.write_text("stop\n", encoding="utf-8")
            try:
                gateway_proc.wait(timeout=120)
            except subprocess.TimeoutExpired:
                gateway_proc.terminate()
                try:
                    gateway_proc.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    gateway_proc.kill()
                    gateway_proc.wait(timeout=10)
        if gateway_stdout is not None:
            gateway_stdout.close()
        if gateway_stderr is not None:
            gateway_stderr.close()

    after_dirs = {p.resolve() for p in log_root.glob("*.typed") if p.is_dir()}
    new_dirs = sorted(after_dirs - before_dirs, key=lambda p: p.stat().st_mtime)
    capture_dir = new_dirs[-1] if new_dirs else None
    if capture_dir is None:
        result["errors"].append("no new VSM typed capture directory")
    else:
        result["capture_dir"] = str(capture_dir)
        required = ["capture.stream", "capture.index", "session.meta.json", "capture.diagnostics.json"]
        missing = [name for name in required if not (capture_dir / name).exists()]
        part_files = [str(p) for p in capture_dir.glob("*.part")]
        if missing:
            result["errors"].append(f"missing finalized files: {missing}")
        if part_files:
            result["errors"].append(f"part files remain: {part_files}")
        if not missing:
            stats = parse_capture(capture_dir / "capture.stream", args)
            capture_report = {
                "stream_bytes": stats["stream_bytes"],
                "records": stats["records"],
                "crc": stats["crc"],
                "length": stats["length"],
                "seq_gaps": stats["seq_gaps"],
                "resync_drop": stats["resync_drop"],
                "types": {str(k): v for k, v in sorted(stats["type_counts"].items())},
                "can_rx_frames": stats["can_rx_frames"],
                "segment_records": stats["segment_records"],
                "segment_frames": stats["segment_frames"],
                "capture_seq_gaps": stats["capture_seq_gaps"],
                "capture_seq_duplicates": stats["capture_seq_duplicates"],
                "capture_seq_reorders": stats["capture_seq_reorders"],
                "capture_seq_first": stats["capture_seq_first"],
                "capture_seq_last": stats["capture_seq_last"],
                "health_delta": stats["health_delta"],
                "capability_seen": stats["capability_seen"],
                "sha256": stats["sha256"],
            }
            result["capture_report"] = capture_report
            (run_dir / "capture_report.json").write_text(json.dumps(capture_report, ensure_ascii=False, indent=2), encoding="utf-8")
            app_state_path = run_dir / "app_state.json"
            if app_state_path.exists():
                try:
                    app_state = json.loads(app_state_path.read_text(encoding="utf-8"))
                    live_stats = app_state.get("live_stats", {})
                    counts = app_state.get("counts", {})
                    raw_ledger_rows = int(live_stats.get("raw_ledger_rows", counts.get("raw_ledger_rows", 0)) or 0)
                    raw_ledger_visible_rows = int(live_stats.get("raw_ledger_visible_rows", counts.get("raw_ledger_visible_rows", 0)) or 0)
                    raw_ledger_segment_bytes = int(live_stats.get("raw_ledger_segment_bytes", 0) or 0)
                    capture_can_rx = int(capture_report.get("can_rx_frames", 0) or 0)
                    ledger_report = {
                        "app_state": str(app_state_path),
                        "raw_ledger_rows": raw_ledger_rows,
                        "raw_ledger_visible_rows": raw_ledger_visible_rows,
                        "raw_ledger_segment_bytes": raw_ledger_segment_bytes,
                        "capture_can_rx_records": capture_can_rx,
                        "parity_ok": raw_ledger_rows == capture_can_rx,
                        "truth_preserved": raw_ledger_rows == capture_can_rx and raw_ledger_visible_rows <= raw_ledger_rows,
                    }
                    result["ledger_report"] = ledger_report
                    (run_dir / "ledger_report.json").write_text(json.dumps(ledger_report, ensure_ascii=False, indent=2), encoding="utf-8")
                    if not ledger_report["parity_ok"]:
                        result["errors"].append("raw ledger / capture CAN_RX parity mismatch")
                except Exception as exc:
                    result["errors"].append(f"ledger report failed: {exc}")
            if (capture_dir / "session.meta.json").exists():
                shutil.copy2(capture_dir / "session.meta.json", run_dir / "session.meta.json")
            if (capture_dir / "capture.diagnostics.json").exists():
                shutil.copy2(capture_dir / "capture.diagnostics.json", run_dir / "capture.diagnostics.json")

            if args.debug_gateway and gateway_dir is not None:
                gateway_meta_path = gateway_dir / "gateway.meta.json"
                gateway_stream_path = gateway_dir / "gateway_capture.stream"
                if not gateway_meta_path.exists() or not gateway_stream_path.exists():
                    result["errors"].append("debug gateway artifacts missing")
                else:
                    gateway_meta = json.loads(gateway_meta_path.read_text(encoding="utf-8"))
                    parser_stats = gateway_meta.get("capture", {}).get("parser", {})
                    gateway_report = {
                        "stream": str(gateway_stream_path),
                        "bytes": gateway_meta.get("capture", {}).get("bytes", 0),
                        "sha256": gateway_meta.get("capture", {}).get("sha256", ""),
                        "stats": gateway_meta.get("stats", {}),
                        "parser": parser_stats,
                    }
                    result["debug_gateway"].update({
                        "meta": str(gateway_meta_path),
                        "stream": str(gateway_stream_path),
                        "report": str(run_dir / "gateway_capture_report.json"),
                    })
                    (run_dir / "gateway_capture_report.json").write_text(
                        json.dumps(gateway_report, ensure_ascii=False, indent=2),
                        encoding="utf-8",
                    )
                    for key in ["crc_failures", "length_failures", "resync_drop", "seq_gaps"]:
                        if int(parser_stats.get(key, 0) or 0) != 0:
                            result["errors"].append(f"debug gateway typed parser {key}={parser_stats.get(key)}")
                    if int(gateway_meta.get("stats", {}).get("serial_rx_bytes", 0) or 0) <= 0:
                        result["errors"].append("debug gateway serial_rx_bytes is zero")
                    if int(gateway_meta.get("stats", {}).get("tcp_tx_bytes", 0) or 0) <= 0:
                        result["errors"].append("debug gateway tcp_tx_bytes is zero")
                    if int(gateway_meta.get("stats", {}).get("tcp_forward_errors", 0) or 0) != 0:
                        result["errors"].append("debug gateway tcp forward errors")
                    if int(gateway_meta.get("stats", {}).get("tcp_queue_dropped_bytes", 0) or 0) != 0:
                        result["errors"].append("debug gateway tcp queue dropped bytes")

            pcan_sequences = list(load_state.get("pcan_sent_sequences", []))
            kv_sequences = list(load_state.get("kvaser_sent_sequences", []))
            sent_sequences = {
                "pcan": {
                    "source": "PCAN",
                    "base_id": args.pcan_base_id,
                    "expected_bus": args.pcan_expected_bus,
                    "source_marker": args.pcan_source_marker,
                    "sent_ok": len(pcan_sequences),
                    "sequences": pcan_sequences,
                },
                "kvaser": {
                    "source": "Kvaser",
                    "base_id": args.kvaser_base_id,
                    "expected_bus": args.kvaser_expected_bus,
                    "source_marker": args.kvaser_source_marker,
                    "sent_ok": len(kv_sequences),
                    "sequences": kv_sequences,
                },
            }
            (run_dir / "sent_sequences.json").write_text(json.dumps(sent_sequences, ensure_ascii=False, indent=2), encoding="utf-8")
            pcan_sent = len(pcan_sequences)
            kv_sent = len(kv_sequences)
            result["pcan_compare"] = {
                "sent": pcan_sent,
                "rx_unique": len(stats["pcan_rx_seqs"]),
                "missing": missing_sequence_count(pcan_sequences, stats["pcan_rx_seqs"]),
                "dups": stats["pcan_dups"],
                "bad_payload": stats["pcan_bad_payload"],
                "wrong_bus": stats["pcan_wrong_bus"],
            }
            result["kvaser_compare"] = {
                "sent": kv_sent,
                "rx_unique": len(stats["kvaser_rx_seqs"]),
                "missing": missing_sequence_count(kv_sequences, stats["kvaser_rx_seqs"]),
                "dups": stats["kvaser_dups"],
                "bad_payload": stats["kvaser_bad_payload"],
                "wrong_bus": stats["kvaser_wrong_bus"],
            }
            if stats["crc"] or stats["length"] or stats["seq_gaps"] or stats["resync_drop"]:
                result["errors"].append("typed parser failures in final VSM capture")
            if stats["capture_seq_gaps"]:
                result["errors"].append("capture_seq64 gaps in final VSM capture")
            if not stats["capability_seen"]:
                result["errors"].append("CAPABILITY missing in final VSM capture")
            if stats["health_delta"]["can_drop"] not in (0, None) or stats["health_delta"]["fifo"] not in (0, None):
                result["errors"].append("CSM can_drop/fifo increased")
            csm_uplink_fatal = csm_uplink_fatal_delta(stats["health_delta"])
            csm_uplink_warning = csm_uplink_warning_delta(stats["health_delta"])
            if csm_uplink_fatal:
                result["errors"].append(f"CSM uplink truth-loss counters increased: {csm_uplink_fatal}")
            if csm_uplink_warning:
                result.setdefault("warnings", []).append(f"CSM uplink backpressure counters increased: {csm_uplink_warning}")
            if not args.no_api_load:
                result["load_state"] = {
                    "pcan_sent_ok": pcan_sent,
                    "pcan_write_errors": dict(load_state.get("pcan_write_errors", {})),
                    "pcan_error": load_state.get("pcan_error"),
                    "kvaser_sent_ok": kv_sent,
                    "kvaser_write_errors": dict(load_state.get("kvaser_write_errors", {})),
                    "kvaser_sync_errors": dict(load_state.get("kvaser_sync_errors", {})),
                    "kvaser_error": load_state.get("kvaser_error"),
                }
                for key in ["pcan_compare", "kvaser_compare"]:
                    cmp = result[key]
                    if cmp["rx_unique"] != cmp["sent"] or cmp["missing"] or cmp["dups"] or cmp["bad_payload"] or cmp["wrong_bus"]:
                        message = f"{key} mismatch"
                        if args.strict_source_compare:
                            result["errors"].append(message)
                        else:
                            result["source_warnings"].append(message)
                if load_state.get("pcan_error") or load_state.get("kvaser_error"):
                    result["errors"].append("load sender error")

    if not monitor.memory_bounded(600 * 1024 * 1024, 2 * 1024 * 1024):
        result["errors"].append("process memory growth exceeded bound")
    if control_driver is not None:
        control_summary = control_driver.summary()
        result["control_smoke"] = control_summary
        if control_summary["errors"]:
            result["errors"].append("control smoke command errors")
    result["pass"] = not result["errors"]

    (run_dir / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    summary = [
        f"PASS={result['pass']}",
        f"run_dir={run_dir}",
        f"capture_dir={result.get('capture_dir', '-')}",
        f"errors={result['errors']}",
        f"source_warnings={result.get('source_warnings', [])}",
    ]
    (run_dir / "summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary))
    return 0 if result["pass"] else 2


if __name__ == "__main__":
    sys.exit(main())

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
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]


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


def make_payload(source: int, seq: int, idx: int) -> bytes:
    return struct.pack("<IHBB", seq & 0xFFFFFFFF, (~seq) & 0xFFFF, source, idx & 0xFF)


def decode_payload(data: bytes, source: int, id_count: int) -> int | None:
    if len(data) != 8 or data[6] != source:
        return None
    seq, inv = struct.unpack_from("<IH", data, 0)
    if inv != ((~seq) & 0xFFFF):
        return None
    if data[7] != (seq % id_count):
        return None
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
                    data = make_payload(0x50, seq, idx)
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
                    data = (ctypes.c_ubyte * 8).from_buffer_copy(make_payload(0x4B, seq, idx))
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
        threads = [threading.Thread(target=self.pcan_sender), threading.Thread(target=self.kvaser_sender)]
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
        elif record_type == 1 and len(payload) >= 30:
            can_id = u32(payload, 8) & 0x1FFFFFFF
            bus = payload[13]
            payload_data = payload[14:22]
            if args.pcan_base_id <= can_id < args.pcan_base_id + args.id_count:
                if bus != 0:
                    stats["pcan_wrong_bus"] += 1
                decoded = decode_payload(payload_data, 0x50, args.id_count)
                if decoded is None:
                    stats["pcan_bad_payload"] += 1
                elif decoded in stats["pcan_rx_seqs"]:
                    stats["pcan_dups"] += 1
                else:
                    stats["pcan_rx_seqs"].add(decoded)
            elif args.kvaser_base_id <= can_id < args.kvaser_base_id + args.id_count:
                if bus != 1:
                    stats["kvaser_wrong_bus"] += 1
                decoded = decode_payload(payload_data, 0x4B, args.id_count)
                if decoded is None:
                    stats["kvaser_bad_payload"] += 1
                elif decoded in stats["kvaser_rx_seqs"]:
                    stats["kvaser_dups"] += 1
                else:
                    stats["kvaser_rx_seqs"].add(decoded)
        elif record_type == 8 and len(payload) >= 52:
            health = {"can_rx": u32(payload, 8), "can_drop": u32(payload, 12), "fifo": u32(payload, 16)}
            if stats["health_first"] is None:
                stats["health_first"] = health
            stats["health_last"] = health
    first = stats["health_first"]
    last = stats["health_last"]
    stats["health_delta"] = {
        "can_drop": d32(first["can_drop"], last["can_drop"]) if first and last else None,
        "fifo": d32(first["fifo"], last["fifo"]) if first and last else None,
    }
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
    parser.add_argument("--kvaser-channel", type=int, default=0)
    parser.add_argument("--kvaser-bitrate", type=int, default=-2)
    parser.add_argument("--kvaser-base-id", type=lambda x: int(x, 0), default=0x720)
    parser.add_argument("--drain-seconds", type=float, default=1.0)
    parser.add_argument("--read-tail-seconds", type=float, default=5.0)
    parser.add_argument("--log-root", default=str(PROJECT_ROOT / "replay_data" / "logs"))
    parser.add_argument("--artifact-root", default=str(PROJECT_ROOT / "artifacts" / "vsm_user_route_hil"))
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = pathlib.Path(args.artifact_root) / f"vsm_user_route_{stamp}"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_root = pathlib.Path(args.log_root)
    log_root.mkdir(parents=True, exist_ok=True)
    log_name = f"vsm_user_route_{stamp}"
    before_dirs = {p.resolve() for p in log_root.glob("*.typed") if p.is_dir()}

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
    result = {"run_dir": str(run_dir), "start": stamp, "pass": False, "errors": []}
    try:
        wait_status(args.control_port, lambda s: True, 20, "hil control")
        state_poller.sample("control_ready")
        control_request(args.control_port, {"cmd": "connect", "port": args.port, "mode": "typed"})
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
        control_request(args.control_port, {"cmd": "snapshot", "path": str(run_dir / "app_state.json")})
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

    after_dirs = {p.resolve() for p in log_root.glob("*.typed") if p.is_dir()}
    new_dirs = sorted(after_dirs - before_dirs, key=lambda p: p.stat().st_mtime)
    capture_dir = new_dirs[-1] if new_dirs else None
    if capture_dir is None:
        result["errors"].append("no new VSM typed capture directory")
    else:
        result["capture_dir"] = str(capture_dir)
        required = ["capture.stream", "capture.index", "session.meta.json"]
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
                "health_delta": stats["health_delta"],
                "capability_seen": stats["capability_seen"],
                "sha256": stats["sha256"],
            }
            result["capture_report"] = capture_report
            (run_dir / "capture_report.json").write_text(json.dumps(capture_report, ensure_ascii=False, indent=2), encoding="utf-8")
            if (capture_dir / "session.meta.json").exists():
                shutil.copy2(capture_dir / "session.meta.json", run_dir / "session.meta.json")

            pcan_sequences = list(load_state.get("pcan_sent_sequences", []))
            kv_sequences = list(load_state.get("kvaser_sent_sequences", []))
            sent_sequences = {
                "pcan": {
                    "source": "PCAN",
                    "base_id": args.pcan_base_id,
                    "bus": 0,
                    "sent_ok": len(pcan_sequences),
                    "sequences": pcan_sequences,
                },
                "kvaser": {
                    "source": "Kvaser",
                    "base_id": args.kvaser_base_id,
                    "bus": 1,
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
            if not stats["capability_seen"]:
                result["errors"].append("CAPABILITY missing in final VSM capture")
            if stats["health_delta"]["can_drop"] not in (0, None) or stats["health_delta"]["fifo"] not in (0, None):
                result["errors"].append("CSM can_drop/fifo increased")
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
                        result["errors"].append(f"{key} mismatch")
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
    ]
    (run_dir / "summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary))
    return 0 if result["pass"] else 2


if __name__ == "__main__":
    sys.exit(main())

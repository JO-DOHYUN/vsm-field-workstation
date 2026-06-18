#!/usr/bin/env python3
"""Crash-survivable VSM typed-stream debug gateway.

The gateway owns the physical serial port, writes the raw CSM typed byte stream
to disk first, and forwards the same bytes to VSM over localhost TCP. VSM can
crash or hang without losing the gateway-side evidence up to the last successful
serial read/write flush.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import queue
import signal
import socket
import struct
import sys
import threading
import time
from datetime import datetime, timezone


SOF = b"\xA5\x5A"
MAX_PAYLOAD = 4096
HEADER_AND_CRC = 11


def crc16_ccitt(data: bytes | bytearray | memoryview) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= int(b) << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def u16(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


class EventLog:
    def __init__(self, path: pathlib.Path):
        self.path = path
        self.lock = threading.Lock()

    def write(self, event: str, **fields) -> None:
        row = {"t_utc": now_iso(), "event": event, **fields}
        line = json.dumps(row, ensure_ascii=False, sort_keys=True)
        with self.lock:
            with self.path.open("a", encoding="utf-8") as handle:
                handle.write(line + "\n")


class SegmentWriter:
    def __init__(self, out_dir: pathlib.Path, segment_bytes: int, events: EventLog):
        self.out_dir = out_dir
        self.segment_bytes = max(1024 * 1024, int(segment_bytes))
        self.events = events
        self.stream_part = out_dir / "gateway_capture.stream.part"
        self.stream_final = out_dir / "gateway_capture.stream"
        self.index_path = out_dir / "gateway_capture.index.jsonl"
        self.segments_dir = out_dir / "segments"
        self.segments_dir.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.stream = self.stream_part.open("wb", buffering=0)
        self.segment_index = 0
        self.segment_bytes_written = 0
        self.segment_part: pathlib.Path | None = None
        self.segment_file = None
        self.total_bytes = 0
        self.sha = hashlib.sha256()
        self._open_segment()

    def _open_segment(self) -> None:
        self.segment_part = self.segments_dir / f"segment_{self.segment_index:06d}.stream.part"
        self.segment_file = self.segment_part.open("wb", buffering=0)
        self.segment_bytes_written = 0
        self.events.write("segment_open", segment=str(self.segment_part), index=self.segment_index)

    def _rotate_if_needed(self) -> None:
        if self.segment_bytes_written < self.segment_bytes:
            return
        self.segment_file.close()
        final = self.segment_part.with_suffix("")
        self.segment_part.replace(final)
        self.events.write("segment_close", segment=str(final), index=self.segment_index, bytes=self.segment_bytes_written)
        self.segment_index += 1
        self._open_segment()

    def write(self, data: bytes) -> None:
        if not data:
            return
        with self.lock:
            self.stream.write(data)
            self.segment_file.write(data)
            self.sha.update(data)
            self.total_bytes += len(data)
            self.segment_bytes_written += len(data)
            self._rotate_if_needed()

    def flush(self) -> None:
        with self.lock:
            self.stream.flush()
            self.segment_file.flush()

    def close(self, finalize: bool) -> dict:
        with self.lock:
            self.stream.flush()
            self.segment_file.flush()
            self.stream.close()
            self.segment_file.close()
            if finalize:
                if self.stream_part.exists():
                    self.stream_part.replace(self.stream_final)
                if self.segment_part and self.segment_part.exists():
                    final_segment = self.segment_part.with_suffix("")
                    self.segment_part.replace(final_segment)
                    self.events.write("segment_close",
                                      segment=str(final_segment),
                                      index=self.segment_index,
                                      bytes=self.segment_bytes_written)
            stream_path = self.stream_final if finalize else self.stream_part
        parser_stats = build_index_file(stream_path, self.index_path) if stream_path.exists() else GatewayIndexParser().stats()
        return {
            "stream": str(stream_path),
            "bytes": self.total_bytes,
            "sha256": self.sha.hexdigest(),
            "index": str(self.index_path),
            "segments": self.segment_index + 1,
            "finalized": finalize,
            "parser": parser_stats,
        }


class GatewayIndexParser:
    def __init__(self, index_handle=None):
        self.index_handle = index_handle
        self.buffer = bytearray()
        self.buffer_offset = 0
        self.absolute_offset = 0
        self.records = 0
        self.type_counts: dict[str, int] = {}
        self.crc_failures = 0
        self.length_failures = 0
        self.initial_resync_drop = 0
        self.resync_drop = 0
        self.seq_gaps = 0
        self.last_seq: int | None = None

    def append(self, chunk: bytes, offset: int) -> None:
        if not self.buffer:
            self.absolute_offset = offset
        self.buffer.extend(chunk)
        self._consume()

    def _drop(self, count: int) -> None:
        del self.buffer[:count]
        self.absolute_offset += count

    def _consume(self) -> None:
        while len(self.buffer) >= HEADER_AND_CRC:
            sof = self.buffer.find(SOF)
            if sof < 0:
                drop = max(0, len(self.buffer) - 1)
                if self.records == 0:
                    self.initial_resync_drop += drop
                else:
                    self.resync_drop += drop
                self._drop(drop)
                return
            if sof > 0:
                if self.records == 0:
                    self.initial_resync_drop += sof
                else:
                    self.resync_drop += sof
                self._drop(sof)
            if len(self.buffer) < HEADER_AND_CRC:
                return
            version = self.buffer[2]
            record_type = self.buffer[3]
            seq = u16(self.buffer, 5)
            payload_len = u16(self.buffer, 7)
            if version != 1 or payload_len > MAX_PAYLOAD:
                self.length_failures += 1
                self._drop(1)
                continue
            frame_len = HEADER_AND_CRC + payload_len
            if len(self.buffer) < frame_len:
                return
            frame = bytes(self.buffer[:frame_len])
            frame_offset = self.absolute_offset
            self._drop(frame_len)
            if u16(frame, frame_len - 2) != crc16_ccitt(frame[2:-2]):
                self.crc_failures += 1
                continue
            if self.last_seq is not None and seq != ((self.last_seq + 1) & 0xFFFF):
                self.seq_gaps += 1
            self.last_seq = seq
            self.records += 1
            key = str(record_type)
            self.type_counts[key] = self.type_counts.get(key, 0) + 1
            if self.index_handle is not None:
                self.index_handle.write(json.dumps({
                    "offset": frame_offset,
                    "length": frame_len,
                    "record_type": record_type,
                    "seq": seq,
                    "payload_length": payload_len,
                }, ensure_ascii=False, sort_keys=True) + "\n")

    def stats(self) -> dict:
        return {
            "records": self.records,
            "type_counts": self.type_counts,
            "crc_failures": self.crc_failures,
            "length_failures": self.length_failures,
            "initial_resync_drop": self.initial_resync_drop,
            "resync_drop": self.resync_drop,
            "seq_gaps": self.seq_gaps,
            "buffered_tail_bytes": len(self.buffer),
        }


def build_index_file(stream_path: pathlib.Path, index_path: pathlib.Path) -> dict:
    parser = GatewayIndexParser()
    offset = 0
    with index_path.open("w", encoding="utf-8") as index_handle:
        parser.index_handle = index_handle
        with stream_path.open("rb") as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                parser.append(chunk, offset)
                offset += len(chunk)
    return parser.stats()


class Gateway:
    def __init__(self, args):
        self.args = args
        self.out_dir = pathlib.Path(args.out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        (self.out_dir / "gateway.ready").unlink(missing_ok=True)
        self.events = EventLog(self.out_dir / "events.jsonl")
        self.writer = SegmentWriter(self.out_dir, args.segment_bytes, self.events)
        self.stop_event = threading.Event()
        self.client_lock = threading.Lock()
        self.serial_lock = threading.Lock()
        self.forward_queue: queue.Queue[bytes] = queue.Queue(maxsize=max(1, int(args.tcp_queue_chunks)))
        self.client: socket.socket | None = None
        self.server: socket.socket | None = None
        self.serial = None
        self.stats = {
            "serial_rx_bytes": 0,
            "tcp_tx_bytes": 0,
            "tcp_rx_bytes": 0,
            "serial_tx_bytes": 0,
            "tcp_clients": 0,
            "tcp_disconnects": 0,
            "tcp_forward_errors": 0,
            "tcp_queue_enqueued_chunks": 0,
            "tcp_queue_enqueued_bytes": 0,
            "tcp_queue_dropped_chunks": 0,
            "tcp_queue_dropped_bytes": 0,
            "tcp_queue_max_chunks": 0,
            "tcp_no_client_dropped_chunks": 0,
            "tcp_no_client_dropped_bytes": 0,
            "serial_read_errors": 0,
            "serial_write_errors": 0,
            "flushes": 0,
            "started_utc": now_iso(),
            "ended_utc": None,
        }

    def _open_serial(self):
        try:
            import serial  # type: ignore
        except Exception as exc:
            raise RuntimeError("pyserial is required: py -3 -m pip install pyserial") from exc
        self.serial = serial.Serial(self.args.port,
                                    baudrate=self.args.baud,
                                    timeout=self.args.serial_timeout,
                                    write_timeout=self.args.serial_write_timeout)
        self.events.write("serial_open", port=self.args.port, baud=self.args.baud)

    def _drop_forward_queue(self, reason: str) -> None:
        dropped_chunks = 0
        dropped_bytes = 0
        while True:
            try:
                data = self.forward_queue.get_nowait()
            except queue.Empty:
                break
            dropped_chunks += 1
            dropped_bytes += len(data)
        if dropped_chunks:
            self.stats["tcp_queue_dropped_chunks"] += dropped_chunks
            self.stats["tcp_queue_dropped_bytes"] += dropped_bytes
            self.events.write("tcp_queue_drop", reason=reason, chunks=dropped_chunks, bytes=dropped_bytes)

    def _enqueue_tcp_forward(self, data: bytes) -> None:
        with self.client_lock:
            has_client = self.client is not None
        if not has_client:
            self.stats["tcp_no_client_dropped_chunks"] += 1
            self.stats["tcp_no_client_dropped_bytes"] += len(data)
            return
        try:
            self.forward_queue.put_nowait(data)
            self.stats["tcp_queue_enqueued_chunks"] += 1
            self.stats["tcp_queue_enqueued_bytes"] += len(data)
            self.stats["tcp_queue_max_chunks"] = max(self.stats["tcp_queue_max_chunks"], self.forward_queue.qsize())
        except queue.Full:
            self.stats["tcp_queue_dropped_chunks"] += 1
            self.stats["tcp_queue_dropped_bytes"] += len(data)
            self.events.write("tcp_queue_drop", reason="forward_queue_full", chunks=1, bytes=len(data))

    def _listen(self):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((self.args.listen_host, self.args.listen_port))
        self.server.listen(1)
        self.server.settimeout(0.2)
        self.events.write("tcp_listen", host=self.args.listen_host, port=self.args.listen_port)
        ready = {
            "host": self.args.listen_host,
            "port": self.args.listen_port,
            "serial_port": self.args.port,
            "out_dir": str(self.out_dir),
            "started_utc": self.stats["started_utc"],
        }
        (self.out_dir / "gateway.ready").write_text(json.dumps(ready, ensure_ascii=False, indent=2), encoding="utf-8")
        print("READY " + json.dumps(ready, ensure_ascii=False), flush=True)
        while not self.stop_event.is_set():
            try:
                client, address = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            client.settimeout(0.05)
            client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with self.client_lock:
                old = self.client
                self.client = client
                if old is not None:
                    try:
                        old.close()
                    except OSError:
                        pass
            self._drop_forward_queue("new_client")
            self.stats["tcp_clients"] += 1
            self.events.write("tcp_client_connected", peer=str(address))
            threading.Thread(target=self._tcp_to_serial, args=(client,), daemon=True).start()

    def _tcp_to_serial(self, client: socket.socket):
        while not self.stop_event.is_set():
            try:
                data = client.recv(65536)
                if not data:
                    break
            except socket.timeout:
                continue
            except OSError as exc:
                self.events.write("tcp_recv_error", error=repr(exc))
                break
            self.stats["tcp_rx_bytes"] += len(data)
            try:
                with self.serial_lock:
                    written = self.serial.write(data)
                self.stats["serial_tx_bytes"] += int(written or 0)
            except Exception as exc:
                self.stats["serial_write_errors"] += 1
                self.events.write("serial_write_error", error=repr(exc), bytes=len(data))
                break
        with self.client_lock:
            if self.client is client:
                self.client = None
        self.stats["tcp_disconnects"] += 1
        try:
            client.close()
        except OSError:
            pass
        self.events.write("tcp_client_disconnected")

    def _tcp_forward(self):
        while not self.stop_event.is_set():
            try:
                data = self.forward_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            with self.client_lock:
                client = self.client
            if client is None:
                self.stats["tcp_no_client_dropped_chunks"] += 1
                self.stats["tcp_no_client_dropped_bytes"] += len(data)
                continue
            try:
                client.sendall(data)
                self.stats["tcp_tx_bytes"] += len(data)
            except OSError as exc:
                self.stats["tcp_forward_errors"] += 1
                self.events.write("tcp_forward_error", error=repr(exc), bytes=len(data))
                with self.client_lock:
                    if self.client is client:
                        self.client = None
                try:
                    client.close()
                except OSError:
                    pass
                self._drop_forward_queue("forward_error")

    def _serial_to_tcp_and_disk(self):
        last_flush = time.monotonic()
        while not self.stop_event.is_set():
            try:
                data = self.serial.read(self.args.read_size)
            except Exception as exc:
                self.stats["serial_read_errors"] += 1
                self.events.write("serial_read_error", error=repr(exc))
                self.stop_event.wait(0.05)
                continue
            if data:
                self.stats["serial_rx_bytes"] += len(data)
                self.writer.write(data)
                self._enqueue_tcp_forward(data)
            now = time.monotonic()
            if now - last_flush >= self.args.flush_interval:
                self.writer.flush()
                self.stats["flushes"] += 1
                last_flush = now

    def run(self) -> int:
        finalize = False
        try:
            self.events.write("gateway_start", args=vars(self.args))
            self._open_serial()
            server_thread = threading.Thread(target=self._listen, daemon=True)
            serial_thread = threading.Thread(target=self._serial_to_tcp_and_disk, daemon=True)
            tcp_forward_thread = threading.Thread(target=self._tcp_forward, daemon=True)
            server_thread.start()
            serial_thread.start()
            tcp_forward_thread.start()
            deadline = time.monotonic() + self.args.duration if self.args.duration > 0 else None
            stop_file = pathlib.Path(self.args.stop_file) if self.args.stop_file else None
            while not self.stop_event.is_set():
                if stop_file is not None and stop_file.exists():
                    self.events.write("stop_file_seen", path=str(stop_file))
                    break
                if deadline is not None and time.monotonic() >= deadline:
                    break
                time.sleep(0.1)
            finalize = True
            return 0
        finally:
            self.stop_event.set()
            if self.server is not None:
                try:
                    self.server.close()
                except OSError:
                    pass
            with self.client_lock:
                client = self.client
                self.client = None
            if client is not None:
                try:
                    client.close()
                except OSError:
                    pass
            if self.serial is not None:
                try:
                    self.serial.close()
                except Exception:
                    pass
            capture = self.writer.close(finalize)
            self.stats["ended_utc"] = now_iso()
            result = {"ok": finalize, "stats": self.stats, "capture": capture}
            (self.out_dir / "gateway.meta.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
            (self.out_dir / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
            (self.out_dir / "summary.md").write_text(
                "\n".join([
                    "# VSM Debug Gateway Summary",
                    "",
                    f"- ok: {finalize}",
                    f"- serial port: `{self.args.port}`",
                    f"- tcp endpoint: `tcp://{self.args.listen_host}:{self.args.listen_port}`",
                    f"- serial_rx_bytes: {self.stats['serial_rx_bytes']}",
                    f"- tcp_tx_bytes: {self.stats['tcp_tx_bytes']}",
                    f"- tcp_rx_bytes: {self.stats['tcp_rx_bytes']}",
                    f"- serial_tx_bytes: {self.stats['serial_tx_bytes']}",
                    f"- tcp_queue_dropped_bytes: {self.stats['tcp_queue_dropped_bytes']}",
                    f"- tcp_queue_max_chunks: {self.stats['tcp_queue_max_chunks']}",
                    f"- parser: `{capture['parser']}`",
                    f"- stream: `{capture['stream']}`",
                ]) + "\n",
                encoding="utf-8",
            )
            self.events.write("gateway_stop", ok=finalize, stats=self.stats, capture=capture)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    default_dir = pathlib.Path("artifacts") / "vsm_debug_gateway" / datetime.now().strftime("%Y%m%d_%H%M%S")
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=18477)
    parser.add_argument("--out-dir", default=str(default_dir))
    parser.add_argument("--duration", type=float, default=0.0, help="0 means run until interrupted")
    parser.add_argument("--read-size", type=int, default=65536)
    parser.add_argument("--tcp-queue-chunks", type=int, default=256)
    parser.add_argument("--segment-bytes", type=int, default=256 * 1024 * 1024)
    parser.add_argument("--flush-interval", type=float, default=0.5)
    parser.add_argument("--serial-timeout", type=float, default=0.05)
    parser.add_argument("--serial-write-timeout", type=float, default=0.5)
    parser.add_argument("--stop-file", default="")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    gateway = Gateway(args)

    def stop_handler(signum, _frame):
        gateway.events.write("signal", signum=int(signum))
        gateway.stop_event.set()

    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, stop_handler)
    if hasattr(signal, "SIGINT"):
        signal.signal(signal.SIGINT, stop_handler)
    return gateway.run()


if __name__ == "__main__":
    raise SystemExit(main())

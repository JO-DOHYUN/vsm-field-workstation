#!/usr/bin/env python3
"""PCAN/Kvaser CAN load generator used by the in-app VSM verifier.

This script never opens VSM or COM7. It only sends deterministic CAN traffic
through PCAN/Kvaser APIs and writes sender-side artifacts.
"""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys
import threading
import time
from datetime import datetime

from hil_analysis_truth_stress import MODEL_IDS, TruthLoadSenders, model_ids_for_profile
from hil_vsm_user_route_stress import CanLoadSenders


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1] if pathlib.Path(__file__).resolve().parent.name == "scripts" else pathlib.Path(__file__).resolve().parent


def write_json(path: pathlib.Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def normalize_state(state: dict) -> dict:
    out = {}
    for key, value in state.items():
        if isinstance(value, list):
            out[key] = list(value)
        elif isinstance(value, collections.Counter):
            out[key] = dict(value)
        else:
            out[key] = value
    return out


def run_selected_sender_threads(sender, source: str) -> dict:
    targets = []
    if source in {"both", "pcan"}:
        targets.append(sender.pcan_sender)
    if source in {"both", "kvaser"}:
        targets.append(sender.kvaser_sender)
    threads = [threading.Thread(target=target, daemon=True) for target in targets]
    for thread in threads:
        thread.start()
    time.sleep(0.2)
    sender.start_event.set()
    for thread in threads:
        thread.join(timeout=max(10.0, float(sender.args.duration) + float(sender.args.drain_seconds) + 10.0))
    sender.stop_event.set()
    with sender.lock:
        return normalize_state(sender.state)


def run(args: argparse.Namespace) -> int:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = pathlib.Path(args.out_dir) if args.out_dir else PROJECT_ROOT / "artifacts" / "vsm_verify" / f"can_load_{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"ARTIFACT_DIR={out_dir}", flush=True)

    if args.dry_run:
        result = {
            "pass": True,
            "dry_run": True,
            "mode": args.mode,
            "duration": args.duration,
            "pcan_rate": args.pcan_rate,
            "kvaser_rate": args.kvaser_rate,
            "id_count": args.id_count,
            "source": args.source,
        }
        write_json(out_dir / "result.json", result)
        print("RESULT=PASS dry-run", flush=True)
        return 0

    if args.mode in {"analysis_truth", "full_truth"}:
        args.profile = "full" if args.mode == "full_truth" else "smoke"
        sender = TruthLoadSenders(args)
        load_state = run_selected_sender_threads(sender, args.source)
        model_ids = model_ids_for_profile(args.profile)
        expected = {
            "mode": args.mode,
            "profile": args.profile,
            "source": args.source,
            "model_ids": [f"0x{can_id:X}" for can_id in model_ids],
            "model_id_count": len(model_ids),
            "fixture": "full_load_truth_stress_model" if args.profile == "full" else "analysis_truth_stress_model",
            "pcan_model_counts": load_state.get("pcan_model_counts", {}),
            "kvaser_model_counts": load_state.get("kvaser_model_counts", {}),
            "expected_stress": {
                "timing": "all model IDs use 20ms period rules and should produce heavy timing updates at high id-count/rate",
                "value": "0x520-0x55F range high, 0x560-0x57F reserved bit, 0x580-0x59F flag active",
                "graph": "0x5A0-0x5AF peak/min/max preservation",
                "noise": "non-model IDs are mixed and must not pollute model alarms",
            },
        }
        write_json(out_dir / "expected_sender_manifest.json", expected)
    else:
        sender = CanLoadSenders(args)
        load_state = run_selected_sender_threads(sender, args.source)
        sent_sequences = {
            "pcan": {
                "base_id": args.pcan_base_id,
                "expected_bus": args.pcan_expected_bus,
                "source_marker": args.pcan_source_marker,
                "sent_ok": len(load_state.get("pcan_sent_sequences", [])),
                "sequences": load_state.get("pcan_sent_sequences", []),
            },
            "kvaser": {
                "base_id": args.kvaser_base_id,
                "expected_bus": args.kvaser_expected_bus,
                "source_marker": args.kvaser_source_marker,
                "sent_ok": len(load_state.get("kvaser_sent_sequences", [])),
                "sequences": load_state.get("kvaser_sent_sequences", []),
            },
        }
        write_json(out_dir / "sent_sequences.json", sent_sequences)

    errors: list[str] = []
    active_prefixes = ("pcan", "kvaser") if args.source == "both" else (args.source,)
    for key in (f"{prefix}_error" for prefix in active_prefixes):
        if load_state.get(key):
            errors.append(f"{key}: {load_state.get(key)}")
    for key in (f"{prefix}_write_errors" for prefix in active_prefixes):
        values = load_state.get(key) or {}
        if values:
            errors.append(f"{key}: {values}")
    if args.source in {"both", "kvaser"}:
        values = load_state.get("kvaser_sync_errors") or {}
        if values:
            errors.append(f"kvaser_sync_errors: {values}")

    result = {
        "pass": not errors,
        "mode": args.mode,
        "source": args.source,
        "duration": args.duration,
        "pcan_rate": args.pcan_rate,
        "kvaser_rate": args.kvaser_rate,
        "id_count": args.id_count,
        "load_state": load_state,
        "errors": errors,
    }
    write_json(out_dir / "result.json", result)
    summary = [
        f"PASS={result['pass']}",
        f"mode={args.mode}",
        f"out_dir={out_dir}",
        f"errors={errors}",
    ]
    (out_dir / "summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print(f"RESULT={'PASS' if result['pass'] else 'FAIL'}", flush=True)
    return 0 if result["pass"] else 2


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Send deterministic PCAN/Kvaser load without launching VSM")
    parser.add_argument("--mode", choices=["raw", "analysis_truth", "full_truth"], default="raw")
    parser.add_argument("--source", choices=["both", "pcan", "kvaser"], default="both")
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--pcan-rate", type=float, default=1000.0)
    parser.add_argument("--kvaser-rate", type=float, default=1000.0)
    parser.add_argument("--id-count", type=int, default=64)
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--dry-run", action="store_true")

    parser.add_argument("--pcan-channel", type=lambda x: int(x, 0), default=0x51)
    parser.add_argument("--pcan-bitrate", type=lambda x: int(x, 0), default=0x001C)
    parser.add_argument("--pcan-base-id", type=lambda x: int(x, 0), default=0x620)
    parser.add_argument("--pcan-expected-bus", type=int, default=0)
    parser.add_argument("--pcan-source-marker", type=lambda x: int(x, 0), default=0x4A)
    parser.add_argument("--pcan-noise-base", type=lambda x: int(x, 0), default=0x640)

    parser.add_argument("--kvaser-channel", type=int, default=0)
    parser.add_argument("--kvaser-bitrate", type=int, default=-2)
    parser.add_argument("--kvaser-base-id", type=lambda x: int(x, 0), default=0x720)
    parser.add_argument("--kvaser-expected-bus", type=int, default=0)
    parser.add_argument("--kvaser-source-marker", type=lambda x: int(x, 0), default=0x6B)
    parser.add_argument("--kvaser-noise-base", type=lambda x: int(x, 0), default=0x740)

    parser.add_argument("--drain-seconds", type=float, default=1.0)
    args = parser.parse_args(argv)
    try:
        return run(args)
    except Exception as exc:
        print(f"RESULT=FAIL exception={exc!r}", file=sys.stderr, flush=True)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

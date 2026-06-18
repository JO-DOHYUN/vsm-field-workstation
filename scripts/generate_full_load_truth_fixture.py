#!/usr/bin/env python3
"""Generate the full-load truth stress model pack.

The fixture intentionally puts timing, value, reserved-bit, flag, and graph
signals under load at the same time. It is deterministic and does not touch
hardware.
"""

from __future__ import annotations

import argparse
import json
import pathlib


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUT = PROJECT_ROOT / "tests" / "fixtures" / "full_load_truth_stress_model.json"


def hex_id(value: int) -> str:
    return f"0x{value:X}"


def base_rule(can_id: int, name: str) -> dict:
    return {
        "id": hex_id(can_id),
        "name_en": name,
        "expected_period_ms": 20.0,
        "ttl_warn_ms": 80.0,
        "ttl_err_ms": 150.0,
        "period_err_warn_pct": 20.0,
        "period_err_err_pct": 50.0,
    }


def byte_signal(name: str, byte_index: int) -> dict:
    return {
        "name": name,
        "byte_index_1based": byte_index,
        "bit_text": "8..1",
        "length_bits": 8,
        "start_bit_lsb": 0,
        "bit_positions_lsb": [],
        "scale": 1.0,
        "offset": 0.0,
        "signed": False,
        "range_text": "0 to 255",
        "operating_text": "unit: 1",
        "description": "full-load truth stress signal",
        "reserved": False,
        "unit": "",
        "alarm_mode": "none",
    }


def make_message(can_id: int, group: str) -> dict | None:
    if group == "timing":
        return None
    if group == "range":
        sig = byte_signal("Range Fault Byte", 1)
        sig.update(
            {
                "unit": "C",
                "range_text": "0 to 60",
                "alarm_mode": "range",
                "warn_max": 50.0,
                "err_max": 60.0,
                "alarm_severity": "ERR",
                "alarm_message": "Full-load range high",
            }
        )
    elif group == "reserved":
        sig = byte_signal("Reserved Fault Bit", 2)
        sig.update(
            {
                "bit_text": "1",
                "length_bits": 1,
                "range_text": "0 to 1",
                "reserved": True,
                "alarm_mode": "reserved",
                "alarm_severity": "ERR",
                "alarm_message": "Full-load reserved bit set",
            }
        )
    elif group == "flag":
        sig = byte_signal("Fault Flag", 3)
        sig.update(
            {
                "bit_text": "1",
                "length_bits": 1,
                "range_text": "0 to 1",
                "operating_text": "0: OK, 1: Fault",
                "alarm_mode": "flag",
                "alarm_severity": "ERR",
                "alarm_message": "Full-load fault flag active",
            }
        )
    elif group == "graph":
        sig = byte_signal("Graph Peak Byte", 1)
        sig.update(
            {
                "unit": "raw",
                "range_text": "0 to 255",
                "description": "full-load graph peak/min/max preservation",
            }
        )
    else:
        return None
    return {"id": hex_id(can_id), "name": f"Full Load {group.title()} 0x{can_id:X}", "signals": [sig]}


def group_for_id(can_id: int) -> str:
    if 0x510 <= can_id <= 0x51F:
        return "timing"
    if 0x520 <= can_id <= 0x55F:
        return "range"
    if 0x560 <= can_id <= 0x57F:
        return "reserved"
    if 0x580 <= can_id <= 0x59F:
        return "flag"
    return "graph"


def build_fixture() -> dict:
    ids = list(range(0x510, 0x5B0))
    rules = [base_rule(can_id, f"FULL_LOAD_{group_for_id(can_id).upper()}_{can_id:X}") for can_id in ids]
    messages = []
    for can_id in ids:
        msg = make_message(can_id, group_for_id(can_id))
        if msg:
            messages.append(msg)
    return {
        "schema": "can-monitor-model-pack.v1",
        "model_key": "full_load_truth_stress",
        "model_name": "Full Load Truth Stress Fixture",
        "model_version": "2026-06-16",
        "vendor": "Codex",
        "rules": rules,
        "messages": messages,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    fixture = build_fixture()
    text = json.dumps(fixture, ensure_ascii=False, indent=2) + "\n"
    if args.check:
        if not out.exists() or out.read_text(encoding="utf-8") != text:
            print(f"fixture out of date: {out}")
            return 1
        print(f"fixture ok: {out}")
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

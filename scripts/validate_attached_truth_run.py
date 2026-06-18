#!/usr/bin/env python3
"""Validate an in-app attached truth/full-load run.

The app starts/stops its own log while an external sender runs. This validator
checks the resulting VSM capture and app snapshot against the sender manifest.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

from hil_analysis_truth_stress import (
    compare_snapshot,
    csm_uplink_fatal_delta,
    csm_uplink_warning_delta,
    expected_from_frames,
    model_ids_for_profile,
    parse_capture_frames,
)


def load_json(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def find_sender_manifest(run_dir: pathlib.Path) -> pathlib.Path | None:
    matches = sorted(run_dir.rglob("expected_sender_manifest.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    return matches[0] if matches else None


def compare_sender_counts(capture: dict, manifest: dict, profile: str) -> list[dict]:
    mismatches: list[dict] = []
    marker_by_source = {"pcan": 0x4A, "kvaser": 0x6B}
    for source, marker in marker_by_source.items():
        counts = manifest.get(f"{source}_model_counts") or {}
        for can_id_text, sent in counts.items():
            can_id = int(can_id_text, 16)
            if can_id not in set(model_ids_for_profile(profile)):
                continue
            key = f"0x{marker:02X}|0x{can_id:X}"
            captured = int(capture["model_counts_by_source"].get(key, 0))
            if captured != int(sent):
                mismatches.append({"source": source, "id": f"0x{can_id:X}", "sent": int(sent), "captured": captured})
    return mismatches


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--capture-dir", required=True)
    parser.add_argument("--snapshot", required=True)
    parser.add_argument("--profile", choices=["smoke", "full"], default="smoke")
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    run_dir = pathlib.Path(args.run_dir)
    capture_dir = pathlib.Path(args.capture_dir)
    snapshot_path = pathlib.Path(args.snapshot)
    out_path = pathlib.Path(args.out) if args.out else run_dir / "truth_validation_result.json"
    errors: list[str] = []
    csm_uplink_warning: dict = {}

    stream_path = capture_dir / "capture.stream"
    diagnostics_path = capture_dir / "capture.diagnostics.json"
    if not stream_path.exists():
        errors.append(f"missing capture.stream: {stream_path}")
        capture = None
    else:
        if not diagnostics_path.exists():
            errors.append(f"missing capture.diagnostics.json: {diagnostics_path}")
        capture = parse_capture_frames(stream_path, set(model_ids_for_profile(args.profile)))
        if capture["crc"] or capture["length"] or capture["seq_gaps"] or capture["resync_drop"]:
            errors.append(
                f"typed parser failures: crc={capture['crc']} length={capture['length']} "
                f"seq_gaps={capture['seq_gaps']} resync_drop={capture['resync_drop']}"
            )
        if capture["capture_seq_gaps"] or capture["capture_seq_duplicates"]:
            errors.append(
                f"capture_seq continuity failure: gaps={capture['capture_seq_gaps']} "
                f"duplicates={capture['capture_seq_duplicates']}"
            )
        health = capture.get("health_delta") or {}
        if health.get("can_drop") not in (0, None) or health.get("fifo") not in (0, None):
            errors.append(f"CSM drop/fifo increased: {health}")
        csm_uplink_fatal = csm_uplink_fatal_delta(health)
        csm_uplink_warning = csm_uplink_warning_delta(health)
        if csm_uplink_fatal:
            errors.append(f"CSM uplink truth-loss counters increased: {csm_uplink_fatal}")
        if not capture.get("capability_seen"):
            errors.append("CAPABILITY not observed in capture")
        if len(capture.get("frames") or []) <= 0:
            errors.append("no fixture model frames captured")

    manifest_path = find_sender_manifest(run_dir)
    manifest = load_json(manifest_path) if manifest_path else {}
    if not manifest_path:
        errors.append("sender expected manifest not found")

    snapshot = load_json(snapshot_path) if snapshot_path.exists() else {}
    if not snapshot:
        errors.append(f"snapshot missing: {snapshot_path}")

    expected = {}
    graph_report = {}
    source_mismatches: list[dict] = []
    if capture is not None:
        expected = expected_from_frames(capture["frames"], args.profile)
        if snapshot:
            snapshot_errors, graph_report = compare_snapshot(snapshot, expected)
            errors.extend(snapshot_errors)
        if manifest:
            source_mismatches = compare_sender_counts(capture, manifest, args.profile)
            if source_mismatches:
                errors.append("sent model-frame count does not match VSM capture source marker count")

    result = {
        "pass": not errors,
        "profile": args.profile,
        "run_dir": str(run_dir),
        "capture_dir": str(capture_dir),
        "snapshot": str(snapshot_path),
        "sender_manifest": str(manifest_path) if manifest_path else "",
        "errors": errors,
        "warnings": [f"CSM uplink backpressure counters increased: {csm_uplink_warning}"] if csm_uplink_warning else [],
        "source_count_mismatches": source_mismatches,
        "capture_report": {
            "records": capture.get("records") if capture else None,
            "can_rx_frames": capture.get("can_rx_frames") if capture else None,
            "model_frames": len(capture.get("frames") or []) if capture else None,
            "seq_gaps": capture.get("seq_gaps") if capture else None,
            "capture_seq_gaps": capture.get("capture_seq_gaps") if capture else None,
            "health_delta": capture.get("health_delta") if capture else None,
        },
        "expected_counts": len(expected.get("counts", {})),
        "expected_timing": len(expected.get("timing", {})),
        "expected_value": len(expected.get("value", {})),
        "expected_graph": len(expected.get("graph", {})),
        "graph_report": graph_report,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS={result['pass']}")
    print(f"RESULT={out_path}")
    if errors:
        print("ERRORS=" + "; ".join(errors[:8]))
    return 0 if result["pass"] else 2


if __name__ == "__main__":
    raise SystemExit(main())

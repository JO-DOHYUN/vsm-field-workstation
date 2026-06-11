#!/usr/bin/env python3
"""Create a small release manifest for field portable folders."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_entry(path: Path, base: Path) -> dict:
    return {
        "path": path.relative_to(base).as_posix(),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def read_project_version(project_dir: Path) -> str:
    cmake = project_dir / "CMakeLists.txt"
    if not cmake.exists():
        return "unknown"
    text = cmake.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"project\([^)]*VERSION\s+([0-9A-Za-z_.-]+)", text)
    return match.group(1) if match else "unknown"


def infer_package_date(package_dir: Path) -> str:
    match = re.search(r"(20\d{6})", package_dir.name)
    if match:
        return match.group(1)
    return datetime.now(timezone.utc).strftime("%Y%m%d")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", required=True)
    parser.add_argument("--project-dir", required=True)
    parser.add_argument("--build-dir", default="")
    parser.add_argument("--baseline-id", default="workspace-direct-edit")
    parser.add_argument("--output-name", default="RELEASE_MANIFEST.json")
    args = parser.parse_args()

    package_dir = Path(args.package_dir).resolve()
    project_dir = Path(args.project_dir).resolve()
    build_dir = Path(args.build_dir).resolve() if args.build_dir else None
    package_date = infer_package_date(package_dir)

    artifacts: list[dict] = []
    for rel in [
        "can_monitor_qml_reboot.exe",
        "data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json",
        "packaging/SBOM.spdx.json",
        "packaging/THIRD_PARTY_NOTICES.txt",
    ]:
        path = package_dir / rel
        if path.exists():
            artifacts.append(file_entry(path, package_dir))

    helpers = []
    for rel in [
        "run_release_here.bat",
        "hil_control_smoke.py",
        "analyze_typed_capture.py",
        "field_latest_capture_report.py",
    ]:
        if (package_dir / rel).exists():
            helpers.append(rel)

    manifest = {
        "schema": "can-monitor-field-release-manifest.v1",
        "generated_at_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "package_id": f"field-rc-{package_date}-{args.baseline_id}",
        "application": {
            "name": "CAN Monitor Reboot",
            "version": read_project_version(project_dir),
            "baseline_id": args.baseline_id,
            "build_type": "Release",
        },
        "paths": {
            "package_dir": str(package_dir),
            "project_dir": str(project_dir),
            "build_dir": str(build_dir) if build_dir else "",
        },
        "protocol": {
            "name": "typed_stream_v1",
            "frame_version": 1,
            "source": "shared/protocol/typed_stream_v1.md",
        },
        "artifacts": artifacts,
        "helpers": helpers,
        "known_issues": [
            "Fresh HIL/vehicle validation is still required before claiming bus0/bus1 field stability.",
            "Actual vehicle control success must be confirmed only by matching CAN_TX_RAW.",
            "Android wireless transport is not implemented in current CSM firmware.",
            "WiX MSI packaging remains a hook/stub; portable folder is the field artifact.",
            "Graph slider/selection has QML probes but still needs real live/replay manual field smoke.",
        ],
        "unverified_until_field_run": [
            "vehicle bus0/bus1 hot-plug stability",
            "vehicle control TX success",
            "Android USB-OTG or wireless CSM transport feasibility",
        ],
    }

    output = package_dir / args.output_name
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

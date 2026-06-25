#!/usr/bin/env python3
"""Official VSM verification runner catalog.

This wrapper keeps field/HIL/debug entrypoints inside the VSM project folder
and standardizes artifact layout. It does not replace the underlying HIL
scripts; it only launches them as external processes and writes a stable
result envelope.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import time
from datetime import datetime


SCRIPT_PATH = pathlib.Path(__file__).resolve()
SCRIPT_DIR = SCRIPT_PATH.parent


def infer_project_root() -> pathlib.Path:
    if SCRIPT_DIR.name.lower() == "scripts":
        return SCRIPT_DIR.parent
    return SCRIPT_DIR


PROJECT_ROOT = infer_project_root()


SCENARIOS: dict[str, dict] = {
    "pcan_mcp_load_30s": {
        "title": "PCAN to MCP CAN load only 1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "raw",
            "--source", "pcan",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--id-count", "64",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "pcan_mcp_load",
    },
    "analysis_pcan_mcp_load_30s": {
        "title": "PCAN to MCP analysis fixture load only 1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "analysis_truth",
            "--source", "pcan",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--id-count", "64",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "analysis_pcan_mcp_load",
    },
    "kvaser_load_30s": {
        "title": "Kvaser CAN load only 1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "raw",
            "--source", "kvaser",
            "--duration", "30",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "kvaser_load",
    },
    "analysis_kvaser_load_30s": {
        "title": "Kvaser analysis fixture load only 1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "analysis_truth",
            "--source", "kvaser",
            "--duration", "30",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "analysis_kvaser_load",
    },
    "full_pcan_mcp_load_30s": {
        "title": "PCAN to MCP full truth load only 1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "full_truth",
            "--source", "pcan",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--id-count", "384",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "full_pcan_mcp_load",
    },
    "full_kvaser_load_30s": {
        "title": "Kvaser full truth load only 1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "full_truth",
            "--source", "kvaser",
            "--duration", "30",
            "--kvaser-rate", "1000",
            "--id-count", "384",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "full_kvaser_load",
    },
    "full_pcan_mcp_load_60s": {
        "title": "PCAN to MCP full truth load only 2000fps 60s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "full_truth",
            "--source", "pcan",
            "--duration", "60",
            "--pcan-rate", "2000",
            "--id-count", "384",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "full_pcan_mcp_load_60s",
    },
    "can_load_30s": {
        "title": "PCAN/Kvaser CAN load only 1000+1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "raw",
            "--source", "both",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "can_load",
    },
    "analysis_can_load_30s": {
        "title": "PCAN/Kvaser analysis fixture load only 1000+1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "analysis_truth",
            "--source", "both",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "analysis_can_load",
    },
    "full_can_load_30s": {
        "title": "PCAN/Kvaser full truth load only 1000+1000fps 30s",
        "script": "vsm_can_load.py",
        "needs_port": False,
        "args": [
            "--mode", "full_truth",
            "--source", "both",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--kvaser-rate", "1000",
            "--id-count", "384",
        ],
        "out_arg": "--out-dir",
        "artifact_subdir": "full_can_load",
    },
    "user_route_30s": {
        "title": "Actual VSM user route 1000+1000fps 30s",
        "script": "hil_vsm_user_route_stress.py",
        "needs_port": True,
        "args": [
            "--duration", "30",
            "--pcan-rate", "1000",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "artifact_arg": "--artifact-root",
        "artifact_subdir": "user_route_hil",
    },
    "user_route_kvaser_30s": {
        "title": "Actual VSM user route Kvaser 1000fps 30s",
        "script": "hil_vsm_user_route_stress.py",
        "needs_port": True,
        "args": [
            "--source", "kvaser",
            "--duration", "30",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "artifact_arg": "--artifact-root",
        "artifact_subdir": "user_route_kvaser_hil",
    },
    "analysis_truth_30s": {
        "title": "Analysis truth timing/value/alarm/graph 1000+1000fps 30s",
        "script": "hil_analysis_truth_stress.py",
        "needs_port": True,
        "args": [
            "--duration", "30",
            "--pcan-rate", "1000",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "artifact_arg": "--artifact-root",
        "artifact_subdir": "analysis_truth_hil",
    },
    "analysis_truth_kvaser_30s": {
        "title": "Analysis truth timing/value/alarm/graph Kvaser 1000fps 30s",
        "script": "hil_analysis_truth_stress.py",
        "needs_port": True,
        "args": [
            "--source", "kvaser",
            "--duration", "30",
            "--kvaser-rate", "1000",
            "--id-count", "64",
        ],
        "artifact_arg": "--artifact-root",
        "artifact_subdir": "analysis_truth_kvaser_hil",
    },
    "full_analysis_truth_30s": {
        "title": "Full analysis truth timing/value/alarm/graph 1000+1000fps 30s",
        "script": "hil_analysis_truth_stress.py",
        "needs_port": True,
        "args": [
            "--profile", "full",
            "--duration", "30",
            "--pcan-rate", "1000",
            "--kvaser-rate", "1000",
            "--id-count", "384",
        ],
        "artifact_arg": "--artifact-root",
        "artifact_subdir": "full_analysis_truth_hil",
    },
    "full_analysis_truth_kvaser_30s": {
        "title": "Full analysis truth timing/value/alarm/graph Kvaser 1000fps 30s",
        "script": "hil_analysis_truth_stress.py",
        "needs_port": True,
        "args": [
            "--profile", "full",
            "--source", "kvaser",
            "--duration", "30",
            "--kvaser-rate", "1000",
            "--id-count", "384",
        ],
        "artifact_arg": "--artifact-root",
        "artifact_subdir": "full_analysis_truth_kvaser_hil",
    },
    "control_smoke": {
        "title": "Direct CSM control evidence smoke",
        "script": "hil_control_smoke.py",
        "needs_port": True,
        "args": ["--duration-s", "6", "--report-periods"],
        "json_arg": "--json-out",
        "artifact_subdir": "control_smoke",
    },
    "debug_gateway": {
        "title": "External raw serial debug gateway 30s",
        "script": "vsm_debug_gateway.py",
        "needs_port": True,
        "args": ["--duration", "30"],
        "out_arg": "--out-dir",
        "artifact_subdir": "debug_gateway",
    },
    "latest_capture_report": {
        "title": "Latest project-local typed capture report",
        "script": "field_latest_capture_report.py",
        "needs_port": False,
        "args": ["--root", str(PROJECT_ROOT / "replay_data" / "logs")],
        "artifact_subdir": "latest_capture_report",
    },
}


def scenario_command(args: argparse.Namespace, run_dir: pathlib.Path) -> list[str]:
    scenario = SCENARIOS[args.scenario]
    script = find_helper_script(scenario["script"])
    cmd = [sys.executable, str(script)]
    if scenario.get("needs_port"):
        if not args.port:
            raise SystemExit(f"scenario requires --port: {args.scenario}")
        cmd += ["--port", args.port]
    if args.app_exe and scenario["script"] in {"hil_vsm_user_route_stress.py", "hil_analysis_truth_stress.py"}:
        cmd += ["--exe", args.app_exe]
    scenario_args = list(scenario.get("args", []))
    if args.duration_override:
        for index in range(len(scenario_args) - 1):
            if scenario_args[index] in {"--duration", "--duration-s"}:
                scenario_args[index + 1] = str(args.duration_override)
                break
        else:
            scenario_args += ["--duration", str(args.duration_override)]
    cmd += scenario_args
    scenario_artifact = run_dir / scenario["artifact_subdir"]
    if scenario.get("artifact_arg"):
        cmd += [scenario["artifact_arg"], str(scenario_artifact)]
    if scenario.get("json_arg"):
        scenario_artifact.mkdir(parents=True, exist_ok=True)
        cmd += [scenario["json_arg"], str(scenario_artifact / "control_smoke.json")]
    if scenario.get("out_arg"):
        scenario_artifact.mkdir(parents=True, exist_ok=True)
        cmd += [scenario["out_arg"], str(scenario_artifact)]
    return cmd


def find_helper_script(name: str) -> pathlib.Path:
    candidates = [
        PROJECT_ROOT / "scripts" / name,
        PROJECT_ROOT / name,
        SCRIPT_DIR / name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def default_app_exe() -> str:
    candidates = [
        PROJECT_ROOT / "can_monitor_qml_reboot.exe",
        PROJECT_ROOT / "out" / "build" / "x64-Release" / "can_monitor_qml_reboot.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    return str(candidates[-1])


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    if args.scenario not in SCENARIOS:
        raise SystemExit(f"unknown scenario: {args.scenario}")
    run_id = f"{args.scenario}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    out_root = pathlib.Path(args.out_root) if args.out_root else PROJECT_ROOT / "artifacts" / "vsm_verify"
    run_dir = out_root / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    print(f"ARTIFACT_DIR={run_dir}", flush=True)

    cmd = scenario_command(args, run_dir)
    envelope = {
        "runner": "vsm_verify",
        "run_id": run_id,
        "scenario": args.scenario,
        "title": SCENARIOS[args.scenario]["title"],
        "started_at": datetime.now().isoformat(timespec="seconds"),
        "artifact_dir": str(run_dir),
        "command": cmd,
        "dry_run": bool(args.dry_run),
    }
    if args.dry_run:
        envelope.update({"pass": True, "exit_code": 0, "finished_at": datetime.now().isoformat(timespec="seconds")})
        write_text(run_dir / "result.json", json.dumps(envelope, ensure_ascii=False, indent=2))
        write_text(run_dir / "summary.md", f"# VSM Verify Dry Run\n\n- scenario: {args.scenario}\n- command: `{' '.join(cmd)}`\n")
        print("RESULT=PASS dry-run", flush=True)
        return 0

    start = time.monotonic()
    proc = subprocess.run(cmd, cwd=PROJECT_ROOT, text=True, capture_output=True)
    elapsed = time.monotonic() - start
    write_text(run_dir / "stdout.log", proc.stdout)
    write_text(run_dir / "stderr.log", proc.stderr)
    passed = proc.returncode == 0
    envelope.update(
        {
            "pass": passed,
            "exit_code": proc.returncode,
            "elapsed_s": elapsed,
            "finished_at": datetime.now().isoformat(timespec="seconds"),
            "stdout_log": str(run_dir / "stdout.log"),
            "stderr_log": str(run_dir / "stderr.log"),
        }
    )
    write_text(run_dir / "result.json", json.dumps(envelope, ensure_ascii=False, indent=2))
    write_text(
        run_dir / "summary.md",
        "\n".join(
            [
                "# VSM Verify Result",
                "",
                f"- scenario: {args.scenario}",
                f"- pass: {passed}",
                f"- exit_code: {proc.returncode}",
                f"- elapsed_s: {elapsed:.2f}",
                f"- stdout: {run_dir / 'stdout.log'}",
                f"- stderr: {run_dir / 'stderr.log'}",
                "",
            ]
        ),
    )
    print(f"RESULT={'PASS' if passed else 'FAIL'} exit={proc.returncode}", flush=True)
    return proc.returncode


def list_scenarios() -> int:
    rows = []
    for key, spec in SCENARIOS.items():
        rows.append({"key": key, "title": spec["title"], "needs_port": bool(spec.get("needs_port"))})
    print(json.dumps(rows, ensure_ascii=False, indent=2))
    return 0


def status(args: argparse.Namespace) -> int:
    out_root = pathlib.Path(args.out_root) if args.out_root else PROJECT_ROOT / "artifacts" / "vsm_verify"
    results = sorted(out_root.glob("*/result.json"), key=lambda path: path.stat().st_mtime, reverse=True)
    if not results:
        print("NO_RESULTS")
        return 1
    latest = results[0]
    print(latest.read_text(encoding="utf-8"))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="VSM official verification runner")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")

    run_parser = sub.add_parser("run")
    run_parser.add_argument("--scenario", required=True, choices=sorted(SCENARIOS))
    run_parser.add_argument("--port", default="")
    run_parser.add_argument("--app-exe", default=default_app_exe())
    run_parser.add_argument("--out-root", default=str(PROJECT_ROOT / "artifacts" / "vsm_verify"))
    run_parser.add_argument("--duration-override", type=float, default=0.0)
    run_parser.add_argument("--dry-run", action="store_true")

    status_parser = sub.add_parser("status")
    status_parser.add_argument("--out-root", default=str(PROJECT_ROOT / "artifacts" / "vsm_verify"))

    args = parser.parse_args(argv)
    if args.command == "list":
        return list_scenarios()
    if args.command == "run":
        return run(args)
    if args.command == "status":
        return status(args)
    raise SystemExit(f"unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())

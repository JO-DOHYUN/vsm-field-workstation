#!/usr/bin/env python3
"""Scan VSM source for live-path ownership boundary debt.

This is intentionally a lightweight static scan. It does not prove runtime
correctness; it prevents the same high-risk ownership shortcuts from being
reintroduced silently.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]


SKIP_DIRS = {
    ".git",
    ".vs",
    "build",
    "out",
    "replay_data",
    "artifacts",
    "__pycache__",
}


SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".qml", ".md"}


@dataclass(frozen=True)
class Rule:
    rule_id: str
    description: str
    pattern: re.Pattern[str]
    final_forbidden: bool
    path_hints: tuple[str, ...] = ()
    allow_hints: tuple[str, ...] = ()


@dataclass
class Finding:
    rule_id: str
    description: str
    path: str
    line: int
    text: str
    final_forbidden: bool


RULES = (
    Rule(
        "typed_record_list_live_fanout",
        "TypedRecordList must not be the shared live fanout object.",
        re.compile(r"\bTypedRecordList\b|typedRecordsReceived|rawTypedRecordsReceived"),
        True,
        path_hints=("src/backend/AppController", "src/backend/SerialWorker", "src/backend/transport"),
        allow_hints=(
            "TypedRecords.h",
            "TypedReplayReader.cpp",
            "TypedTransportParser.cpp",
            "TypedRecordHandoffQueue",
            "TypedCaptureWriter",
            "StorageRuntime",
        ),
    ),
    Rule(
        "storage_frame_bytes_live_path",
        "storage frameBytes must not flow through live display/projection paths.",
        re.compile(r"\bframeBytes\b"),
        True,
        path_hints=("src/backend/AppController", "src/backend/SerialWorker", "src/backend/RawFrameTableModel"),
        allow_hints=(
            "TypedRecords.h",
            "TypedReplayReader.cpp",
            "TypedTransportParser.cpp",
            "StorageRuntime.cpp",
            "TypedRecordHandoffQueue",
            "TypedCaptureWriter",
        ),
    ),
    Rule(
        "diagnostics_payload_state_owner",
        "AppController must not become transport state owner from diagnostics payloads.",
        re.compile(r"applyCoreTransportSummaryPayload"),
        True,
        path_hints=("src/backend/AppController",),
    ),
    Rule(
        "legacy_serialworker_live_route",
        "SerialWorker direct live routes must disappear from final core-process production.",
        re.compile(
            r"&SerialWorker::(framesReceived|rawFramesReceived|truthFramesReceived)"
            r"|emit\s+(framesReceived|rawFramesReceived|truthFramesReceived)\s*\("
            r"|void\s+(framesReceived|rawFramesReceived|truthFramesReceived)\s*\("
            r"|queue(Projected|Truth|Analysis)Frames\s*\("
            r"|void\s+SerialWorker::queue(Projected|Truth|Analysis)Frames"
            r"|void\s+queue(Projected|Truth|Analysis)Frames"
        ),
        True,
        path_hints=("src/backend/AppController", "src/backend/SerialWorker"),
    ),
    Rule(
        "full_snapshot_push_to_ui",
        "High-rate UI push should be ViewChanged plus bounded GetView, not full snapshot streaming.",
        re.compile(r"projectedFramesReady|canRxFramesReady|truthFramesReady"),
        True,
        path_hints=("src/backend/transport", "src/backend/SerialWorker", "src/backend/core"),
        allow_hints=("CoreView", "ViewChanged"),
    ),
    Rule(
        "passive_serial_policy_bypass",
        "Production serial open must not hard-code read/write mode or DTR/RTS assertion outside RuntimeProfile policy.",
        re.compile(r"open\s*\(\s*QIODevice::ReadWrite\s*\)|setDataTerminalReady\s*\(\s*true\s*\)|setRequestToSend\s*\(\s*true\s*\)"),
        True,
        path_hints=("src/backend",),
        allow_hints=("RuntimeProfile.cpp",),
    ),
)


def iter_files() -> Iterable[Path]:
    for path in ROOT.rglob("*"):
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def is_candidate(rule: Rule, path: Path) -> bool:
    rel = path.as_posix()
    if rule.path_hints and not any(hint in rel for hint in rule.path_hints):
        return False
    if rule.allow_hints and any(hint in rel for hint in rule.allow_hints):
        return False
    return True


def scan() -> list[Finding]:
    findings: list[Finding] = []
    for path in iter_files():
        rel = path.relative_to(ROOT).as_posix()
        for rule in RULES:
            if not is_candidate(rule, path):
                continue
            try:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                continue
            for idx, line in enumerate(lines, start=1):
                if rule.pattern.search(line):
                    stripped = line.strip()
                    if not stripped or stripped.startswith("//"):
                        continue
                    findings.append(
                        Finding(
                            rule_id=rule.rule_id,
                            description=rule.description,
                            path=rel,
                            line=idx,
                            text=stripped[:220],
                            final_forbidden=rule.final_forbidden,
                        )
                    )
    return findings


def print_text(findings: list[Finding], mode: str) -> None:
    by_rule: dict[str, list[Finding]] = {}
    for finding in findings:
        by_rule.setdefault(finding.rule_id, []).append(finding)

    print(f"boundary_scan mode={mode} findings={len(findings)}")
    for rule_id in sorted(by_rule):
        group = by_rule[rule_id]
        print(f"\n[{rule_id}] {len(group)}")
        for finding in group[:25]:
            print(f"  {finding.path}:{finding.line}: {finding.text}")
        if len(group) > 25:
            print(f"  ... {len(group) - 25} more")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("transition", "strict"), default="transition")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()

    findings = scan()
    if args.format == "json":
        print(json.dumps([asdict(f) for f in findings], ensure_ascii=False, indent=2))
    else:
        print_text(findings, args.mode)

    if args.mode == "strict" and any(f.final_forbidden for f in findings):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

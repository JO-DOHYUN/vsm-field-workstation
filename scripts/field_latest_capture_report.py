#!/usr/bin/env python3
"""Find and summarize the newest project-local typed capture session."""

from __future__ import annotations

import argparse
import pathlib
import sys

from analyze_typed_capture import parse_capture, print_report


def find_latest_session(root: pathlib.Path) -> pathlib.Path | None:
    candidates: list[pathlib.Path] = []
    for stream in root.rglob("capture.stream"):
        if stream.parent.suffix == ".typed":
            candidates.append(stream.parent)
    if not candidates:
        return None
    return max(candidates, key=lambda path: (path / "capture.stream").stat().st_mtime)


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize the latest replay_data typed capture")
    parser.add_argument("--root", default="replay_data/logs", help="project-local log root")
    parser.add_argument("--top", type=int, default=24)
    args = parser.parse_args()

    root = pathlib.Path(args.root)
    if not root.exists():
        print(f"FAIL: log root does not exist: {root}", file=sys.stderr)
        return 2

    session = find_latest_session(root)
    if session is None:
        print(f"FAIL: no typed capture.stream found under {root}", file=sys.stderr)
        return 3

    print(f"latest_session={session}")
    print_report(parse_capture(session), args.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Audit and optionally recycle stale RTT build artifacts left in source folders.

The RTT SCons flow should keep compiler outputs under build/.  Old bring-up
runs occasionally left .o/.d/.cmd files next to sources, which makes searches
and workspace reviews noisy.  This tool never touches git-tracked files.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path


DEFAULT_EXCLUDES = {
    ".git",
    "archive",
    "build",
    "modules",
    "results",
}

ARTIFACT_SUFFIXES = {
    ".d",
    ".o",
}

ARTIFACT_NAMES = {
    ".sconsign.dblite",
}


def git_tracked(root: Path, relpath: str) -> bool:
    return subprocess.run(
        ["git", "ls-files", "--error-unmatch", relpath],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode == 0


def is_candidate(path: Path) -> bool:
    return path.name in ARTIFACT_NAMES or path.suffix in ARTIFACT_SUFFIXES


def iter_candidates(root: Path, excludes: set[str]):
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = Path(dirpath).relative_to(root)
        if rel_dir == Path("."):
            rel_parts = ()
        else:
            rel_parts = rel_dir.parts

        dirnames[:] = [
            d for d in dirnames
            if d not in excludes and (not rel_parts or rel_parts[0] not in excludes)
        ]
        if rel_parts and rel_parts[0] in excludes:
            continue

        for filename in filenames:
            path = Path(dirpath) / filename
            if is_candidate(path):
                yield path


def artifact_record(root: Path, path: Path, tracked: bool) -> dict:
    st = path.stat()
    return {
        "path": path.relative_to(root).as_posix(),
        "size": st.st_size,
        "mtime_utc": datetime.fromtimestamp(st.st_mtime, timezone.utc).isoformat(),
        "tracked": tracked,
    }


def recycle_file(root: Path, path: Path, recycle_root: Path) -> str:
    rel = path.relative_to(root)
    dest = recycle_root / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(path), str(dest))
    return dest.relative_to(root).as_posix()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=".",
        help="repository root, default: current directory",
    )
    parser.add_argument(
        "--recycle",
        action="store_true",
        help="move untracked artifacts into archive/recycle/<stamp>",
    )
    parser.add_argument(
        "--out",
        help="write JSON report to this path",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="additional top-level directory to skip; can be repeated",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    excludes = set(DEFAULT_EXCLUDES)
    excludes.update(args.exclude)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    recycle_root = root / "archive" / "recycle" / f"rtt_source_artifacts_{stamp}"

    records = []
    moved = []
    for path in sorted(iter_candidates(root, excludes)):
        relpath = path.relative_to(root).as_posix()
        tracked = git_tracked(root, relpath)
        record = artifact_record(root, path, tracked)
        if args.recycle and not tracked:
            record["recycled_to"] = recycle_file(root, path, recycle_root)
            moved.append(record)
        records.append(record)

    summary = {
        "root": str(root),
        "timestamp_utc": stamp,
        "mode": "recycle" if args.recycle else "audit",
        "excluded_top_level": sorted(excludes),
        "candidate_count": len(records),
        "tracked_count": sum(1 for r in records if r["tracked"]),
        "untracked_count": sum(1 for r in records if not r["tracked"]),
        "moved_count": len(moved),
        "recycle_root": recycle_root.relative_to(root).as_posix() if moved else None,
        "artifacts": records,
    }

    output = json.dumps(summary, indent=2, sort_keys=True)
    if args.out:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(output + "\n", encoding="utf-8")
    else:
        print(output)

    return 1 if summary["untracked_count"] and not args.recycle else 0


if __name__ == "__main__":
    raise SystemExit(main())

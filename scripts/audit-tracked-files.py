#!/usr/bin/env python3
"""Read-only tracked-file hygiene inventory; never deletes repository content."""
import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import sys

GENERATED_SUFFIXES = {".o", ".a", ".so", ".elf", ".iso", ".img", ".pyc", ".log", ".tmp", ".bak", ".orig", ".rej", ".swp"}
GENERATED_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}
GENERATED_DIRS = {"build", ".cache", "__pycache__", ".pytest_cache", ".mypy_cache"}

def git_files(root):
    output = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z", "--cached"])
    return [pathlib.Path(x.decode("utf-8", "surrogateescape"))
            for x in output.split(b"\0") if x]

def inventory(root):
    tracked = git_files(root)
    suspicious = []
    missing = []
    duplicates = collections.defaultdict(list)
    suffixes = collections.Counter()
    total_bytes = 0
    for relative in tracked:
        absolute = root / relative
        if not absolute.is_file():
            missing.append(relative.as_posix())
            continue
        size = absolute.stat().st_size
        total_bytes += size
        suffixes[relative.suffix.lower() or "(none)"] += 1
        if (relative.name in GENERATED_NAMES or relative.suffix.lower() in GENERATED_SUFFIXES
                or GENERATED_DIRS.intersection(relative.parts)):
            suspicious.append({"path": relative.as_posix(), "reason": "generated/cache pattern", "bytes": size})
        digest = hashlib.sha256()
        with absolute.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        duplicates[(size, digest.hexdigest())].append(relative.as_posix())
    return {
        "tracked_count": len(tracked),
        "present_bytes": total_bytes,
        "missing_from_worktree": sorted(missing),
        "candidate_patterns_review_only": sorted(suspicious, key=lambda x: x["path"]),
        "byte_identical_groups_review_only": [
            {"bytes_each": size, "sha256": digest, "paths": sorted(paths)}
            for (size, digest), paths in sorted(duplicates.items())
            if len(paths) > 1
        ],
        "extensions": dict(sorted(suffixes.items())),
        "warning": "Candidates are NOT proven unused. Check build/runtime/test references before deletion.",
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=pathlib.Path, help="Optional JSON report path")
    args = parser.parse_args()
    root = args.root.resolve()
    if not (root / ".git").exists():
        parser.error("root must be a local Git checkout")
    report = json.dumps(inventory(root), indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    else:
        sys.stdout.write(report)

if __name__ == "__main__":
    main()

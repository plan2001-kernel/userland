#!/usr/bin/env python3
"""Fetch pinned *source* dependencies into sibling directories (never vendor them)."""
import argparse
import json
import re
import subprocess
from pathlib import Path

EXPECTED = {"kernel": "plan2001-kernel/kernel", "busybox": "mirror/busybox"}


def git(*args, cwd=None):
    return subprocess.check_output(["git", *map(str, args)], cwd=cwd, text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()

    lock = json.loads(args.lock.read_text(encoding="utf-8"))
    args.dest.mkdir(parents=True, exist_ok=True)
    args.reports.mkdir(parents=True, exist_ok=True)

    records = {}
    for name, expected_repo in EXPECTED.items():
        entry = lock[name]
        repo, commit = entry["repository"], entry["commit"]
        if repo != expected_repo:
            raise SystemExit(f"{name}: repository mismatch: {repo!r}")
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise SystemExit(f"{name}: expected a full 40-digit lowercase commit SHA")
        target = args.dest / name
        if target.exists():
            raise SystemExit(f"{name}: refusing existing checkout {target}")
        target.mkdir()
        git("init", "-q", target)
        git("remote", "add", "origin", f"https://github.com/{repo}.git", cwd=target)
        print(f"Fetching {name} {repo}@{commit}", flush=True)
        git("fetch", "-q", "--depth=1", "--no-tags", "origin", commit, cwd=target)
        git("checkout", "-q", "--detach", "FETCH_HEAD", cwd=target)
        actual = git("rev-parse", "HEAD", cwd=target)
        if actual != commit:
            raise SystemExit(f"{name}: checkout mismatch: expected {commit}, got {actual}")
        records[name] = {"repository": repo, "commit": actual, "path": str(target)}

    (args.reports / "sources.json").write_text(
        json.dumps(records, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    summary = (
        "# Plan2001 userland CI scope\n\n"
        "Pinned source inputs fetched and verified:\n\n"
        + "".join(f"- {name}: `{record['repository']}@{record['commit']}`\n"
                  for name, record in records.items())
        + "\n**The host-control BusyBox is linked against the Linux runner's C library.** "
          "It is not a Plan2001 executable.\n\n"
          "| Check | Status |\n| --- | --- |\n"
          "| Pin verification | PASS (actual commits compared to lock) |\n"
          "| Plan2001 libc + ABI inventory | see audit report |\n"
          "| BusyBox Linux host-control compilation | see host-control report |\n"
          "| BusyBox linked against Plan2001 libc | **NOT RUN (not implemented)** |\n"
          "| BusyBox booted in Plan2001 QEMU | **NOT RUN (not implemented)** |\n"
          "| Physical hardware | **NOT RUN** |\n"
    )
    (args.reports / "SUMMARY.md").write_text(summary, encoding="utf-8")
    print("Verified immutable source revisions", flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Measure a command's wall time, CPU, peak RSS, output, and artifact sizes.

Uses only the Python standard library. The subprocess is launched directly and its
stdout/stderr is streamed to both the terminal and a log file. CPU time comes from
wait4's child rusage (which includes descendants waited for by the direct child);
peak RSS is sampled from the whole live /proc process tree.

Example:
    python3 scripts/measure.py --label tests --output reports/metrics.json \
        --directory build -- ./tests/run.sh
"""
from __future__ import annotations

import argparse
import json
import os
import resource
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

HZ = os.sysconf("SC_CLK_TCK")


def descendants(root: int) -> list[int]:
    """Return root plus all currently live descendants, best effort."""
    found: set[int] = {root}
    stack = [root]
    while stack:
        pid = stack.pop()
        try:
            with open(f"/proc/{pid}/task/{pid}/children", encoding="ascii") as handle:
                children = [int(value) for value in handle.read().split()]
        except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError):
            continue
        for child in children:
            if child not in found:
                found.add(child)
                stack.append(child)
    return list(found)


def proc_stat(pid: int) -> tuple[float, float]:
    """(user CPU seconds, system CPU seconds) for one process."""
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii") as handle:
            fields = handle.read().rsplit(") ", 1)[1].split()
        # fields[11] and [13] after the ')' split are utime and stime.
        return int(fields[11]) / HZ, int(fields[13]) / HZ
    except (FileNotFoundError, ProcessLookupError, PermissionError, IndexError, ValueError):
        return 0.0, 0.0


def proc_rss(pid: int) -> int:
    """Current resident set size in bytes."""
    try:
        with open(f"/proc/{pid}/status", encoding="ascii") as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) * 1024
    except (FileNotFoundError, ProcessLookupError, PermissionError, IndexError, ValueError):
        pass
    return 0


def directory_size(path: Path) -> int:
    if path.is_file():
        return path.stat().st_size
    if not path.exists():
        return 0
    total = 0
    for child in path.rglob("*"):
        try:
            if child.is_file() and not child.is_symlink():
                total += child.stat().st_size
        except OSError:
            continue
    return total


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True, help="Short name for this measurement")
    parser.add_argument("--output", default="reports/metrics.json", help="JSON report path")
    parser.add_argument("--log", default=None, help="Command output log; default reports/<label>.log")
    parser.add_argument("--directory", action="append", default=[], help="Directory/file whose final size is recorded")
    parser.add_argument("--database", default=None, help="Optional database path; its byte size is recorded")
    parser.add_argument("--sample-interval", type=float, default=0.05)
    parser.add_argument("command", nargs=argparse.REMAINDER, help="Command after --")
    args = parser.parse_args()
    if args.command and args.command[0] == "--":
        args.command.pop(0)
    if not args.command:
        parser.error("a command is required after --")
    return args


def main() -> int:
    args = parse_args()
    output = Path(args.output)
    log_path = Path(args.log) if args.log else output.parent / f"{args.label}.log"
    output.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.monotonic()
    with log_path.open("w", encoding="utf-8", buffering=1) as log:
        process = subprocess.Popen(
            args.command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        def drain() -> None:
            assert process.stdout is not None
            for line in process.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                log.write(line)

        reader = threading.Thread(target=drain, daemon=True)
        reader.start()

        peak_rss = 0
        sampled_cpu = (0.0, 0.0)
        samples = 0
        wait_user = wait_system = 0.0
        wait_maxrss = 0
        status = 0
        # Reap with wait4(WNOHANG) rather than Popen.poll(): wait4 returns the direct
        # child's rusage, including descendants it waited for, without the duplicate
        # accounting that RUSAGE_CHILDREN can show for deeply nested build shells.
        while True:
            pids = descendants(process.pid)
            peak_rss = max(peak_rss, sum(proc_rss(pid) for pid in pids))
            user = system = 0.0
            for pid in pids:
                u, s = proc_stat(pid)
                user += u
                system += s
            sampled_cpu = (user, system)
            samples += 1
            try:
                reaped, status, usage = os.wait4(process.pid, os.WNOHANG)
            except ChildProcessError:
                reaped, usage = process.pid, None
            if reaped == process.pid:
                if usage is not None:
                    wait_user, wait_system = usage.ru_utime, usage.ru_stime
                    wait_maxrss = usage.ru_maxrss * 1024
                # Tell Popen the child has been reaped so __del__ does not wait again.
                process._handle_exitstatus(status)  # noqa: SLF001 - CPython Popen API
                break
            time.sleep(max(args.sample_interval, 0.001))

        exit_code = process.returncode if process.returncode is not None else 0
        reader.join(timeout=5)
    end = time.monotonic()
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    wall = end - start

    rusage_user = max(0.0, after.ru_utime - before.ru_utime)
    rusage_system = max(0.0, after.ru_stime - before.ru_stime)
    # Prefer wait4; fall back to rusage only when the platform does not supply it.
    cpu_user = wait_user if wait_user > 0 else max(rusage_user, sampled_cpu[0])
    cpu_system = wait_system if wait_system > 0 else max(rusage_system, sampled_cpu[1])
    cpu_total = cpu_user + cpu_system
    peak_rss = max(peak_rss, wait_maxrss)

    sizes: dict[str, Any] = {}
    for item in args.directory:
        path = Path(item)
        sizes[str(path)] = directory_size(path)
    database_bytes = None
    if args.database:
        database_bytes = directory_size(Path(args.database))

    record = {
        "label": args.label,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "command": args.command,
        "exit_code": exit_code,
        "wall_seconds": round(wall, 3),
        "cpu_user_seconds": round(cpu_user, 3),
        "cpu_system_seconds": round(cpu_system, 3),
        "cpu_total_seconds": round(cpu_total, 3),
        "cpu_percent_of_wall": round(100.0 * cpu_total / wall, 1) if wall else 0.0,
        "peak_rss_bytes": peak_rss,
        "peak_rss_mib": round(peak_rss / (1024 * 1024), 2),
        "samples": samples,
        "artifact_sizes_bytes": sizes,
        "database_bytes": database_bytes,
        "log": str(log_path),
    }

    merged: dict[str, Any] = {"measurements": []}
    if output.exists():
        try:
            existing = json.loads(output.read_text(encoding="utf-8"))
            if isinstance(existing, dict) and isinstance(existing.get("measurements"), list):
                merged = existing
            else:
                merged["previous_report"] = existing
        except (OSError, json.JSONDecodeError):
            merged["previous_report_unreadable"] = True
    # Replace a prior measurement with the same label, preserving other labels.
    merged["measurements"] = [m for m in merged["measurements"] if m.get("label") != args.label]
    merged["measurements"].append(record)
    merged["measurements"].sort(key=lambda m: str(m.get("label", "")))
    merged["updated_at"] = datetime.now(timezone.utc).isoformat()
    output.write_text(json.dumps(merged, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"MEASURE {args.label}: exit={record['exit_code']} wall={record['wall_seconds']}s "
          f"cpu={record['cpu_total_seconds']}s peak_rss={record['peak_rss_mib']}MiB")
    return record["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())

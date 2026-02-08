#!/usr/bin/env python3
"""
Task Tracker - Analyze debug.log for task_group START/END tracking.

Identifies tasks that started but never ended (potential hangs).
"""

import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


def parse_timestamp(line: str) -> datetime | None:
    """Extract timestamp from log line if present."""
    # Common formats: [2026-02-07 12:34:56.789] or 2026-02-07 12:34:56.789
    patterns = [
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]',
        r'^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)',
    ]
    for pattern in patterns:
        match = re.search(pattern, line)
        if match:
            try:
                return datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S.%f")
            except ValueError:
                pass
    return None


def analyze_task_group(log_path: Path) -> None:
    """Analyze task_group START/END messages."""

    # Track tasks: name -> list of (start_time, end_time, line_num)
    tasks = defaultdict(list)
    active_tasks = {}  # name -> (start_time, start_line)

    # Patterns for task_group messages
    # New format: [task_group:group_name] START: task_name (active: N)
    start_pattern = re.compile(r'\[task_group:([^\]]+)\] START: ([^\s]+)')
    end_pattern = re.compile(r'\[task_group:([^\]]+)\] END: ([^\s]+)')
    exception_pattern = re.compile(r'\[task_group:([^\]]+)\] EXCEPTION: ([^\s]+)')

    print(f"Analyzing: {log_path}")
    print("=" * 70)

    with open(log_path, 'r', errors='replace') as f:
        for line_num, line in enumerate(f, 1):
            timestamp = parse_timestamp(line)

            # Check for START
            match = start_pattern.search(line)
            if match:
                group_name = match.group(1)
                task_name = match.group(2)
                full_name = f"{group_name}:{task_name}"
                if full_name in active_tasks:
                    # Task already running - might be multiple instances
                    pass
                active_tasks[full_name] = (timestamp, line_num)
                tasks[full_name].append({
                    'start_time': timestamp,
                    'start_line': line_num,
                    'end_time': None,
                    'end_line': None,
                    'exception': False,
                    'group': group_name
                })
                continue

            # Check for EXCEPTION
            match = exception_pattern.search(line)
            if match:
                group_name = match.group(1)
                task_name = match.group(2)
                full_name = f"{group_name}:{task_name}"
                if tasks[full_name]:
                    tasks[full_name][-1]['exception'] = True
                continue

            # Check for END
            match = end_pattern.search(line)
            if match:
                group_name = match.group(1)
                task_name = match.group(2)
                full_name = f"{group_name}:{task_name}"
                if full_name in active_tasks:
                    del active_tasks[full_name]
                if tasks[full_name]:
                    # Find the last unfinished instance
                    for task in reversed(tasks[full_name]):
                        if task['end_time'] is None:
                            task['end_time'] = timestamp
                            task['end_line'] = line_num
                            break
                continue

    # Report
    print("\n### COMPLETED TASKS ###\n")
    completed = []
    for name, instances in sorted(tasks.items()):
        for inst in instances:
            if inst['end_time'] is not None:
                duration = None
                if inst['start_time'] and inst['end_time']:
                    duration = (inst['end_time'] - inst['start_time']).total_seconds()
                completed.append((name, inst, duration))

    if completed:
        for name, inst, duration in completed:
            duration_str = f" ({duration:.2f}s)" if duration else ""
            exc_str = " [EXCEPTION]" if inst['exception'] else ""
            print(f"  {name}: lines {inst['start_line']}-{inst['end_line']}{duration_str}{exc_str}")
    else:
        print("  (none)")

    print("\n### HANGING TASKS (started but never ended) ###\n")
    hanging = []
    for name, instances in sorted(tasks.items()):
        for inst in instances:
            if inst['end_time'] is None:
                hanging.append((name, inst))

    if hanging:
        for name, inst in hanging:
            exc_str = " [EXCEPTION]" if inst['exception'] else ""
            print(f"  {name}: started at line {inst['start_line']}{exc_str}")
    else:
        print("  (none - all tasks completed!)")

    print("\n### SUMMARY ###\n")
    total_started = sum(len(instances) for instances in tasks.values())
    total_completed = len(completed)
    total_hanging = len(hanging)

    print(f"  Total tasks started: {total_started}")
    print(f"  Completed: {total_completed}")
    print(f"  Hanging: {total_hanging}")

    if active_tasks:
        print(f"\n  Active task names: {', '.join(sorted(active_tasks.keys()))}")


def analyze_peer_tasks(log_path: Path) -> None:
    """Analyze peer-related task patterns."""

    print("\n" + "=" * 70)
    print("### PEER TASK ANALYSIS ###\n")

    peer_starts = defaultdict(int)
    peer_ends = defaultdict(int)

    # New format: [task_group:group_name] START: task_name (active: N)
    start_pattern = re.compile(r'\[task_group:[^\]]+\] START: (peer_[^\s]+)')
    end_pattern = re.compile(r'\[task_group:[^\]]+\] END: (peer_[^\s]+)')

    with open(log_path, 'r', errors='replace') as f:
        for line in f:
            match = start_pattern.search(line)
            if match:
                # Extract just the peer address part (without counter)
                full_name = match.group(1)
                # peer_in_1.2.3.4:8333:0 -> peer_in_1.2.3.4:8333
                parts = full_name.rsplit(':', 1)
                if len(parts) == 2 and parts[1].isdigit():
                    peer_key = parts[0]
                else:
                    peer_key = full_name
                peer_starts[peer_key] += 1
                continue

            match = end_pattern.search(line)
            if match:
                full_name = match.group(1)
                parts = full_name.rsplit(':', 1)
                if len(parts) == 2 and parts[1].isdigit():
                    peer_key = parts[0]
                else:
                    peer_key = full_name
                peer_ends[peer_key] += 1

    all_peers = set(peer_starts.keys()) | set(peer_ends.keys())

    if all_peers:
        print("  Peer task counts (starts / ends):\n")
        for peer in sorted(all_peers):
            starts = peer_starts[peer]
            ends = peer_ends[peer]
            status = "OK" if starts == ends else "MISMATCH!"
            print(f"    {peer}: {starts} / {ends}  [{status}]")
    else:
        print("  No peer tasks found.")


def main():
    if len(sys.argv) > 1:
        log_path = Path(sys.argv[1])
    else:
        # Default to debug.log in current directory
        log_path = Path("debug.log")

    if not log_path.exists():
        print(f"Error: {log_path} not found")
        sys.exit(1)

    analyze_task_group(log_path)
    analyze_peer_tasks(log_path)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Analyze performance degradation over time."""

import re
import sys

def parse_line(line):
    # Pattern: 200000/930000 (21.5%) | 2088ms (f:7 s:577 p:1107 a:397 d:0)
    pattern = r'(\d+)/930000.*\| (\d+)ms \(f:(\d+) s:(\d+) p:(\d+) a:(\d+) d:(\d+)\)'
    m = re.search(pattern, line)
    if m:
        return {
            'height': int(m.group(1)),
            'total': int(m.group(2)),
            'fetch': int(m.group(3)),
            'serial': int(m.group(4)),
            'process': int(m.group(5)),
            'apply': int(m.group(6)),
            'deferred': int(m.group(7)),
        }
    return None

def main():
    data = []
    with open('stdout4.txt', 'r') as f:
        for line in f:
            parsed = parse_line(line)
            if parsed:
                data.append(parsed)

    print("=" * 100)
    print("PERFORMANCE DEGRADATION ANALYSIS")
    print("=" * 100)
    print()

    # Find where fetch time jumps
    print("FETCH TIME JUMPS (looking for > 100ms increase):")
    print("-" * 60)
    prev = None
    for d in data:
        if prev and d['fetch'] - prev['fetch'] > 100:
            print(f"Height {prev['height']:>6} -> {d['height']:>6}: fetch {prev['fetch']:>6}ms -> {d['fetch']:>6}ms (+{d['fetch']-prev['fetch']}ms)")
        prev = d
    print()

    # Show progression at key heights
    print("PROGRESSION AT KEY HEIGHTS:")
    print("-" * 100)
    print(f"{'Height':>8} | {'Total':>8} | {'Fetch':>8} | {'Serial':>8} | {'Process':>8} | {'Apply':>8} | {'Deferred':>8} | {'f%':>6}")
    print("-" * 100)

    for d in data:
        if d['height'] % 50000 == 0 or d['height'] in [223000, 224000, 225000]:
            f_pct = 100 * d['fetch'] / d['total'] if d['total'] > 0 else 0
            print(f"{d['height']:>8} | {d['total']:>8} | {d['fetch']:>8} | {d['serial']:>8} | {d['process']:>8} | {d['apply']:>8} | {d['deferred']:>8} | {f_pct:>5.1f}%")
    print()

    # Analyze the trend
    print("TREND ANALYSIS (every 50k blocks):")
    print("-" * 80)

    buckets = {}
    for d in data:
        bucket = (d['height'] // 50000) * 50000
        if bucket not in buckets:
            buckets[bucket] = []
        buckets[bucket].append(d)

    print(f"{'Range':>12} | {'Avg Total':>10} | {'Avg Fetch':>10} | {'Avg Serial':>10} | {'Avg Process':>10} | {'Fetch %':>8}")
    print("-" * 80)

    for bucket in sorted(buckets.keys()):
        items = buckets[bucket]
        avg_total = sum(d['total'] for d in items) / len(items)
        avg_fetch = sum(d['fetch'] for d in items) / len(items)
        avg_serial = sum(d['serial'] for d in items) / len(items)
        avg_process = sum(d['process'] for d in items) / len(items)
        fetch_pct = 100 * avg_fetch / avg_total if avg_total > 0 else 0
        print(f"{bucket:>6}-{bucket+50000:<6}| {avg_total:>10.0f} | {avg_fetch:>10.0f} | {avg_serial:>10.0f} | {avg_process:>10.0f} | {fetch_pct:>7.1f}%")

    print()
    print("CONCLUSION:")
    print("-" * 80)

    # Get last entry
    last = data[-1]
    first_fast = [d for d in data if d['fetch'] < 50]
    last_fast = first_fast[-1] if first_fast else data[0]

    print(f"Fetch time went from {last_fast['fetch']}ms (height {last_fast['height']}) to {last['fetch']}ms (height {last['height']})")
    print(f"This is a {last['fetch'] / max(last_fast['fetch'], 1):.0f}x slowdown in fetch time.")
    print()
    print("Probable cause: OS page cache eviction.")
    print("The LMDB database file is larger than available RAM cache.")
    print("Later blocks need to be read from disk instead of RAM.")

if __name__ == '__main__':
    main()

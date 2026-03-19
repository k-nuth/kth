#!/usr/bin/env python3
"""Compare UTXO builder log files to analyze performance differences."""

import re
import sys

def parse_log_line(line):
    """Parse a log line and extract timing data."""
    # Pattern for stdout.log (individual get_block)
    # fetch: 905ms, process: 1052ms, apply: 338ms, deferred: 0ms, total: 2295ms
    pattern1 = r'(\d+)/930000.*fetch: (\d+)ms, process: (\d+)ms, apply: (\d+)ms, deferred: (\d+)ms, total: (\d+)ms'

    # Pattern for stdout2.log and stdout3.log (batch)
    # 200000/930000 (21.5%) | 2662ms (f:1451 p:1105 a:106 d:0)
    pattern2 = r'(\d+)/930000.*\| (\d+)ms \(f:(\d+) p:(\d+) a:(\d+) d:(\d+)\)'

    # Pattern for stdout3.log with serialization time
    # 200000/930000 (21.5%) | 2067ms (f:8 s:651 p:1060 a:348 d:0)
    pattern3 = r'(\d+)/930000.*\| (\d+)ms \(f:(\d+) s:(\d+) p:(\d+) a:(\d+) d:(\d+)\)'

    m = re.search(pattern3, line)
    if m:
        return {
            'height': int(m.group(1)),
            'total': int(m.group(2)),
            'fetch': int(m.group(3)),
            'serial': int(m.group(4)),
            'process': int(m.group(5)),
            'apply': int(m.group(6)),
            'deferred': int(m.group(7)),
            'type': 'raw'
        }

    m = re.search(pattern2, line)
    if m:
        return {
            'height': int(m.group(1)),
            'total': int(m.group(2)),
            'fetch': int(m.group(3)),
            'serial': 0,
            'process': int(m.group(4)),
            'apply': int(m.group(5)),
            'deferred': int(m.group(6)),
            'type': 'batch'
        }

    m = re.search(pattern1, line)
    if m:
        return {
            'height': int(m.group(1)),
            'fetch': int(m.group(2)),
            'serial': 0,
            'process': int(m.group(3)),
            'apply': int(m.group(4)),
            'deferred': int(m.group(5)),
            'total': int(m.group(6)),
            'type': 'individual'
        }

    return None

def parse_log_file(filepath):
    """Parse a log file and return dict of height -> data."""
    data = {}
    with open(filepath, 'r') as f:
        for line in f:
            parsed = parse_log_line(line)
            if parsed:
                data[parsed['height']] = parsed
    return data

def main():
    log1 = parse_log_file('stdout.log')   # individual get_block()
    log2 = parse_log_file('stdout2.log')  # batch get_blocks() with deserial
    log3 = parse_log_file('stdout3.log')  # raw get_blocks_raw()

    # Find common heights
    heights = sorted(set(log1.keys()) & set(log2.keys()) & set(log3.keys()))

    print("=" * 120)
    print("COMPARISON: Individual vs Batch(deserial) vs Raw")
    print("=" * 120)
    print()

    # Header
    print(f"{'Height':>8} | {'--- Individual ---':^30} | {'--- Batch+Deser ---':^30} | {'--- Raw Batch ---':^35} | {'Winner':^8}")
    print(f"{'':>8} | {'total':>8} {'fetch':>8} {'proc':>8} | {'total':>8} {'fetch':>8} {'proc':>8} | {'total':>8} {'f':>6} {'s':>6} {'proc':>8} | {'':^8}")
    print("-" * 120)

    # Stats
    individual_wins = 0
    batch_wins = 0
    raw_wins = 0

    total_individual = 0
    total_batch = 0
    total_raw = 0

    # Sample every 10k blocks for readability
    sample_heights = [h for h in heights if h % 10000 == 0 and h > 0]

    for h in sample_heights:
        d1 = log1[h]
        d2 = log2[h]
        d3 = log3[h]

        total_individual += d1['total']
        total_batch += d2['total']
        total_raw += d3['total']

        # Determine winner
        min_total = min(d1['total'], d2['total'], d3['total'])
        if d3['total'] == min_total:
            winner = "RAW"
            raw_wins += 1
        elif d1['total'] == min_total:
            winner = "INDIV"
            individual_wins += 1
        else:
            winner = "BATCH"
            batch_wins += 1

        print(f"{h:>8} | {d1['total']:>8} {d1['fetch']:>8} {d1['process']:>8} | "
              f"{d2['total']:>8} {d2['fetch']:>8} {d2['process']:>8} | "
              f"{d3['total']:>8} {d3['fetch']:>6} {d3['serial']:>6} {d3['process']:>8} | {winner:^8}")

    print("-" * 120)
    print()

    # Summary
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Sample points: {len(sample_heights)}")
    print(f"Wins - Individual: {individual_wins}, Batch: {batch_wins}, Raw: {raw_wins}")
    print()
    print(f"Cumulative time (sampled heights only):")
    print(f"  Individual: {total_individual:,} ms")
    print(f"  Batch:      {total_batch:,} ms ({total_batch/total_individual:.2%} of individual)")
    print(f"  Raw:        {total_raw:,} ms ({total_raw/total_individual:.2%} of individual)")
    print()

    # Detailed breakdown at specific heights
    print("=" * 80)
    print("DETAILED BREAKDOWN AT KEY HEIGHTS")
    print("=" * 80)

    key_heights = [50000, 100000, 150000, 200000, 220000]
    for h in key_heights:
        if h not in log1 or h not in log2 or h not in log3:
            continue
        d1 = log1[h]
        d2 = log2[h]
        d3 = log3[h]

        print(f"\nHeight {h}:")
        print(f"  Individual: total={d1['total']:>5}ms  fetch={d1['fetch']:>5}ms  process={d1['process']:>5}ms  apply={d1['apply']:>5}ms")
        print(f"  Batch:      total={d2['total']:>5}ms  fetch={d2['fetch']:>5}ms  process={d2['process']:>5}ms  apply={d2['apply']:>5}ms")
        print(f"  Raw:        total={d3['total']:>5}ms  fetch={d3['fetch']:>5}ms  serial={d3['serial']:>5}ms  process={d3['process']:>5}ms  apply={d3['apply']:>5}ms")

        # Analysis
        print(f"  Analysis:")
        print(f"    - Raw fetch ({d3['fetch']}ms) vs Individual fetch ({d1['fetch']}ms): LMDB overhead = {d1['fetch'] - d3['fetch'] - d3['serial']}ms")
        print(f"    - Batch fetch ({d2['fetch']}ms) vs Raw fetch+serial ({d3['fetch'] + d3['serial']}ms): cursor overhead = {d2['fetch'] - d3['fetch'] - d3['serial']}ms")
        print(f"    - Total speedup Raw vs Individual: {d1['total'] - d3['total']}ms ({100*(d1['total']-d3['total'])/d1['total']:.1f}%)")

if __name__ == '__main__':
    main()

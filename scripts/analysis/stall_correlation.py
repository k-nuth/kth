#!/usr/bin/env python3
"""
Correlate throughput stalls with block heights to identify problematic blocks.
"""

import re
import sys
from datetime import datetime
from dataclasses import dataclass


@dataclass
class StatsEntry:
    timestamp: datetime
    blocks: int
    blk_per_sec: int
    mb_per_sec: float
    peers: int


def parse_log(logfile: str) -> list[StatsEntry]:
    """Parse block_supervisor Stats lines."""
    entries = []
    pattern = r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*Stats: (\d+) blocks \((\d+) blk/s, ([\d.]+) MB/s\), (\d+) peers'

    with open(logfile, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                entries.append(StatsEntry(
                    timestamp=datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f'),
                    blocks=int(match.group(2)),
                    blk_per_sec=int(match.group(3)),
                    mb_per_sec=float(match.group(4)),
                    peers=int(match.group(5))
                ))
    return entries


def analyze_stalls(entries: list[StatsEntry]) -> None:
    """Find stalls and correlate with block heights."""
    print("=" * 80)
    print("STALL ANALYSIS BY BLOCK HEIGHT")
    print("=" * 80)

    stalls = []

    for i in range(1, len(entries)):
        prev = entries[i-1]
        curr = entries[i]

        # Detect stall: high throughput followed by low
        if prev.mb_per_sec > 30 and curr.mb_per_sec < 10:
            gap = (curr.timestamp - prev.timestamp).total_seconds()
            stalls.append({
                'time': curr.timestamp,
                'block_height': curr.blocks,
                'from_mb': prev.mb_per_sec,
                'to_mb': curr.mb_per_sec,
                'gap': gap,
                'peers': curr.peers
            })

    print(f"\nFound {len(stalls)} significant stalls (>30 MB/s to <10 MB/s)")

    # Group stalls by block height ranges (10k blocks each)
    height_buckets: dict[int, list] = {}
    for s in stalls:
        bucket = (s['block_height'] // 10000) * 10000
        if bucket not in height_buckets:
            height_buckets[bucket] = []
        height_buckets[bucket].append(s)

    print("\nStalls by block height range:")
    for bucket in sorted(height_buckets.keys()):
        stalls_in_bucket = height_buckets[bucket]
        print(f"\n  {bucket:,} - {bucket+9999:,}: {len(stalls_in_bucket)} stalls")
        for s in stalls_in_bucket[:5]:  # Show first 5
            print(f"    Block {s['block_height']:,}: {s['from_mb']:.1f} -> {s['to_mb']:.1f} MB/s, gap={s['gap']:.1f}s, {s['peers']} peers")

    # Calculate average throughput by height range
    print("\n" + "=" * 80)
    print("AVERAGE THROUGHPUT BY BLOCK HEIGHT RANGE")
    print("=" * 80)

    throughput_by_height: dict[int, list[float]] = {}
    for e in entries:
        bucket = (e.blocks // 50000) * 50000
        if bucket not in throughput_by_height:
            throughput_by_height[bucket] = []
        throughput_by_height[bucket].append(e.mb_per_sec)

    print("\nThroughput by 50k block ranges:")
    for bucket in sorted(throughput_by_height.keys()):
        values = throughput_by_height[bucket]
        avg = sum(values) / len(values)
        min_val = min(values)
        max_val = max(values)
        print(f"  {bucket:,} - {bucket+49999:,}: avg={avg:.1f} MB/s, min={min_val:.1f}, max={max_val:.1f} (n={len(values)})")

    # Look for consecutive low-throughput periods
    print("\n" + "=" * 80)
    print("EXTENDED LOW THROUGHPUT PERIODS")
    print("=" * 80)

    low_periods = []
    in_low_period = False
    period_start = None
    period_entries = []

    for e in entries:
        if e.mb_per_sec < 15:  # Low throughput threshold
            if not in_low_period:
                in_low_period = True
                period_start = e
                period_entries = [e]
            else:
                period_entries.append(e)
        else:
            if in_low_period and len(period_entries) >= 3:  # At least 3 consecutive low samples
                duration = (period_entries[-1].timestamp - period_start.timestamp).total_seconds()
                low_periods.append({
                    'start_time': period_start.timestamp,
                    'start_block': period_start.blocks,
                    'end_block': period_entries[-1].blocks,
                    'duration': duration,
                    'samples': len(period_entries),
                    'avg_mb': sum(e.mb_per_sec for e in period_entries) / len(period_entries),
                    'avg_peers': sum(e.peers for e in period_entries) / len(period_entries)
                })
            in_low_period = False
            period_entries = []

    print(f"\nFound {len(low_periods)} extended low-throughput periods (>=3 consecutive samples <15 MB/s)")
    for p in sorted(low_periods, key=lambda x: x['duration'], reverse=True)[:15]:
        print(f"  {p['start_time'].strftime('%H:%M:%S')}: blocks {p['start_block']:,}-{p['end_block']:,}, "
              f"duration={p['duration']:.1f}s, avg={p['avg_mb']:.1f} MB/s, peers={p['avg_peers']:.0f}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    entries = parse_log(sys.argv[1])
    print(f"Parsed {len(entries)} stats entries")

    if entries:
        analyze_stalls(entries)


if __name__ == '__main__':
    main()

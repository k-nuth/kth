#!/usr/bin/env python3
"""
Analyze block download throughput patterns to identify causes of speed variations.

Usage:
    python throughput_analysis.py <logfile>
    python throughput_analysis.py /path/to/solo-download-6.log
"""

import re
import sys
from dataclasses import dataclass
from datetime import datetime
from typing import Optional


@dataclass
class StatsSample:
    timestamp: datetime
    blocks: int
    blk_per_sec: int
    mb_per_sec: float
    peers_downloading: int


def parse_stats_line(line: str) -> Optional[StatsSample]:
    """Parse a block_supervisor Stats line."""
    # [2026-02-07 21:42:28.691] [info] [block_supervisor] Stats: 931585 blocks (506 blk/s, 78.2 MB/s), 33 peers downloading
    pattern = r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*Stats: (\d+) blocks \((\d+) blk/s, ([\d.]+) MB/s\), (\d+) peers'
    match = re.search(pattern, line)
    if not match:
        return None

    return StatsSample(
        timestamp=datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f'),
        blocks=int(match.group(2)),
        blk_per_sec=int(match.group(3)),
        mb_per_sec=float(match.group(4)),
        peers_downloading=int(match.group(5))
    )


def analyze_throughput(samples: list[StatsSample]) -> None:
    """Analyze throughput patterns."""
    if not samples:
        print("No samples found")
        return

    # Basic stats
    mb_values = [s.mb_per_sec for s in samples]
    blk_values = [s.blk_per_sec for s in samples]
    peer_values = [s.peers_downloading for s in samples]

    print("=" * 70)
    print("THROUGHPUT ANALYSIS")
    print("=" * 70)

    print(f"\nTotal samples: {len(samples)}")
    print(f"Time range: {samples[0].timestamp} to {samples[-1].timestamp}")
    duration = (samples[-1].timestamp - samples[0].timestamp).total_seconds()
    print(f"Duration: {duration:.1f} seconds ({duration/60:.1f} minutes)")

    print(f"\n--- MB/s Statistics ---")
    print(f"  Min: {min(mb_values):.1f} MB/s")
    print(f"  Max: {max(mb_values):.1f} MB/s")
    print(f"  Avg: {sum(mb_values)/len(mb_values):.1f} MB/s")

    print(f"\n--- Blocks/s Statistics ---")
    print(f"  Min: {min(blk_values)} blk/s")
    print(f"  Max: {max(blk_values)} blk/s")
    print(f"  Avg: {sum(blk_values)/len(blk_values):.0f} blk/s")

    print(f"\n--- Peers Statistics ---")
    print(f"  Min: {min(peer_values)} peers")
    print(f"  Max: {max(peer_values)} peers")
    print(f"  Avg: {sum(peer_values)/len(peer_values):.1f} peers")

    # Identify drops
    print("\n" + "=" * 70)
    print("DROP ANALYSIS (looking for sudden throughput drops)")
    print("=" * 70)

    drops = []
    for i in range(1, len(samples)):
        prev = samples[i-1]
        curr = samples[i]

        # Detect significant drops (>50% drop or from high to very low)
        if prev.mb_per_sec > 30 and curr.mb_per_sec < 15:
            time_gap = (curr.timestamp - prev.timestamp).total_seconds()
            drops.append({
                'index': i,
                'time': curr.timestamp,
                'from_mb': prev.mb_per_sec,
                'to_mb': curr.mb_per_sec,
                'from_blk': prev.blk_per_sec,
                'to_blk': curr.blk_per_sec,
                'peers_before': prev.peers_downloading,
                'peers_after': curr.peers_downloading,
                'time_gap': time_gap
            })

    print(f"\nFound {len(drops)} significant drops (>30 MB/s to <15 MB/s)")

    if drops:
        print("\nTop 20 drops:")
        for i, drop in enumerate(drops[:20]):
            peer_change = drop['peers_after'] - drop['peers_before']
            peer_str = f"peers: {drop['peers_before']}->{drop['peers_after']}" if peer_change != 0 else f"peers: {drop['peers_before']} (same)"
            print(f"  {drop['time'].strftime('%H:%M:%S')} | {drop['from_mb']:5.1f} -> {drop['to_mb']:5.1f} MB/s | "
                  f"{drop['from_blk']:4d} -> {drop['to_blk']:4d} blk/s | {peer_str} | gap: {drop['time_gap']:.1f}s")

    # Analyze correlation between peers and throughput
    print("\n" + "=" * 70)
    print("PEER COUNT vs THROUGHPUT CORRELATION")
    print("=" * 70)

    # Group by peer count
    by_peers: dict[int, list[float]] = {}
    for s in samples:
        if s.peers_downloading not in by_peers:
            by_peers[s.peers_downloading] = []
        by_peers[s.peers_downloading].append(s.mb_per_sec)

    print("\nAverage throughput by peer count:")
    for peer_count in sorted(by_peers.keys()):
        values = by_peers[peer_count]
        avg = sum(values) / len(values)
        print(f"  {peer_count:2d} peers: {avg:5.1f} MB/s avg ({len(values):3d} samples)")

    # Look for stalls (very low throughput with many peers)
    print("\n" + "=" * 70)
    print("STALL ANALYSIS (low throughput despite many peers)")
    print("=" * 70)

    stalls = [s for s in samples if s.mb_per_sec < 5 and s.peers_downloading > 20]
    print(f"\nFound {len(stalls)} stalls (<5 MB/s with >20 peers)")

    if stalls:
        print("\nStall events (first 20):")
        for s in stalls[:20]:
            print(f"  {s.timestamp.strftime('%H:%M:%S')} | {s.mb_per_sec:5.1f} MB/s | {s.blk_per_sec:4d} blk/s | {s.peers_downloading} peers")

    # Look for time gaps (periods where stats weren't reported - could indicate blocking)
    print("\n" + "=" * 70)
    print("TIME GAP ANALYSIS (looking for periods of no stats)")
    print("=" * 70)

    gaps = []
    for i in range(1, len(samples)):
        gap = (samples[i].timestamp - samples[i-1].timestamp).total_seconds()
        if gap > 5:  # More than 5 seconds between stats
            gaps.append({
                'time': samples[i].timestamp,
                'gap': gap,
                'mb_before': samples[i-1].mb_per_sec,
                'mb_after': samples[i].mb_per_sec
            })

    print(f"\nFound {len(gaps)} gaps > 5 seconds")
    if gaps:
        print("\nLarge gaps (first 20):")
        for g in gaps[:20]:
            print(f"  {g['time'].strftime('%H:%M:%S')} | gap: {g['gap']:5.1f}s | {g['mb_before']:.1f} -> {g['mb_after']:.1f} MB/s")

    # Throughput distribution
    print("\n" + "=" * 70)
    print("THROUGHPUT DISTRIBUTION")
    print("=" * 70)

    buckets = {
        '0-10 MB/s': 0,
        '10-30 MB/s': 0,
        '30-50 MB/s': 0,
        '50-80 MB/s': 0,
        '80-100 MB/s': 0,
        '100+ MB/s': 0
    }

    for s in samples:
        if s.mb_per_sec < 10:
            buckets['0-10 MB/s'] += 1
        elif s.mb_per_sec < 30:
            buckets['10-30 MB/s'] += 1
        elif s.mb_per_sec < 50:
            buckets['30-50 MB/s'] += 1
        elif s.mb_per_sec < 80:
            buckets['50-80 MB/s'] += 1
        elif s.mb_per_sec < 100:
            buckets['80-100 MB/s'] += 1
        else:
            buckets['100+ MB/s'] += 1

    total = len(samples)
    for bucket, count in buckets.items():
        pct = count / total * 100
        bar = '#' * int(pct / 2)
        print(f"  {bucket:12s}: {count:4d} ({pct:5.1f}%) {bar}")


def analyze_round_transitions(logfile: str) -> None:
    """Look for patterns that might indicate round transitions in chunk_coordinator."""
    print("\n" + "=" * 70)
    print("ROUND TRANSITION ANALYSIS")
    print("=" * 70)

    # Look for claim_chunk retry patterns (20ms waits)
    # These happen when: resetting_ is true, or all slots occupied
    retry_patterns = []

    with open(logfile, 'r') as f:
        for line in f:
            if 'Round reset in progress' in line or 'All slots occupied' in line:
                retry_patterns.append(line.strip())

    print(f"\nFound {len(retry_patterns)} claim_chunk retry events")
    if retry_patterns:
        print("First 10:")
        for p in retry_patterns[:10]:
            print(f"  {p}")


def analyze_chunk_timing(logfile: str) -> None:
    """Analyze chunk download timing patterns."""
    print("\n" + "=" * 70)
    print("CHUNK DOWNLOAD TIMING ANALYSIS")
    print("=" * 70)

    # Pattern: [block_download] Peer X downloaded Y blocks for chunk Z in Nms
    import re
    pattern = r'downloaded (\d+) blocks for chunk (\d+) in (\d+)ms'

    chunk_times: dict[int, list[int]] = {}  # chunk_id -> list of times (retries)

    with open(logfile, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                blocks = int(match.group(1))
                chunk_id = int(match.group(2))
                time_ms = int(match.group(3))
                if chunk_id not in chunk_times:
                    chunk_times[chunk_id] = []
                chunk_times[chunk_id].append(time_ms)

    if not chunk_times:
        print("No chunk timing data found")
        return

    # Flatten all times
    all_times = []
    for times in chunk_times.values():
        all_times.extend(times)

    print(f"\nTotal chunks downloaded: {len(chunk_times)}")
    print(f"Total download attempts: {len(all_times)}")

    if all_times:
        all_times.sort()
        n = len(all_times)
        print(f"\nChunk download times:")
        print(f"  Min: {all_times[0]}ms")
        print(f"  Median: {all_times[n//2]}ms")
        print(f"  p95: {all_times[int(n*0.95)]}ms")
        print(f"  p99: {all_times[int(n*0.99)]}ms")
        print(f"  Max: {all_times[-1]}ms")
        print(f"  Avg: {sum(all_times)/len(all_times):.0f}ms")

    # Find slow chunks (>2000ms)
    slow_chunks = [(cid, times) for cid, times in chunk_times.items()
                   if any(t > 2000 for t in times)]
    print(f"\nSlow chunks (>2000ms): {len(slow_chunks)}")
    if slow_chunks:
        print("First 10:")
        for cid, times in sorted(slow_chunks, key=lambda x: max(x[1]), reverse=True)[:10]:
            print(f"  Chunk {cid}: {times}ms")


def analyze_peer_events(logfile: str) -> None:
    """Analyze peer connection/disconnection events that might cause drops."""
    print("\n" + "=" * 70)
    print("PEER EVENT ANALYSIS")
    print("=" * 70)

    import re

    # Look for peer task endings
    task_ended_pattern = r'\[block_supervisor:task_ended\].*remaining downloading=(\d+)'
    peer_updates_pattern = r'\[block_supervisor:peers_updated\].*spawned=(\d+)'

    task_ends = []
    peer_updates = []

    with open(logfile, 'r') as f:
        for line in f:
            # Extract timestamp
            ts_match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]', line)
            if not ts_match:
                continue
            ts = ts_match.group(1)

            if 'task_ended' in line:
                match = re.search(task_ended_pattern, line)
                if match:
                    task_ends.append((ts, int(match.group(1))))

            if 'peers_updated' in line:
                match = re.search(peer_updates_pattern, line)
                if match:
                    peer_updates.append((ts, int(match.group(1))))

    print(f"\nTask ended events: {len(task_ends)}")
    print(f"Peers updated events: {len(peer_updates)}")

    # Look for bursts of task endings (multiple within 2 seconds)
    if task_ends:
        bursts = []
        i = 0
        while i < len(task_ends):
            burst = [task_ends[i]]
            j = i + 1
            while j < len(task_ends):
                # Parse timestamps and check if within 2 seconds
                t1 = datetime.strptime(task_ends[i][0], '%Y-%m-%d %H:%M:%S.%f')
                t2 = datetime.strptime(task_ends[j][0], '%Y-%m-%d %H:%M:%S.%f')
                if (t2 - t1).total_seconds() <= 2:
                    burst.append(task_ends[j])
                    j += 1
                else:
                    break
            if len(burst) >= 3:
                bursts.append(burst)
            i = j if j > i + 1 else i + 1

        print(f"\nBursts of task endings (>=3 within 2s): {len(bursts)}")
        if bursts:
            print("First 5 bursts:")
            for burst in bursts[:5]:
                print(f"  {burst[0][0]}: {len(burst)} tasks ended, remaining: {burst[-1][1]}")


def analyze_channel_waits(logfile: str) -> None:
    """Analyze channel retry patterns (backpressure)."""
    print("\n" + "=" * 70)
    print("CHANNEL BACKPRESSURE ANALYSIS")
    print("=" * 70)

    channel_full_count = 0
    retry_warnings = 0

    with open(logfile, 'r') as f:
        for line in f:
            if 'Channel full' in line:
                channel_full_count += 1
            if 'after retries' in line:
                retry_warnings += 1

    print(f"\n'Channel full' events: {channel_full_count}")
    print(f"'after retries' warnings: {retry_warnings}")

    if channel_full_count > 0:
        print("\nChannel backpressure detected - this can cause throughput drops!")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]

    samples = []
    with open(logfile, 'r') as f:
        for line in f:
            if 'block_supervisor' in line and 'Stats:' in line:
                sample = parse_stats_line(line)
                if sample:
                    samples.append(sample)

    print(f"Parsed {len(samples)} stats samples from {logfile}")
    analyze_throughput(samples)

    # Additional analyses
    analyze_round_transitions(logfile)
    analyze_chunk_timing(logfile)
    analyze_peer_events(logfile)
    analyze_channel_waits(logfile)


if __name__ == '__main__':
    main()

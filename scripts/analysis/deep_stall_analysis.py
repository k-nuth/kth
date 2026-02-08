#!/usr/bin/env python3
"""
Deep analysis of throughput stalls using full debug.log.
Looks for patterns in chunk downloads, peer behavior, and timing.
"""

import re
import sys
from collections import defaultdict
from datetime import datetime, timedelta
from dataclasses import dataclass
from typing import Optional


@dataclass
class ChunkEvent:
    timestamp: datetime
    event_type: str  # 'claimed', 'downloaded', 'completed', 'failed', 'timeout'
    chunk_id: int
    peer: Optional[str] = None
    download_time_ms: Optional[int] = None
    blocks: Optional[int] = None


def parse_timestamp(line: str) -> Optional[datetime]:
    """Extract timestamp from log line."""
    match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]', line)
    if match:
        return datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f')
    return None


def analyze_chunk_events(logfile: str) -> None:
    """Analyze chunk claim/download/complete events."""
    print("=" * 80)
    print("CHUNK EVENT ANALYSIS")
    print("=" * 80)

    events: list[ChunkEvent] = []

    # Patterns to look for
    claimed_pattern = r'\[block_download\] Peer ([\d.:]+) claiming chunk (\d+)'
    downloaded_pattern = r'\[block_download\] Peer ([\d.:]+) downloaded (\d+) blocks for chunk (\d+) in (\d+)ms'
    completed_pattern = r'\[chunk_coordinator\] Chunk (\d+) completed'
    failed_pattern = r'\[chunk_coordinator\] Chunk (\d+) failed'
    timeout_pattern = r'\[chunk_coordinator\] Chunk (\d+).*timed out'
    round_pattern = r'\[chunk_coordinator\] Advancing to round (\d+)'

    round_advances = []

    with open(logfile, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            if not ts:
                continue

            if match := re.search(claimed_pattern, line):
                events.append(ChunkEvent(ts, 'claimed', int(match.group(2)), peer=match.group(1)))
            elif match := re.search(downloaded_pattern, line):
                events.append(ChunkEvent(ts, 'downloaded', int(match.group(3)),
                    peer=match.group(1), download_time_ms=int(match.group(4)), blocks=int(match.group(2))))
            elif match := re.search(completed_pattern, line):
                events.append(ChunkEvent(ts, 'completed', int(match.group(1))))
            elif match := re.search(failed_pattern, line):
                events.append(ChunkEvent(ts, 'failed', int(match.group(1))))
            elif match := re.search(timeout_pattern, line):
                events.append(ChunkEvent(ts, 'timeout', int(match.group(1))))
            elif match := re.search(round_pattern, line):
                round_advances.append((ts, int(match.group(1))))

    print(f"\nTotal chunk events: {len(events)}")
    print(f"  Claimed: {sum(1 for e in events if e.event_type == 'claimed')}")
    print(f"  Downloaded: {sum(1 for e in events if e.event_type == 'downloaded')}")
    print(f"  Completed: {sum(1 for e in events if e.event_type == 'completed')}")
    print(f"  Failed: {sum(1 for e in events if e.event_type == 'failed')}")
    print(f"  Timeout: {sum(1 for e in events if e.event_type == 'timeout')}")
    print(f"  Round advances: {len(round_advances)}")

    # Analyze download times
    download_times = [e.download_time_ms for e in events if e.event_type == 'downloaded' and e.download_time_ms]
    if download_times:
        download_times.sort()
        n = len(download_times)
        print(f"\nDownload time stats (n={n}):")
        print(f"  Min: {download_times[0]}ms")
        print(f"  Median: {download_times[n//2]}ms")
        print(f"  p95: {download_times[int(n*0.95)]}ms")
        print(f"  p99: {download_times[int(n*0.99)]}ms")
        print(f"  Max: {download_times[-1]}ms")

        # Find very slow chunks (>5000ms)
        slow_chunks = [e for e in events if e.event_type == 'downloaded' and e.download_time_ms and e.download_time_ms > 5000]
        print(f"\nVery slow chunks (>5000ms): {len(slow_chunks)}")
        for e in slow_chunks[:20]:
            print(f"  {e.timestamp.strftime('%H:%M:%S')} chunk {e.chunk_id}: {e.download_time_ms}ms from {e.peer}")


def analyze_peer_disconnects(logfile: str) -> None:
    """Analyze peer disconnection patterns during stalls."""
    print("\n" + "=" * 80)
    print("PEER DISCONNECT ANALYSIS")
    print("=" * 80)

    # Look for task_ended, peer stopped, channel errors
    task_ended = []
    channel_stopped = []
    channel_timeout = []

    task_ended_pattern = r'\[block_download\] Peer ([\d.:]+).*exited main loop.*chunks=(\d+)'
    stopped_pattern = r'\[block_download\] Peer ([\d.:]+).*(stopped|FAILED)'
    timeout_pattern = r'channel_timeout|Timeout waiting for'

    with open(logfile, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            if not ts:
                continue

            if match := re.search(task_ended_pattern, line):
                task_ended.append((ts, match.group(1), int(match.group(2))))
            if 'channel_stopped' in line:
                channel_stopped.append((ts, line.strip()[:100]))
            elif match := re.search(stopped_pattern, line):
                channel_stopped.append((ts, line.strip()[:100]))
            if re.search(timeout_pattern, line):
                channel_timeout.append((ts, line.strip()[:100]))

    print(f"\nTask ended events: {len(task_ended)}")
    print(f"Channel stopped events: {len(channel_stopped)}")
    print(f"Timeout events: {len(channel_timeout)}")

    # Group by minute to see if disconnects cluster
    if task_ended:
        by_minute: dict[str, list] = defaultdict(list)
        for ts, peer, chunks in task_ended:
            minute = ts.strftime('%H:%M')
            by_minute[minute].append((ts, peer, chunks))

        # Find minutes with many disconnects
        busy_minutes = [(m, events) for m, events in by_minute.items() if len(events) >= 3]
        print(f"\nMinutes with 3+ disconnects: {len(busy_minutes)}")
        for minute, events in sorted(busy_minutes, key=lambda x: len(x[1]), reverse=True)[:10]:
            print(f"  {minute}: {len(events)} disconnects")
            for ts, peer, chunks in events[:3]:
                print(f"    {ts.strftime('%H:%M:%S')} {peer} (chunks={chunks})")


def analyze_batch_request_timing(logfile: str) -> None:
    """Analyze timing of batch block requests."""
    print("\n" + "=" * 80)
    print("BATCH REQUEST TIMING")
    print("=" * 80)

    # Look for request_blocks_batch timing logs
    # [protocol] Received N blocks in batch from [peer] (send=Xus net=Yus deser=Zus)
    batch_pattern = r'\[protocol\] Received (\d+) blocks in batch from \[([\d.:]+)\] \(send=(\d+)us net=(\d+)us deser=(\d+)us\)'

    batches = []

    with open(logfile, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            if not ts:
                continue

            if match := re.search(batch_pattern, line):
                batches.append({
                    'timestamp': ts,
                    'blocks': int(match.group(1)),
                    'peer': match.group(2),
                    'send_us': int(match.group(3)),
                    'net_us': int(match.group(4)),
                    'deser_us': int(match.group(5))
                })

    if not batches:
        print("No batch timing data found")
        return

    print(f"\nTotal batches: {len(batches)}")

    # Calculate stats
    net_times = [b['net_us'] for b in batches]
    deser_times = [b['deser_us'] for b in batches]

    net_times.sort()
    deser_times.sort()
    n = len(net_times)

    print(f"\nNetwork wait time (n={n}):")
    print(f"  Min: {net_times[0]/1000:.1f}ms")
    print(f"  Median: {net_times[n//2]/1000:.1f}ms")
    print(f"  p95: {net_times[int(n*0.95)]/1000:.1f}ms")
    print(f"  p99: {net_times[int(n*0.99)]/1000:.1f}ms")
    print(f"  Max: {net_times[-1]/1000:.1f}ms")

    print(f"\nDeserialize time:")
    print(f"  Min: {deser_times[0]/1000:.1f}ms")
    print(f"  Median: {deser_times[n//2]/1000:.1f}ms")
    print(f"  p95: {deser_times[int(n*0.95)]/1000:.1f}ms")
    print(f"  p99: {deser_times[int(n*0.99)]/1000:.1f}ms")
    print(f"  Max: {deser_times[-1]/1000:.1f}ms")

    # Find slow batches
    slow_batches = [b for b in batches if b['net_us'] > 5_000_000]  # >5 seconds
    print(f"\nVery slow batches (net >5s): {len(slow_batches)}")
    for b in slow_batches[:10]:
        total_ms = (b['net_us'] + b['deser_us']) / 1000
        print(f"  {b['timestamp'].strftime('%H:%M:%S')}: net={b['net_us']/1000:.0f}ms deser={b['deser_us']/1000:.0f}ms total={total_ms:.0f}ms {b['peer']}")


def analyze_log_gaps(logfile: str) -> None:
    """Find gaps in logging that might indicate io_context blocking."""
    print("\n" + "=" * 80)
    print("LOG GAP ANALYSIS (IO_CONTEXT BLOCKING)")
    print("=" * 80)

    last_ts = None
    gaps = []

    with open(logfile, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            if not ts:
                continue

            if last_ts:
                gap = (ts - last_ts).total_seconds()
                if gap > 1.0:  # Gap > 1 second
                    gaps.append({
                        'timestamp': ts,
                        'gap': gap,
                        'before_line': prev_line[:80] if prev_line else '',
                        'after_line': line[:80]
                    })

            last_ts = ts
            prev_line = line

    print(f"\nFound {len(gaps)} gaps > 1 second")

    # Distribution of gaps
    gap_buckets = {
        '1-2s': 0,
        '2-5s': 0,
        '5-10s': 0,
        '10-30s': 0,
        '30s+': 0
    }

    for g in gaps:
        if g['gap'] < 2:
            gap_buckets['1-2s'] += 1
        elif g['gap'] < 5:
            gap_buckets['2-5s'] += 1
        elif g['gap'] < 10:
            gap_buckets['5-10s'] += 1
        elif g['gap'] < 30:
            gap_buckets['10-30s'] += 1
        else:
            gap_buckets['30s+'] += 1

    print("\nGap distribution:")
    for bucket, count in gap_buckets.items():
        print(f"  {bucket}: {count}")

    # Show largest gaps
    gaps.sort(key=lambda x: x['gap'], reverse=True)
    print("\nLargest gaps (top 15):")
    for g in gaps[:15]:
        print(f"  {g['timestamp'].strftime('%H:%M:%S')} gap={g['gap']:.1f}s")
        print(f"    After: {g['after_line'].strip()[:70]}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]
    print(f"Analyzing {logfile}...")

    analyze_chunk_events(logfile)
    analyze_peer_disconnects(logfile)
    analyze_batch_request_timing(logfile)
    analyze_log_gaps(logfile)


if __name__ == '__main__':
    main()

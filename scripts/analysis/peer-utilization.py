#!/usr/bin/env python3
"""Analyze peer utilization from debug.log"""

import sys
import re
from datetime import datetime, timedelta
from collections import defaultdict

def parse_timestamp(line):
    match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})', line)
    if match:
        return datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S')
    return None

def main():
    if len(sys.argv) < 2:
        print("Usage: peer-utilization.py <debug.log>")
        sys.exit(1)

    logfile = sys.argv[1]

    # Track peer activity
    peer_sessions = {}  # peer_addr -> {start, end, chunks, task_count}
    active_peers_timeline = []  # (timestamp, active_count, event)
    task_starts = []
    task_ends = []
    zero_chunk_tasks = 0
    total_tasks = 0

    # Track connected peers over time
    connected_peers = set()
    downloading_peers = set()

    with open(logfile, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            if not ts:
                continue

            # Task started
            if 'Task started for peer' in line:
                match = re.search(r'Task started for peer ([^\s]+) .* \(active peers: (\d+)\)', line)
                if match:
                    peer_addr = match.group(1)
                    active_count = int(match.group(2))
                    total_tasks += 1
                    task_starts.append((ts, peer_addr, active_count))
                    downloading_peers.add(peer_addr)
                    active_peers_timeline.append((ts, active_count, 'start', peer_addr))

            # Task ended
            elif 'Task ended for peer' in line:
                match = re.search(r'Task ended for peer ([^\s]+) .* \(downloaded (\d+) chunks, active peers: (\d+)\)', line)
                if match:
                    peer_addr = match.group(1)
                    chunks = int(match.group(2))
                    active_count = int(match.group(3))
                    task_ends.append((ts, peer_addr, chunks, active_count))
                    downloading_peers.discard(peer_addr)
                    active_peers_timeline.append((ts, active_count, 'end', peer_addr))
                    if chunks == 0:
                        zero_chunk_tasks += 1

                    if peer_addr not in peer_sessions:
                        peer_sessions[peer_addr] = {'chunks': 0, 'task_count': 0}
                    peer_sessions[peer_addr]['chunks'] += chunks
                    peer_sessions[peer_addr]['task_count'] += 1

            # Peer connected
            elif 'Handshake complete' in line or 'perform_handshake_direct' in line:
                match = re.search(r'\[([0-9a-f.:]+:\d+)\]', line)
                if match:
                    connected_peers.add(match.group(1))

    # Analysis
    print("=" * 80)
    print("PEER UTILIZATION ANALYSIS")
    print("=" * 80)
    print()

    print(f"Total download tasks started: {total_tasks}")
    print(f"Tasks with 0 chunks downloaded: {zero_chunk_tasks} ({100*zero_chunk_tasks/max(1,total_tasks):.1f}%)")
    print()

    # Time spent at each active peer count
    if active_peers_timeline:
        print("TIME SPENT AT EACH ACTIVE PEER COUNT:")
        print("-" * 40)
        time_at_count = defaultdict(timedelta)
        sorted_timeline = sorted(active_peers_timeline, key=lambda x: x[0])

        for i in range(len(sorted_timeline) - 1):
            ts, count, event, peer = sorted_timeline[i]
            next_ts = sorted_timeline[i+1][0]
            duration = next_ts - ts
            time_at_count[count] += duration

        total_time = sum(time_at_count.values(), timedelta())
        for count in sorted(time_at_count.keys()):
            pct = 100 * time_at_count[count].total_seconds() / max(1, total_time.total_seconds())
            print(f"  {count} active peers: {time_at_count[count]} ({pct:.1f}%)")
        print()

    # Gaps between task activity
    print("GAPS IN DOWNLOAD ACTIVITY (> 5 minutes):")
    print("-" * 60)
    all_events = sorted(task_starts + [(ts, peer, 0, count) for ts, peer, chunks, count in task_ends], key=lambda x: x[0])
    gap_count = 0
    for i in range(len(all_events) - 1):
        ts1 = all_events[i][0]
        ts2 = all_events[i+1][0]
        gap = (ts2 - ts1).total_seconds()
        if gap > 300:  # 5 minutes
            gap_count += 1
            print(f"  {ts1} to {ts2}: {gap/60:.1f} min gap")
    if gap_count == 0:
        print("  (none)")
    print()

    # Top peers by chunks downloaded
    print("TOP 10 PEERS BY CHUNKS DOWNLOADED:")
    print("-" * 60)
    sorted_peers = sorted(peer_sessions.items(), key=lambda x: x[1]['chunks'], reverse=True)
    for peer, data in sorted_peers[:10]:
        print(f"  {peer}: {data['chunks']:,} chunks in {data['task_count']} tasks")
    print()

    # Peers with no productive work
    print("PEERS WITH NO PRODUCTIVE WORK:")
    print("-" * 60)
    unproductive = [(p, d) for p, d in peer_sessions.items() if d['chunks'] == 0]
    print(f"  {len(unproductive)} peers had download tasks but downloaded 0 chunks")
    print()

    # Utilization over time (by hour)
    print("ACTIVE PEERS BY HOUR:")
    print("-" * 60)
    hourly_data = defaultdict(list)
    for ts, count, event, peer in active_peers_timeline:
        hour = ts.replace(minute=0, second=0, microsecond=0)
        hourly_data[hour].append(count)

    for hour in sorted(hourly_data.keys()):
        counts = hourly_data[hour]
        avg = sum(counts) / len(counts)
        min_c = min(counts)
        max_c = max(counts)
        print(f"  {hour}: avg={avg:.1f}, min={min_c}, max={max_c} active peers")

if __name__ == '__main__':
    main()

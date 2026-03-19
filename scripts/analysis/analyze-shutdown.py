#!/usr/bin/env python3
"""
Analyze p2p_node shutdown sequence from debug.log to find stuck peers.
"""

import re
import sys
from collections import defaultdict
from datetime import datetime

def parse_timestamp(line):
    """Extract timestamp from log line."""
    match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]', line)
    if match:
        return datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f')
    return None

def extract_peer(line):
    """Extract peer address from log line."""
    match = re.search(r'\[(\d+\.\d+\.\d+\.\d+:\d+)\]', line)
    if match:
        return match.group(1)
    return None

def main():
    log_file = sys.argv[1] if len(sys.argv) > 1 else '/home/fernando/dev/kth/kth-mono/debug.log'

    # Track peer lifecycle events
    peers = defaultdict(lambda: {
        'run_started': False,
        'run_completed': False,
        'lambda_completed': False,
        'full_lifecycle': False,
        'handshake_done': False,
        'protocols_started': False,
        'protocols_breaking': False,
        'stop_called': False,
        'channels_closed': False,
        'events': []
    })

    # Track global events
    shutdown_time = None
    waiting_join = None
    join_completed = None

    with open(log_file, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            peer = extract_peer(line)

            # Global shutdown events
            if 'Waiting for peer_tasks.join()' in line:
                waiting_join = ts

            if 'peer_supervisor finished' in line:
                join_completed = ts

            if 'peer_supervisor waiting for' in line:
                shutdown_time = ts
                match = re.search(r'waiting for (\d+) active', line)
                if match:
                    print(f"\n=== SHUTDOWN STARTED at {ts} ===")
                    print(f"Active peer tasks: {match.group(1)}")

            # Per-peer events
            if peer:
                if 'run() starting' in line:
                    peers[peer]['run_started'] = True
                    peers[peer]['events'].append(('run_started', ts))

                if 'run() completed' in line:
                    peers[peer]['run_completed'] = True
                    peers[peer]['events'].append(('run_completed', ts))

                if 'lambda completed' in line:
                    peers[peer]['lambda_completed'] = True
                    peers[peer]['events'].append(('lambda_completed', ts))

                if 'full lifecycle completed' in line:
                    peers[peer]['full_lifecycle'] = True
                    peers[peer]['events'].append(('full_lifecycle', ts))

                if 'Starting protocols for' in line:
                    peers[peer]['protocols_started'] = True
                    peers[peer]['events'].append(('protocols_started', ts))

                if 'Breaking due to stopped' in line:
                    peers[peer]['protocols_breaking'] = True
                    peers[peer]['events'].append(('protocols_breaking', ts))

                if 'CONNECTED:' in line:
                    peers[peer]['handshake_done'] = True
                    peers[peer]['events'].append(('connected', ts))

                if 'stop() checkpoint 1: entering' in line:
                    peers[peer]['stop_called'] = True
                    peers[peer]['events'].append(('stop_called', ts))

                if 'checkpoint 12: all channels closed' in line:
                    peers[peer]['channels_closed'] = True
                    peers[peer]['events'].append(('channels_closed', ts))

    print(f"\n=== PEER LIFECYCLE ANALYSIS ===")
    print(f"Total peers seen: {len(peers)}\n")

    # Find peers that didn't complete their lifecycle
    stuck_peers = []
    completed_peers = []

    for peer, state in peers.items():
        if state['run_completed'] and not state['lambda_completed']:
            stuck_peers.append((peer, state))
        elif state['full_lifecycle']:
            completed_peers.append((peer, state))

    print(f"Peers with completed lifecycle: {len(completed_peers)}")
    print(f"Peers stuck (run completed, lambda not): {len(stuck_peers)}")

    if stuck_peers:
        print(f"\n=== STUCK PEERS ANALYSIS ===")
        for peer, state in stuck_peers:
            print(f"\n{peer}:")
            print(f"  run_started:       {state['run_started']}")
            print(f"  run_completed:     {state['run_completed']}")
            print(f"  handshake_done:    {state['handshake_done']}")
            print(f"  protocols_started: {state['protocols_started']}")
            print(f"  protocols_breaking: {state['protocols_breaking']}")
            print(f"  lambda_completed:  {state['lambda_completed']}")
            print(f"  stop_called:       {state['stop_called']}")
            print(f"  channels_closed:   {state['channels_closed']}")
            print(f"  Timeline:")
            for event, ts in state['events']:
                print(f"    {ts}: {event}")

    print(f"\n=== SHUTDOWN SUMMARY ===")
    print(f"Shutdown started: {shutdown_time}")
    print(f"Waiting for join: {waiting_join}")
    print(f"Join completed:   {join_completed or 'NEVER (STUCK)'}")

    if waiting_join and not join_completed:
        print(f"\n*** DEADLOCK DETECTED: peer_tasks.join() never completed ***")
        print(f"*** {len(stuck_peers)} peer tasks are stuck ***")

if __name__ == '__main__':
    main()

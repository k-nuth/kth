#!/usr/bin/env python3
"""
Analyze peer download performance from debug.log.
Identifies slow peers and patterns of degradation.
"""

import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional


@dataclass
class PeerSession:
    """One download session for a peer (from task start to exit)."""
    peer: str
    start_time: Optional[datetime] = None
    end_time: Optional[datetime] = None
    chunks_downloaded: int = 0
    chunks_failed: int = 0
    download_times: list[int] = field(default_factory=list)

    @property
    def min_ms(self) -> int:
        return min(self.download_times) if self.download_times else 0

    @property
    def max_ms(self) -> int:
        return max(self.download_times) if self.download_times else 0

    @property
    def avg_ms(self) -> float:
        return sum(self.download_times) / len(self.download_times) if self.download_times else 0

    @property
    def median_ms(self) -> int:
        if not self.download_times:
            return 0
        s = sorted(self.download_times)
        return s[len(s) // 2]


@dataclass
class PeerStats:
    """Aggregate stats for a peer across all sessions."""
    peer: str
    sessions: list[PeerSession] = field(default_factory=list)
    total_chunks: int = 0
    total_failures: int = 0
    all_times: list[int] = field(default_factory=list)

    @property
    def session_count(self) -> int:
        return len(self.sessions)

    @property
    def avg_ms(self) -> float:
        return sum(self.all_times) / len(self.all_times) if self.all_times else 0

    @property
    def max_ms(self) -> int:
        return max(self.all_times) if self.all_times else 0

    @property
    def slow_chunk_count(self) -> int:
        """Chunks that took >5 seconds."""
        return sum(1 for t in self.all_times if t > 5000)

    @property
    def slow_chunk_pct(self) -> float:
        return self.slow_chunk_count / len(self.all_times) * 100 if self.all_times else 0


def parse_timestamp(line: str) -> Optional[datetime]:
    match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]', line)
    if match:
        return datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f')
    return None


def extract_peer(line: str) -> Optional[str]:
    """Extract peer address from log line."""
    match = re.search(r'Peer ([\d.]+:\d+)', line)
    if match:
        return match.group(1)
    return None


def analyze_peers(logfile: str) -> dict[str, PeerStats]:
    """Analyze all peer performance from log."""

    # Pattern for chunk download times
    download_pattern = r'Peer ([\d.]+:\d+).*downloaded \d+ blocks for chunk \d+ in (\d+)ms'
    failed_pattern = r'Peer ([\d.]+:\d+).*FAILED chunk'
    stats_pattern = r'\[download\] Peer ([\d.]+:\d+).*stats \(n=(\d+)\)'

    peers: dict[str, PeerStats] = {}
    active_sessions: dict[str, PeerSession] = {}  # peer -> current session

    with open(logfile, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)

            # Track individual chunk downloads
            if match := re.search(download_pattern, line):
                peer = match.group(1)
                time_ms = int(match.group(2))

                if peer not in peers:
                    peers[peer] = PeerStats(peer)
                if peer not in active_sessions:
                    active_sessions[peer] = PeerSession(peer, start_time=ts)

                active_sessions[peer].download_times.append(time_ms)
                peers[peer].all_times.append(time_ms)

            # Track failures
            elif match := re.search(failed_pattern, line):
                peer = match.group(1)
                if peer in active_sessions:
                    active_sessions[peer].chunks_failed += 1
                if peer in peers:
                    peers[peer].total_failures += 1

            # Session end (stats line)
            elif match := re.search(stats_pattern, line):
                peer = match.group(1)
                chunks = int(match.group(2))

                if peer in active_sessions:
                    session = active_sessions[peer]
                    session.end_time = ts
                    session.chunks_downloaded = chunks

                    if peer not in peers:
                        peers[peer] = PeerStats(peer)
                    peers[peer].sessions.append(session)
                    peers[peer].total_chunks += chunks

                    del active_sessions[peer]

    return peers


def print_report(peers: dict[str, PeerStats]) -> None:
    """Print analysis report."""

    print("=" * 90)
    print("PEER PERFORMANCE ANALYSIS")
    print("=" * 90)

    # Sort by average download time (slowest first)
    sorted_peers = sorted(peers.values(), key=lambda p: p.avg_ms, reverse=True)

    print(f"\nTotal peers analyzed: {len(peers)}")

    # Summary stats
    all_times = []
    for p in peers.values():
        all_times.extend(p.all_times)

    if all_times:
        all_times.sort()
        n = len(all_times)
        print(f"\nOverall chunk download times (n={n}):")
        print(f"  Min: {all_times[0]}ms")
        print(f"  Median: {all_times[n//2]}ms")
        print(f"  p95: {all_times[int(n*0.95)]}ms")
        print(f"  p99: {all_times[int(n*0.99)]}ms")
        print(f"  Max: {all_times[-1]}ms")

    # Slow peers (avg > 1000ms)
    slow_peers = [p for p in sorted_peers if p.avg_ms > 1000]
    print(f"\n" + "=" * 90)
    print(f"SLOW PEERS (avg > 1000ms): {len(slow_peers)}")
    print("=" * 90)

    print(f"\n{'Peer':<25} {'Sessions':>8} {'Chunks':>8} {'Avg(ms)':>10} {'Max(ms)':>10} {'Slow%':>8} {'Fails':>6}")
    print("-" * 90)

    for p in slow_peers[:30]:
        print(f"{p.peer:<25} {p.session_count:>8} {p.total_chunks:>8} {p.avg_ms:>10.0f} {p.max_ms:>10} {p.slow_chunk_pct:>7.1f}% {p.total_failures:>6}")

    # Problematic peers (high failure rate or very slow)
    print(f"\n" + "=" * 90)
    print("PROBLEMATIC PEERS (>10% slow chunks or failures)")
    print("=" * 90)

    problematic = [p for p in sorted_peers if p.slow_chunk_pct > 10 or p.total_failures > 0]

    print(f"\n{'Peer':<25} {'Chunks':>8} {'SlowCnt':>8} {'Slow%':>8} {'Fails':>6} {'MaxMs':>10}")
    print("-" * 90)

    for p in sorted(problematic, key=lambda x: x.slow_chunk_pct, reverse=True)[:30]:
        print(f"{p.peer:<25} {len(p.all_times):>8} {p.slow_chunk_count:>8} {p.slow_chunk_pct:>7.1f}% {p.total_failures:>6} {p.max_ms:>10}")

    # Fast peers (for comparison)
    print(f"\n" + "=" * 90)
    print("FAST PEERS (avg < 300ms, >50 chunks)")
    print("=" * 90)

    fast_peers = [p for p in sorted_peers if p.avg_ms < 300 and len(p.all_times) > 50]
    fast_peers.sort(key=lambda p: p.avg_ms)

    print(f"\n{'Peer':<25} {'Chunks':>8} {'Avg(ms)':>10} {'Median':>10} {'Max(ms)':>10}")
    print("-" * 90)

    for p in fast_peers[:20]:
        median = sorted(p.all_times)[len(p.all_times)//2] if p.all_times else 0
        print(f"{p.peer:<25} {len(p.all_times):>8} {p.avg_ms:>10.0f} {median:>10} {p.max_ms:>10}")

    # Degradation analysis
    print(f"\n" + "=" * 90)
    print("DEGRADATION ANALYSIS (peers that got slower over time)")
    print("=" * 90)

    degraded = []
    for p in peers.values():
        if len(p.all_times) < 20:
            continue
        # Compare first 10 vs last 10 chunks
        first_avg = sum(p.all_times[:10]) / 10
        last_avg = sum(p.all_times[-10:]) / 10
        if last_avg > first_avg * 3:  # Got 3x slower
            degraded.append((p, first_avg, last_avg))

    degraded.sort(key=lambda x: x[2] / x[1], reverse=True)

    print(f"\n{'Peer':<25} {'Chunks':>8} {'First10':>10} {'Last10':>10} {'Ratio':>8}")
    print("-" * 90)

    for p, first, last in degraded[:20]:
        ratio = last / first if first > 0 else 0
        print(f"{p.peer:<25} {len(p.all_times):>8} {first:>10.0f} {last:>10.0f} {ratio:>7.1f}x")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <debug.log>")
        sys.exit(1)

    print(f"Analyzing {sys.argv[1]}...")
    peers = analyze_peers(sys.argv[1])
    print_report(peers)


if __name__ == '__main__':
    main()

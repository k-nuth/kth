#!/usr/bin/env python3
"""
Sync Performance Analyzer
Analyzes debug.log to measure sync performance over time.
"""

import re
import sys
from datetime import datetime
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Optional

@dataclass
class SyncSnapshot:
    timestamp: datetime
    height: int
    target: int
    blk_per_sec: int
    peers: int
    pending: int
    # Validation stats
    organize_avg_ms: float = 0.0
    organize_p95_ms: float = 0.0
    organize_max_ms: float = 0.0
    merkle_avg_ms: float = 0.0
    push_avg_ms: float = 0.0
    # Storage stats
    storage_write_avg_ms: float = 0.0
    storage_write_mbps: float = 0.0
    storage_open_avg_us: float = 0.0
    storage_alloc_mb: float = 0.0
    # Download stats (aggregated)
    download_samples: int = 0
    download_total_ms: float = 0.0
    download_total_blocks: int = 0
    # Channel wait timing (new)
    validation_wait_avg_ms: float = 0.0
    validation_wait_pct: float = 0.0
    bridge_recv_wait_ms: float = 0.0
    bridge_send_wait_ms: float = 0.0
    # Download timing (deserialization vs network)
    block_deser_avg_ms: float = 0.0
    block_net_wait_avg_ms: float = 0.0
    # Block statistics
    total_txs: int = 0
    total_bytes: int = 0
    avg_txs_per_block: float = 0.0
    avg_bytes_per_block: float = 0.0
    # Pipeline latency (network vs channel overhead)
    pipeline_download_task_ms: float = 0.0
    pipeline_channels_ms: float = 0.0
    pipeline_total_ms: float = 0.0

@dataclass
class DownloadSample:
    timestamp: datetime
    peer: str
    blocks: int
    time_ms: float

def parse_timestamp(ts_str: str) -> datetime:
    return datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")

def parse_log(log_path: str):
    snapshots = []
    current = None
    download_buffer = []  # Buffer downloads between snapshots

    # Patterns
    sync_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[block_sync:fast\]\s+'
        r'(\d+)/(\d+)\s+\((\d+)\s+blk/s.*\|\s+(\d+)\s+peers\s+\|\s+pending:\s+(\d+)'
    )

    organize_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[validation\]\s+organize\(\)\s+stats.*'
        r'avg=([\d.]+)ms.*p95=([\d.]+)ms.*max=([\d.]+)ms'
    )

    fastmode_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[validation\]\s+height\s+\d+\s+fast\s+mode\s+avg:\s+'
        r'merkle=([\d.]+)ms.*push=([\d.]+)ms'
    )

    storage_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[block_storage\].*'
        r'alloc:.*total=([\d.]+)MB.*write:.*avg=([\d.]+)ms\s+([\d.]+)MB/s.*open:.*avg=([\d.]+)us'
    )

    download_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[block_download\]\s+Peer\s+(\S+).*'
        r'downloaded\s+(\d+)\s+blocks.*in\s+(\d+)ms'
    )

    # New timing patterns
    validation_wait_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[validation\]\s+channel_wait:\s+'
        r'avg=([\d.]+)ms/blk\s+\(([\d.]+)%'
    )

    bridge_timing_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[block_bridge\]\s+timing:\s+'
        r'recv_wait=([\d.]+)ms/blk\s+send_wait=([\d.]+)ms/blk'
    )

    download_timing_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[download_timing\]\s+'
        r'block\s+deser=([\d.]+)ms/blk\s+net_wait=([\d.]+)ms/blk'
    )

    block_stats_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[block_stats\]\s+'
        r'window:\s+\d+\s+blocks,\s+(\d+)\s+txs\s+\(([\d.]+)\s+txs/blk\),\s+'
        r'([\d.]+)\s+MB\s+\(([\d.]+)\s+bytes/blk\)'
    )

    pipeline_latency_pattern = re.compile(
        r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*\[pipeline_latency\]\s+'
        r'download_task=([\d.]+)ms.*channels=([\d.]+)ms.*total=([\d.]+)ms'
    )

    with open(log_path, 'r') as f:
        for line in f:
            # Check for sync progress (creates new snapshot)
            m = sync_pattern.search(line)
            if m:
                # Save previous snapshot with download stats
                if current:
                    if download_buffer:
                        current.download_samples = len(download_buffer)
                        current.download_total_ms = sum(d.time_ms for d in download_buffer)
                        current.download_total_blocks = sum(d.blocks for d in download_buffer)
                    snapshots.append(current)
                    download_buffer = []

                current = SyncSnapshot(
                    timestamp=parse_timestamp(m.group(1)),
                    height=int(m.group(2)),
                    target=int(m.group(3)),
                    blk_per_sec=int(m.group(4)),
                    peers=int(m.group(5)),
                    pending=int(m.group(6))
                )
                continue

            if not current:
                continue

            # Organize stats
            m = organize_pattern.search(line)
            if m:
                current.organize_avg_ms = float(m.group(2))
                current.organize_p95_ms = float(m.group(3))
                current.organize_max_ms = float(m.group(4))
                continue

            # Fast mode breakdown
            m = fastmode_pattern.search(line)
            if m:
                current.merkle_avg_ms = float(m.group(2))
                current.push_avg_ms = float(m.group(3))
                continue

            # Storage stats
            m = storage_pattern.search(line)
            if m:
                current.storage_alloc_mb = float(m.group(2))
                current.storage_write_avg_ms = float(m.group(3))
                current.storage_write_mbps = float(m.group(4))
                current.storage_open_avg_us = float(m.group(5))
                continue

            # Download stats
            m = download_pattern.search(line)
            if m:
                download_buffer.append(DownloadSample(
                    timestamp=parse_timestamp(m.group(1)),
                    peer=m.group(2),
                    blocks=int(m.group(3)),
                    time_ms=float(m.group(4))
                ))
                continue

            # Validation channel wait timing
            m = validation_wait_pattern.search(line)
            if m:
                current.validation_wait_avg_ms = float(m.group(2))
                current.validation_wait_pct = float(m.group(3))
                continue

            # Block bridge timing
            m = bridge_timing_pattern.search(line)
            if m:
                current.bridge_recv_wait_ms = float(m.group(2))
                current.bridge_send_wait_ms = float(m.group(3))
                continue

            # Download timing (deserialize vs network)
            m = download_timing_pattern.search(line)
            if m:
                current.block_deser_avg_ms = float(m.group(2))
                current.block_net_wait_avg_ms = float(m.group(3))
                continue

            # Block statistics
            m = block_stats_pattern.search(line)
            if m:
                current.total_txs = int(m.group(2))
                current.avg_txs_per_block = float(m.group(3))
                current.total_bytes = int(float(m.group(4)) * 1_000_000)  # MB to bytes
                current.avg_bytes_per_block = float(m.group(5))
                continue

            # Pipeline latency (network vs channel overhead)
            m = pipeline_latency_pattern.search(line)
            if m:
                current.pipeline_download_task_ms = float(m.group(2))
                current.pipeline_channels_ms = float(m.group(3))
                current.pipeline_total_ms = float(m.group(4))

    # Don't forget last snapshot
    if current:
        if download_buffer:
            current.download_samples = len(download_buffer)
            current.download_total_ms = sum(d.time_ms for d in download_buffer)
            current.download_total_blocks = sum(d.blocks for d in download_buffer)
        snapshots.append(current)

    return snapshots

def analyze_snapshots(snapshots: list[SyncSnapshot]):
    if len(snapshots) < 2:
        print("Not enough data points for analysis")
        return

    print("=" * 120)
    print("SYNC PERFORMANCE ANALYSIS")
    print("=" * 120)
    print(f"Time range: {snapshots[0].timestamp} to {snapshots[-1].timestamp}")
    print(f"Height range: {snapshots[0].height:,} to {snapshots[-1].height:,}")
    total_time = (snapshots[-1].timestamp - snapshots[0].timestamp).total_seconds()
    total_blocks = snapshots[-1].height - snapshots[0].height
    print(f"Overall rate: {total_blocks / total_time:.1f} blk/s over {total_time:.1f}s")
    print()

    # Legend
    print("COLUMN LEGEND:")
    print("  Height    : Block height at end of 1000-block window")
    print("  Rate      : Blocks per second (blk/s)")
    print("  Pend      : Blocks waiting in validation queue")
    print("  Organize  : Time per block for full organize() call (includes merkle+store+overhead)")
    print("  Merkle    : Time per block calculating merkle root")
    print("  Store     : Time per block storing to disk")
    print("  Deser     : Time per block deserializing (block::from_data)")
    print("  NetWait   : Time per block waiting for network data")
    print("  DlTask    : Pipeline latency in download task (from net receive to channel send)")
    print("  Channels  : Pipeline latency in channels (supervisor→bridge→validation)")
    print("  Txs/Blk   : Average transactions per block")
    print("  KB/Blk    : Average block size in kilobytes")
    print()

    # Timeline header
    print("-" * 165)
    print(f"{'Height':>8} | {'Rate':>7} | {'Pend':>4} | "
          f"{'Organize':>8} | {'Merkle':>7} {'Store':>7} | "
          f"{'Deser':>7} {'NetWait':>7} | "
          f"{'DlTask':>7} {'Channels':>8} | "
          f"{'Txs/Blk':>7} {'KB/Blk':>7}")
    print(f"{'':>8} | {'blk/s':>7} | {'':>4} | "
          f"{'ms/blk':>8} | {'ms/blk':>7} {'ms/blk':>7} | "
          f"{'ms/blk':>7} {'ms/blk':>7} | "
          f"{'ms/blk':>7} {'ms/blk':>8} | "
          f"{'':>7} {'':>7}")
    print("-" * 165)

    prev = None
    for snap in snapshots:
        if prev:
            delta_time = (snap.timestamp - prev.timestamp).total_seconds()
            delta_blocks = snap.height - prev.height

            if delta_time > 0 and delta_blocks > 0:
                actual_rate = delta_blocks / delta_time
                kb_per_block = snap.avg_bytes_per_block / 1024.0

                print(f"{snap.height:>8,} | {actual_rate:>7.0f} | {snap.pending:>4} | "
                      f"{snap.organize_avg_ms:>8.3f} | "
                      f"{snap.merkle_avg_ms:>7.3f} {snap.push_avg_ms:>7.3f} | "
                      f"{snap.block_deser_avg_ms:>7.3f} {snap.block_net_wait_avg_ms:>7.3f} | "
                      f"{snap.pipeline_download_task_ms:>7.3f} {snap.pipeline_channels_ms:>8.3f} | "
                      f"{snap.avg_txs_per_block:>7.1f} {kb_per_block:>7.1f}")

        prev = snap

    print("-" * 165)
    print()

    # Summary statistics by height ranges
    print("=" * 120)
    print("SUMMARY BY HEIGHT RANGE (every 50k blocks)")
    print("=" * 120)

    ranges = defaultdict(list)
    for snap in snapshots:
        range_key = (snap.height // 50000) * 50000
        ranges[range_key].append(snap)

    print(f"{'Range':>15} | {'N':>4} | {'Rate':>7} | "
          f"{'Organize':>8} | {'Merkle':>7} {'Store':>7} | "
          f"{'Deser':>7} {'NetWait':>7} | "
          f"{'DlTask':>7} {'Channels':>8} | "
          f"{'Txs/Blk':>7} {'KB/Blk':>7}")
    print("-" * 145)

    for range_start in sorted(ranges.keys()):
        snaps = ranges[range_start]
        if len(snaps) < 2:
            continue

        avg_rate = sum(s.blk_per_sec for s in snaps) / len(snaps)
        avg_org = sum(s.organize_avg_ms for s in snaps) / len(snaps)
        avg_merkle = sum(s.merkle_avg_ms for s in snaps) / len(snaps)
        avg_push = sum(s.push_avg_ms for s in snaps) / len(snaps)
        avg_deser = sum(s.block_deser_avg_ms for s in snaps) / len(snaps)
        avg_net_wait = sum(s.block_net_wait_avg_ms for s in snaps) / len(snaps)
        avg_dl_task = sum(s.pipeline_download_task_ms for s in snaps) / len(snaps)
        avg_channels = sum(s.pipeline_channels_ms for s in snaps) / len(snaps)
        avg_txs = sum(s.avg_txs_per_block for s in snaps) / len(snaps)
        avg_kb = sum(s.avg_bytes_per_block for s in snaps) / len(snaps) / 1024.0

        print(f"{range_start:>7,}-{range_start+50000:>7,} | {len(snaps):>4} | {avg_rate:>6.0f}/s | "
              f"{avg_org:>7.2f}ms | "
              f"{avg_merkle:>6.3f}ms {avg_push:>6.3f}ms | "
              f"{avg_deser:>6.3f}ms {avg_net_wait:>6.3f}ms | "
              f"{avg_dl_task:>6.3f}ms {avg_channels:>7.3f}ms | "
              f"{avg_txs:>7.1f} {avg_kb:>7.1f}")

    print()

    # Bottleneck analysis
    print("=" * 120)
    print("BOTTLENECK ANALYSIS")
    print("=" * 120)

    # Group by bottleneck type
    net_bound = []
    val_bound = []

    prev = None
    for snap in snapshots:
        if prev:
            delta_time = (snap.timestamp - prev.timestamp).total_seconds()
            delta_blocks = snap.height - prev.height

            if delta_time > 0 and delta_blocks > 0:
                actual_rate = delta_blocks / delta_time
                validation_time_1k = snap.organize_avg_ms * 1000
                actual_time_1k = (1000 / actual_rate) * 1000

                val_pct = (validation_time_1k / actual_time_1k) * 100 if actual_time_1k > 0 else 0

                if val_pct > 60:
                    val_bound.append(snap.height)
                else:
                    net_bound.append(snap.height)
        prev = snap

    print(f"Network-bound snapshots: {len(net_bound)} ({100*len(net_bound)/(len(net_bound)+len(val_bound)):.1f}%)")
    print(f"Validation-bound snapshots: {len(val_bound)} ({100*len(val_bound)/(len(net_bound)+len(val_bound)):.1f}%)")

    if val_bound:
        print(f"\nValidation became bottleneck around heights: {val_bound[:5]}...")

    print()

def main():
    if len(sys.argv) < 2:
        log_path = "debug.log"
    else:
        log_path = sys.argv[1]

    print(f"Analyzing: {log_path}")
    print()

    snapshots = parse_log(log_path)
    print(f"Found {len(snapshots)} sync snapshots")

    if snapshots:
        analyze_snapshots(snapshots)

if __name__ == "__main__":
    main()

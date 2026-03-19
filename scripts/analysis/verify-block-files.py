#!/usr/bin/env python3
"""
Verify block files (blk*.dat) integrity and chain consistency.

This script reads all blk*.dat files and verifies:
1. File format integrity (magic bytes, sizes)
2. Block deserialization
3. Chain consistency (prev_block_hash links)
4. Sequential ordering

Usage:
    python3 verify-block-files.py /path/to/blocks/directory [--network mainnet|testnet]
"""

import os
import sys
import struct
import hashlib
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, List, Tuple
import argparse

# Disk magic bytes for BCH block files (matches BCHN for compatibility)
# Note: BCH mainnet/testnet use same disk magic as BTC for historical compatibility
MAGIC_MAINNET  = bytes([0xf9, 0xbe, 0xb4, 0xd9])  # BCH mainnet (same as BTC)
MAGIC_TESTNET  = bytes([0x0b, 0x11, 0x09, 0x07])  # BCH testnet (same as BTC)
MAGIC_TESTNET4 = bytes([0xcd, 0x22, 0xa7, 0x92])  # BCH testnet4
MAGIC_SCALENET = bytes([0xba, 0xc2, 0x2d, 0xc4])  # BCH scalenet
MAGIC_REGTEST  = bytes([0xfa, 0xbf, 0xb5, 0xda])  # BCH regtest

# Genesis block hashes (in internal byte order - reversed from display)
GENESIS_MAINNET  = bytes.fromhex('000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f')[::-1]
GENESIS_TESTNET  = bytes.fromhex('000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943')[::-1]
GENESIS_TESTNET4 = bytes.fromhex('000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b')[::-1]
GENESIS_SCALENET = bytes.fromhex('00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87571f8a4f9c72e96e32a')[::-1]
GENESIS_REGTEST  = bytes.fromhex('0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206')[::-1]

@dataclass
class BlockHeader:
    version: int
    prev_block_hash: bytes
    merkle_root: bytes
    timestamp: int
    bits: int
    nonce: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'BlockHeader':
        if len(data) < 80:
            raise ValueError(f"Header too short: {len(data)} bytes")

        version = struct.unpack('<I', data[0:4])[0]
        prev_block_hash = data[4:36]
        merkle_root = data[36:68]
        timestamp = struct.unpack('<I', data[68:72])[0]
        bits = struct.unpack('<I', data[72:76])[0]
        nonce = struct.unpack('<I', data[76:80])[0]

        return cls(version, prev_block_hash, merkle_root, timestamp, bits, nonce)

    def hash(self, raw_header: bytes) -> bytes:
        """Compute block hash (double SHA256 of header)"""
        return hashlib.sha256(hashlib.sha256(raw_header[:80]).digest()).digest()


@dataclass
class BlockInfo:
    file_num: int
    offset: int
    size: int
    block_hash: bytes
    prev_hash: bytes
    height: Optional[int] = None  # Will be computed from chain


def read_varint(data: bytes, offset: int) -> Tuple[int, int]:
    """Read a variable-length integer, return (value, new_offset)"""
    first = data[offset]
    if first < 0xfd:
        return first, offset + 1
    elif first == 0xfd:
        return struct.unpack('<H', data[offset+1:offset+3])[0], offset + 3
    elif first == 0xfe:
        return struct.unpack('<I', data[offset+1:offset+5])[0], offset + 5
    else:
        return struct.unpack('<Q', data[offset+1:offset+9])[0], offset + 9


def parse_block_file(filepath: Path, file_num: int, expected_magic: bytes) -> List[BlockInfo]:
    """Parse a single blk*.dat file and return list of blocks found."""
    blocks = []

    with open(filepath, 'rb') as f:
        data = f.read()

    offset = 0
    while offset < len(data):
        # Check if we have enough bytes for header
        if offset + 8 > len(data):
            break

        # Read magic
        magic = data[offset:offset+4]
        if magic == b'\x00\x00\x00\x00':
            # Padding/empty space - skip to next potential block
            offset += 4
            continue

        if magic != expected_magic:
            print(f"  Warning: Unexpected magic at offset {offset}: {magic.hex()}")
            # Try to find next valid magic
            next_magic = data.find(expected_magic, offset + 1)
            if next_magic == -1:
                break
            offset = next_magic
            continue

        # Read size
        block_size = struct.unpack('<I', data[offset+4:offset+8])[0]

        if block_size == 0 or block_size > 256 * 1024 * 1024:  # Max 256MB
            print(f"  Warning: Invalid block size {block_size} at offset {offset}")
            offset += 8
            continue

        # Read block data
        block_start = offset + 8
        if block_start + block_size > len(data):
            print(f"  Warning: Block at offset {offset} extends beyond file")
            break

        block_data = data[block_start:block_start + block_size]

        # Parse header
        try:
            header = BlockHeader.from_bytes(block_data)
            block_hash = header.hash(block_data[:80])

            blocks.append(BlockInfo(
                file_num=file_num,
                offset=block_start,
                size=block_size,
                block_hash=block_hash,
                prev_hash=header.prev_block_hash
            ))
        except Exception as e:
            print(f"  Warning: Failed to parse block at offset {offset}: {e}")

        offset = block_start + block_size

    return blocks


def verify_chain(blocks: List[BlockInfo], genesis_hash: bytes) -> Tuple[int, List[str]]:
    """
    Verify chain consistency by checking prev_hash links.
    Returns (valid_count, list of errors)
    """
    errors = []

    if not blocks:
        return 0, ["No blocks found"]

    # Build hash -> block index for fast lookup
    hash_to_idx = {b.block_hash: i for i, b in enumerate(blocks)}

    # Find the chain start:
    # 1. Genesis block itself (prev_hash = 0 or hash = genesis_hash)
    # 2. Block 1 (prev_hash = genesis_hash) - genesis is often not stored
    null_hash = b'\x00' * 32
    start_idx = None
    start_height = 0

    for i, block in enumerate(blocks):
        if block.prev_hash == null_hash or block.block_hash == genesis_hash:
            # Found genesis block
            start_idx = i
            start_height = 0
            break
        if block.prev_hash == genesis_hash:
            # Found block 1 (genesis not in files, which is normal)
            start_idx = i
            start_height = 1
            break

    if start_idx is None:
        errors.append("Could not find chain start (neither genesis nor block 1)")
        return 0, errors

    blocks[start_idx].height = start_height

    # Build chain using hash lookup (O(n) instead of O(n²))
    chain_blocks = {start_idx}

    # Create reverse lookup: prev_hash -> list of block indices
    prev_to_blocks = {}
    for i, block in enumerate(blocks):
        if block.prev_hash not in prev_to_blocks:
            prev_to_blocks[block.prev_hash] = []
        prev_to_blocks[block.prev_hash].append(i)

    # BFS from chain start
    queue = [start_idx]
    while queue:
        parent_idx = queue.pop(0)
        parent_hash = blocks[parent_idx].block_hash
        parent_height = blocks[parent_idx].height

        # Find all children
        for child_idx in prev_to_blocks.get(parent_hash, []):
            if child_idx not in chain_blocks:
                chain_blocks.add(child_idx)
                blocks[child_idx].height = parent_height + 1
                queue.append(child_idx)

    # Check for orphans (blocks not connected to chain start)
    orphan_count = len(blocks) - len(chain_blocks)
    if orphan_count > 0:
        errors.append(f"Found {orphan_count} orphan blocks (not connected to main chain)")

    # Verify no gaps in heights
    heights = sorted([b.height for b in blocks if b.height is not None])
    if heights:
        expected = start_height
        gaps = []
        for h in heights:
            if h != expected:
                gaps.append(f"{expected}->{h}")
                expected = h
            expected += 1
        if gaps and len(gaps) <= 10:
            errors.append(f"Gaps in chain: {', '.join(gaps)}")
        elif gaps:
            errors.append(f"Found {len(gaps)} gaps in chain")

    max_height = max(heights, default=-1)

    return max_height + 1 - start_height, errors


def main():
    parser = argparse.ArgumentParser(description='Verify block files integrity')
    parser.add_argument('blocks_dir', help='Directory containing blk*.dat files')
    parser.add_argument('--network', choices=['mainnet', 'testnet', 'testnet4', 'scalenet', 'regtest'],
                        default='mainnet', help='Network (default: mainnet)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    args = parser.parse_args()

    blocks_dir = Path(args.blocks_dir)
    if not blocks_dir.exists():
        print(f"Error: Directory {blocks_dir} does not exist")
        sys.exit(1)

    # Select magic bytes based on network
    networks = {
        'mainnet':  (MAGIC_MAINNET, GENESIS_MAINNET),
        'testnet':  (MAGIC_TESTNET, GENESIS_TESTNET),
        'testnet4': (MAGIC_TESTNET4, GENESIS_TESTNET4),
        'scalenet': (MAGIC_SCALENET, GENESIS_SCALENET),
        'regtest':  (MAGIC_REGTEST, GENESIS_REGTEST),
    }
    magic, genesis_hash = networks[args.network]

    print("=" * 70)
    print("Block File Verification")
    print("=" * 70)
    print(f"Directory: {blocks_dir}")
    print(f"Network:   {args.network}")
    print(f"Magic:     {magic.hex()}")
    print("-" * 70)

    # Find all blk*.dat files
    blk_files = sorted(blocks_dir.glob('blk*.dat'))

    if not blk_files:
        print("No blk*.dat files found!")
        sys.exit(1)

    print(f"Found {len(blk_files)} block file(s)")
    print()

    all_blocks = []
    total_bytes = 0

    for filepath in blk_files:
        # Extract file number from filename (blk00000.dat -> 0)
        try:
            file_num = int(filepath.stem[3:])
        except ValueError:
            file_num = 0

        file_size = filepath.stat().st_size
        total_bytes += file_size

        print(f"Processing {filepath.name} ({file_size:,} bytes)...")

        blocks = parse_block_file(filepath, file_num, magic)
        print(f"  Found {len(blocks)} blocks")

        if args.verbose and blocks:
            print(f"  First block: {blocks[0].block_hash[::-1].hex()[:16]}...")
            print(f"  Last block:  {blocks[-1].block_hash[::-1].hex()[:16]}...")

        all_blocks.extend(blocks)

    print()
    print("-" * 70)
    print(f"Total blocks found: {len(all_blocks)}")
    print(f"Total file size:    {total_bytes:,} bytes ({total_bytes / (1024*1024*1024):.2f} GB)")
    print()

    # Verify chain
    print("Verifying chain consistency...")
    valid_count, errors = verify_chain(all_blocks, genesis_hash)

    print()
    print("=" * 70)
    print("Results")
    print("=" * 70)
    print(f"Blocks in valid chain: {valid_count}")

    if errors:
        print()
        print("Errors/Warnings:")
        for error in errors:
            print(f"  - {error}")
        print()
        print("VERIFICATION FAILED")
        sys.exit(1)
    else:
        print()
        print("VERIFICATION PASSED - All blocks connected correctly")
        sys.exit(0)


if __name__ == '__main__':
    main()

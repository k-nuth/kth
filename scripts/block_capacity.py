#!/usr/bin/env python3
# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Block capacity calculator for static array allocation.

This script calculates the number of blocks to allocate based on the
compilation date, using 5-year periods to determine the target date.

Usage:
    python block_capacity.py [--year YYYY]

If no year is provided, uses the current year.
"""

from datetime import datetime, timezone
import argparse

# Bitcoin genesis block: 2009-01-03 18:15:05 UTC (block 0)
GENESIS_TIMESTAMP = datetime(2009, 1, 3, 18, 15, 5, tzinfo=timezone.utc)

# Average: 1 block every 10 minutes = 144 blocks/day = 52560 blocks/year
SECONDS_PER_BLOCK = 600

# Alignment for block count (2^16 = 65536)
BLOCK_ALIGNMENT = 65536

# Base year for period calculation
BASE_PERIOD_YEAR = 2030
PERIOD_LENGTH_YEARS = 5


def get_target_year(compile_year: int) -> int:
    """
    Calculate the target year based on compilation year.

    Periods end at: 2030, 2035, 2040, 2045, ...

    For any year within a period, we target the END of the next period.
    This ensures we always have at least 5 years of capacity ahead.

    Examples:
        < 2030  -> 2030 (first period end)
        2030-2034 -> 2035
        2035-2039 -> 2040
        2040-2044 -> 2045

    Args:
        compile_year: The year when the code is being compiled.

    Returns:
        The target year (end of the 5-year period).
    """
    if compile_year < BASE_PERIOD_YEAR:
        return BASE_PERIOD_YEAR

    # How many complete periods since base year
    periods_since_base = (compile_year - BASE_PERIOD_YEAR) // PERIOD_LENGTH_YEARS

    # Always jump to the END of the next period
    return BASE_PERIOD_YEAR + (periods_since_base + 1) * PERIOD_LENGTH_YEARS


def get_target_date(target_year: int) -> datetime:
    """
    Get the target date: last second of December 31st of target_year.

    Args:
        target_year: The target year.

    Returns:
        datetime object for 2030-12-31 23:59:59 UTC (or equivalent year).
    """
    return datetime(target_year, 12, 31, 23, 59, 59, tzinfo=timezone.utc)


def calculate_blocks(target_date: datetime) -> int:
    """
    Calculate the number of blocks from genesis to target_date.

    The result is rounded UP to the nearest multiple of BLOCK_ALIGNMENT.

    Args:
        target_date: The target date.

    Returns:
        Number of blocks, aligned to BLOCK_ALIGNMENT.
    """
    seconds_since_genesis = (target_date - GENESIS_TIMESTAMP).total_seconds()
    blocks_exact = seconds_since_genesis / SECONDS_PER_BLOCK

    # Round up to nearest multiple of BLOCK_ALIGNMENT
    blocks_aligned = ((int(blocks_exact) + BLOCK_ALIGNMENT - 1) // BLOCK_ALIGNMENT) * BLOCK_ALIGNMENT

    return blocks_aligned


def get_capacity_for_year(compile_year: int) -> tuple[datetime, int]:
    """
    Main function: get target date and block capacity for a compilation year.

    Args:
        compile_year: The year when the code is being compiled.

    Returns:
        Tuple of (target_date, block_count).
    """
    target_year = get_target_year(compile_year)
    target_date = get_target_date(target_year)
    blocks = calculate_blocks(target_date)
    return target_date, blocks


def estimate_memory_mb(blocks: int) -> float:
    """
    Estimate memory usage in MB for SoA storage.

    Per block (without merkle_root):
        parent_idx:  4 bytes
        skip_idx:    4 bytes
        height:      4 bytes
        chain_work:  8 bytes
        status:      4 bytes
        version:     4 bytes
        timestamp:   4 bytes
        bits:        4 bytes
        nonce:       4 bytes
        -----------------------
        Subtotal:   40 bytes

    With merkle_root (32 bytes): 72 bytes total

    Args:
        blocks: Number of blocks.

    Returns:
        Estimated memory in MB.
    """
    bytes_per_block = 72  # Including merkle_root
    return blocks * bytes_per_block / 1_000_000


def main():
    parser = argparse.ArgumentParser(
        description="Calculate block capacity for static array allocation."
    )
    parser.add_argument(
        "--year",
        type=int,
        default=None,
        help="Compilation year (default: current year)"
    )
    parser.add_argument(
        "--test",
        action="store_true",
        help="Run test with multiple years"
    )
    args = parser.parse_args()

    if args.test:
        # Test with all years from 2024 to 2045
        print(f"{'Compile Year':<14} {'Target Date':<22} {'Blocks':<12} {'~MB'}")
        print("-" * 58)

        for year in range(2024, 2046):
            target_date, blocks = get_capacity_for_year(year)
            mb = estimate_memory_mb(blocks)
            print(f"{year:<14} {target_date.strftime('%Y-%m-%d %H:%M:%S'):<22} {blocks:<12,} {mb:,.0f}")
    else:
        # Single year calculation
        year = args.year if args.year else datetime.now().year
        target_date, blocks = get_capacity_for_year(year)
        mb = estimate_memory_mb(blocks)

        print(f"Compile year:  {year}")
        print(f"Target date:   {target_date.strftime('%Y-%m-%d %H:%M:%S')} UTC")
        print(f"Block capacity: {blocks:,}")
        print(f"Memory (SoA):  ~{mb:,.0f} MB")

        # Output just the number for scripting
        # print(blocks)


if __name__ == "__main__":
    main()

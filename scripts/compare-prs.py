#!/usr/bin/env python3
"""
Compare two git branches, accounting for static_cast style normalization.
Usage: python3 scripts/compare-prs.py <branch1> <branch2>
"""

import subprocess
import sys
import re


def normalize_line(line: str) -> str:
    """Normalize static_cast<T>(x) to T(x) for comparison."""
    # Match common simple types used in static_cast
    simple_types = r'(size_t|u?int(?:8|16|32|64)?_t|time_t|ptrdiff_t|unsigned|signed|char|short|int|long|float|double|bool)'
    pattern = rf'static_cast<\s*({simple_types})\s*>\s*\('
    normalized = re.sub(pattern, r'\1(', line)
    return normalized


def get_file_content(branch: str, filepath: str) -> str | None:
    """Get file content from a specific branch."""
    try:
        result = subprocess.run(
            ['git', 'show', f'{branch}:{filepath}'],
            capture_output=True,
            check=True
        )
        # Try to decode as UTF-8, return None for binary files
        try:
            return result.stdout.decode('utf-8')
        except UnicodeDecodeError:
            return None  # Binary file
    except subprocess.CalledProcessError:
        return None


def get_files_in_branch(branch: str, base: str) -> set[str]:
    """Get all files that differ between branch and base."""
    result = subprocess.run(
        ['git', 'diff', '--name-only', base, branch],
        capture_output=True,
        text=True,
        check=True
    )
    return set(result.stdout.strip().split('\n')) if result.stdout.strip() else set()


def compare_branches(branch1: str, branch2: str):
    """Compare two branches accounting for style normalization."""
    # Find common ancestor (usually master)
    result = subprocess.run(
        ['git', 'merge-base', branch1, branch2],
        capture_output=True,
        text=True,
        check=True
    )
    merge_base = result.stdout.strip()

    # Get files changed in each branch relative to merge base
    files1 = get_files_in_branch(branch1, merge_base)
    files2 = get_files_in_branch(branch2, merge_base)

    all_files = files1 | files2

    identical = []
    minor_diffs = []  # Only static_cast style differences
    significant_diffs = []
    only_in_1 = []
    only_in_2 = []
    binary_files = []

    for filepath in sorted(all_files):
        content1 = get_file_content(branch1, filepath)
        content2 = get_file_content(branch2, filepath)

        # Check if file exists in branch (use git ls-tree for binary detection)
        exists1 = subprocess.run(['git', 'cat-file', '-e', f'{branch1}:{filepath}'],
                                  capture_output=True).returncode == 0
        exists2 = subprocess.run(['git', 'cat-file', '-e', f'{branch2}:{filepath}'],
                                  capture_output=True).returncode == 0

        if not exists1 and not exists2:
            continue
        elif not exists1:
            only_in_2.append(filepath)
            continue
        elif not exists2:
            only_in_1.append(filepath)
            continue

        # Both exist - check if binary (content is None)
        if content1 is None or content2 is None:
            binary_files.append(filepath)
            continue

        if content1 == content2:
            identical.append(filepath)
        else:
            # Check if differences are only static_cast style
            lines1 = [normalize_line(line) for line in content1.split('\n')]
            lines2 = [normalize_line(line) for line in content2.split('\n')]

            if lines1 == lines2:
                minor_diffs.append(filepath)
            else:
                significant_diffs.append(filepath)

    # Print results
    print(f"Comparing {branch1} vs {branch2}")
    print(f"Merge base: {merge_base[:8]}")
    print("=" * 60)

    print(f"\nIdentical files: {len(identical)}")

    print(f"\nMinor differences (static_cast style only): {len(minor_diffs)}")
    for f in minor_diffs[:10]:
        print(f"  {f}")
    if len(minor_diffs) > 10:
        print(f"  ... and {len(minor_diffs) - 10} more")

    print(f"\nSignificant differences: {len(significant_diffs)}")
    for f in significant_diffs:
        print(f"  {f}")
        # Show actual diff for significant files
        diff_result = subprocess.run(
            ['git', 'diff', branch1, branch2, '--', f],
            capture_output=True,
            text=True
        )
        if diff_result.stdout:
            diff_lines = diff_result.stdout.split('\n')
            # Show first 30 lines of diff
            for line in diff_lines[:30]:
                print(f"    {line}")
            if len(diff_lines) > 30:
                print(f"    ... ({len(diff_lines) - 30} more lines)")
        print()

    print(f"\nOnly in {branch1}: {len(only_in_1)}")
    for f in only_in_1:
        print(f"  {f}")

    print(f"\nOnly in {branch2}: {len(only_in_2)}")
    for f in only_in_2:
        print(f"  {f}")

    if binary_files:
        print(f"\nBinary files (not compared): {len(binary_files)}")
        for f in binary_files:
            print(f"  {f}")

    print("\n" + "=" * 60)
    total_files = len(identical) + len(minor_diffs) + len(significant_diffs) + len(binary_files)
    print(f"Summary: {total_files} files compared")
    if len(significant_diffs) == 0 and len(only_in_1) == 0 and len(only_in_2) == 0 and len(binary_files) == 0:
        print("RESULT: Branches are equivalent (only style differences)")
    elif len(significant_diffs) == 0 and len(only_in_1) == 0 and len(only_in_2) == 0:
        print(f"RESULT: Branches are equivalent in text files, but {len(binary_files)} binary file(s) were not compared")
    else:
        print("RESULT: Branches have significant differences")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <branch1> <branch2>")
        sys.exit(1)

    compare_branches(sys.argv[1], sys.argv[2])

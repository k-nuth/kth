#!/usr/bin/env python3
"""
Header Implementation Benchmark Comparison Tool

This script runs both header implementation benchmarks (header_raw and header_members),
parses the results, and generates a markdown comparison report.

Usage:
    python compare_header_benchmarks.py [--output report.md] [--runs N]
"""

import subprocess
import re
import argparse
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional


def find_benchmark_executables() -> tuple[Path, Path]:
    """Find the benchmark executables in the build directory."""
    # Try common build paths
    build_paths = [
        Path("build/build/Release/src/domain"),
        Path("build/Release/src/domain"),
        Path("build/src/domain"),
        Path("../../build/build/Release/src/domain"),
    ]

    raw_exe = None
    members_exe = None

    for base in build_paths:
        raw_candidate = base / "kth_domain_benchmarks_header_raw"
        members_candidate = base / "kth_domain_benchmarks_header_members"

        if raw_candidate.exists():
            raw_exe = raw_candidate
        if members_candidate.exists():
            members_exe = members_candidate

        if raw_exe and members_exe:
            break

    if not raw_exe:
        # Try absolute path
        raw_exe = Path("/Users/fernando/dev/kth/kth-mono/build/build/Release/src/domain/kth_domain_benchmarks_header_raw")
    if not members_exe:
        members_exe = Path("/Users/fernando/dev/kth/kth-mono/build/build/Release/src/domain/kth_domain_benchmarks_header_members")

    return raw_exe, members_exe


def run_benchmark(executable: Path, runs: int = 1) -> str:
    """Run a benchmark executable and return its output."""
    if not executable.exists():
        raise FileNotFoundError(f"Benchmark executable not found: {executable}")

    print(f"Running {executable.name}...", file=sys.stderr)

    # Run multiple times and take the last run (warmup effect)
    output = ""
    for i in range(runs):
        result = subprocess.run(
            [str(executable)],
            capture_output=True,
            text=True,
            timeout=300  # 5 minute timeout
        )
        output = result.stdout + result.stderr
        if i < runs - 1:
            print(f"  Run {i + 1}/{runs} complete (warmup)", file=sys.stderr)

    print(f"  Final run complete", file=sys.stderr)
    return output


def parse_benchmark_output(output: str) -> dict:
    """
    Parse nanobench output and extract benchmark results.

    Format:
    |               ns/op |                op/s |    err% |     total | Benchmark Title
    |--------------------:|--------------------:|--------:|----------:|:---------------
    |                2.14 |      467,112,663.79 |    1.9% |      0.01 | `implementation_name`
    """
    results = {}

    # Extract memory layout info
    sizeof_match = re.search(r'sizeof\(header\):\s+(\d+)\s+bytes', output)
    alignof_match = re.search(r'alignof\(header\):\s+(\d+)\s+bytes', output)

    if sizeof_match:
        results['sizeof'] = int(sizeof_match.group(1))
    if alignof_match:
        results['alignof'] = int(alignof_match.group(1))

    lines = output.split('\n')
    current_title = None

    for line in lines:
        # Match header row (contains title, no backticks, no separator characters)
        # |               ns/op |                op/s |    err% |     total | Construction from Fields
        header_match = re.match(
            r'\|\s+ns/op\s+\|\s+op/s\s+\|\s+err%\s+\|\s+total\s+\|\s+(.+)$',
            line
        )
        if header_match:
            current_title = header_match.group(1).strip()
            continue

        # Skip separator rows (contain :--- or ---:)
        if '|---' in line or '|:--' in line:
            continue

        # Match data row (contains backticks with implementation name)
        # |                2.14 |      467,112,663.79 |    1.9% |      0.01 | `header_raw (array-based)`
        # |           19,597.78 |           51,026.20 |    5.2% |      0.01 | `header_raw (array-based)`
        # May also have :wavy_dash: prefix for unstable results
        data_match = re.match(
            r'\|\s+([\d,]+\.?\d*)\s+\|\s+([\d,]+\.?\d*)\s+\|\s+([\d.]+)%\s+\|\s+([\d.]+)\s+\|.*`([^`]+)`',
            line
        )
        if data_match and current_title:
            ns_op = float(data_match.group(1).replace(',', ''))
            ops_s = float(data_match.group(2).replace(',', ''))
            err_pct = float(data_match.group(3))
            impl_name = data_match.group(5)

            results[current_title] = {
                'ns_op': ns_op,
                'ops_s': ops_s,
                'err_pct': err_pct,
                'impl': impl_name
            }

    return results


def calculate_diff(raw_val: float, members_val: float) -> tuple[float, str]:
    """Calculate percentage difference and return formatted string."""
    if raw_val == 0:
        return 0, "N/A"

    diff_pct = ((members_val - raw_val) / raw_val) * 100

    if abs(diff_pct) < 1:
        return diff_pct, "~same"
    elif diff_pct > 0:
        return diff_pct, f"+{diff_pct:.1f}% slower"
    else:
        return diff_pct, f"{abs(diff_pct):.1f}% faster"


def generate_markdown_report(
    raw_results: dict,
    members_results: dict,
    output_path: Optional[Path] = None
) -> str:
    """Generate a markdown comparison report."""

    report = []
    report.append("# Header Implementation Benchmark Comparison")
    report.append("")
    report.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append("")

    # Memory layout comparison
    report.append("## Memory Layout")
    report.append("")
    report.append("| Property | header_raw | header_members | Difference |")
    report.append("|----------|------------|----------------|------------|")

    raw_sizeof = raw_results.get('sizeof', 'N/A')
    members_sizeof = members_results.get('sizeof', 'N/A')
    sizeof_diff = ""
    if isinstance(raw_sizeof, int) and isinstance(members_sizeof, int):
        diff = members_sizeof - raw_sizeof
        sizeof_diff = f"{diff:+d} bytes" if diff != 0 else "same"

    report.append(f"| sizeof(header) | {raw_sizeof} bytes | {members_sizeof} bytes | {sizeof_diff} |")

    raw_alignof = raw_results.get('alignof', 'N/A')
    members_alignof = members_results.get('alignof', 'N/A')
    alignof_diff = ""
    if isinstance(raw_alignof, int) and isinstance(members_alignof, int):
        diff = members_alignof - raw_alignof
        alignof_diff = f"{diff:+d} bytes" if diff != 0 else "same"

    report.append(f"| alignof(header) | {raw_alignof} bytes | {members_alignof} bytes | {alignof_diff} |")
    report.append("")

    # Benchmark categories
    categories = {
        "Construction": [
            "Construction from Fields",
            "Construction from Raw Bytes",
            "Default Construction",
        ],
        "Field Access": [
            "Access: version",
            "Access: timestamp",
            "Access: bits",
            "Access: nonce",
            "Access: previous_block_hash",
            "Access: merkle",
            "Access: ALL fields",
        ],
        "Serialization": [
            "Serialization: to_data(wire=true)",
            "Raw Data Access (zero-copy)",
        ],
        "Deserialization": [
            "Deserialization: from_data(wire=true)",
        ],
        "Hashing": [
            "Hash Computation: chain::hash()",
        ],
        "Copy/Move": [
            "Copy Construction",
            "Move Construction",
        ],
        "Comparison": [
            "Equality Comparison (==)",
        ],
        "Batch Operations": [
            "Batch Construction (10000 headers)",
            "Batch Construction from Raw (10000 headers)",
            "Batch Hashing (10000 headers)",
        ],
        "Realistic Workload": [
            "IBD Simulation: construct + access + hash (2000 headers)",
        ],
    }

    report.append("## Performance Comparison")
    report.append("")
    report.append("Lower ns/op is better. The 'Winner' column shows which implementation is faster.")
    report.append("")

    all_benchmarks = []  # Track for summary

    for category, benchmarks in categories.items():
        has_data = False
        category_rows = []

        for bench_name in benchmarks:
            raw_data = raw_results.get(bench_name)
            members_data = members_results.get(bench_name)

            if raw_data or members_data:
                has_data = True

                raw_ns = raw_data['ns_op'] if raw_data else None
                raw_err = raw_data['err_pct'] if raw_data else None
                members_ns = members_data['ns_op'] if members_data else None
                members_err = members_data['err_pct'] if members_data else None

                # Determine winner
                winner = ""
                diff_str = ""
                if raw_ns and members_ns:
                    diff_pct, diff_str = calculate_diff(raw_ns, members_ns)
                    if diff_pct < -1:
                        winner = "**members**"
                    elif diff_pct > 1:
                        winner = "**raw**"
                    else:
                        winner = "tie"
                    all_benchmarks.append((bench_name, diff_pct))
                elif raw_ns:
                    winner = "raw only"
                    diff_str = "N/A"
                elif members_ns:
                    winner = "members only"
                    diff_str = "N/A"

                raw_str = f"{raw_ns:.2f} ±{raw_err:.1f}%" if raw_ns else "N/A"
                members_str = f"{members_ns:.2f} ±{members_err:.1f}%" if members_ns else "N/A"

                category_rows.append(f"| {bench_name} | {raw_str} | {members_str} | {diff_str} | {winner} |")

        if has_data:
            report.append(f"### {category}")
            report.append("")
            report.append("| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |")
            report.append("|-----------|-------------------|----------------------|------------|--------|")
            report.extend(category_rows)
            report.append("")

    # Summary
    report.append("## Summary")
    report.append("")

    # Count wins
    raw_wins = 0
    members_wins = 0
    ties = 0

    for bench_name, diff_pct in all_benchmarks:
        if diff_pct < -1:
            members_wins += 1
        elif diff_pct > 1:
            raw_wins += 1
        else:
            ties += 1

    report.append(f"- **header_raw wins**: {raw_wins} benchmarks")
    report.append(f"- **header_members wins**: {members_wins} benchmarks")
    report.append(f"- **Ties** (within 1%): {ties} benchmarks")
    report.append("")

    # Key observations
    report.append("### Key Observations")
    report.append("")
    report.append("- `header_raw` (80 bytes): Array-based storage with zero-copy raw data access")
    report.append("- `header_members` (80 bytes): Traditional member-based storage")
    report.append("")
    report.append("**header_raw advantages:**")
    report.append("- Zero-copy raw data access for serialization")
    report.append("- Better cache locality for sequential header processing")
    report.append("")
    report.append("**header_members advantages:**")
    report.append("- More intuitive code structure")
    report.append("- Direct field access without byte extraction")
    report.append("")

    result = '\n'.join(report)

    if output_path:
        output_path.write_text(result)
        print(f"Report written to: {output_path}", file=sys.stderr)

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Compare header implementation benchmarks"
    )
    parser.add_argument(
        '--output', '-o',
        type=Path,
        default=Path("header_benchmark_comparison.md"),
        help="Output markdown file path (default: header_benchmark_comparison.md)"
    )
    parser.add_argument(
        '--runs', '-r',
        type=int,
        default=3,
        help="Number of benchmark runs (last run is used, earlier runs are warmup)"
    )
    parser.add_argument(
        '--raw-exe',
        type=Path,
        help="Path to header_raw benchmark executable"
    )
    parser.add_argument(
        '--members-exe',
        type=Path,
        help="Path to header_members benchmark executable"
    )

    args = parser.parse_args()

    # Find executables
    if args.raw_exe and args.members_exe:
        raw_exe, members_exe = args.raw_exe, args.members_exe
    else:
        raw_exe, members_exe = find_benchmark_executables()

    print(f"Using executables:", file=sys.stderr)
    print(f"  raw: {raw_exe}", file=sys.stderr)
    print(f"  members: {members_exe}", file=sys.stderr)
    print("", file=sys.stderr)

    # Run benchmarks
    try:
        raw_output = run_benchmark(raw_exe, args.runs)
        members_output = run_benchmark(members_exe, args.runs)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        print("Please build the benchmarks first with:", file=sys.stderr)
        print("  cmake --build build --target kth_domain_benchmarks_header_raw", file=sys.stderr)
        print("  cmake --build build --target kth_domain_benchmarks_header_members", file=sys.stderr)
        sys.exit(1)
    except subprocess.TimeoutExpired:
        print("Error: Benchmark timed out", file=sys.stderr)
        sys.exit(1)

    # Parse results
    raw_results = parse_benchmark_output(raw_output)
    members_results = parse_benchmark_output(members_output)

    # Debug: print parsed benchmark names
    print(f"\nParsed {len(raw_results) - 2} benchmarks from header_raw", file=sys.stderr)
    print(f"Parsed {len(members_results) - 2} benchmarks from header_members", file=sys.stderr)

    # Generate report
    report = generate_markdown_report(raw_results, members_results, args.output)

    # Also print to stdout
    print(report)

    return 0


if __name__ == "__main__":
    sys.exit(main())

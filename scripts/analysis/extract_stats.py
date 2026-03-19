#!/usr/bin/env python3
"""Extract sync statistics from debug.log."""

import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: extract_stats.py <debug.log> <output.txt>", file=sys.stderr)
        sys.exit(1)

    log_file = sys.argv[1]
    output_file = sys.argv[2]

    with open(log_file, 'r') as f, open(output_file, 'w') as out:
        # Print first line
        first_line = f.readline()
        if first_line:
            out.write(first_line)

        out.write("---\n")

        # Print all lines containing the stats pattern
        for line in f:
            if "[info] [block_supervisor] Stats:" in line:
                out.write(line)

    print(f"Output written to {output_file}")

if __name__ == "__main__":
    main()

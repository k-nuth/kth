#!/usr/bin/env python3
"""Record which third-party binaries Conan reused, downloaded or rebuilt."""
import argparse
from collections import Counter
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graph", type=Path)
    args = parser.parse_args()
    graph = json.loads(args.graph.read_text())["graph"]
    rows = []
    for node in graph["nodes"].values():
        if not node.get("ref") or node.get("binary") is None:
            continue  # The workspace consumer has no dependency binary.
        row = {key: node.get(key) for key in
               ("ref", "context", "package_id", "binary", "settings", "options")}
        rows.append(row)
        if node["binary"] in ("Build", "Missing"):
            print("::warning::Dependency binary was not reusable: " + json.dumps(row, sort_keys=True))
    counts = dict(sorted(Counter(row["binary"] for row in rows).items()))
    report = {"counts": counts, "dependencies": rows}
    args.graph.with_name("dependency-binaries.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Conan dependency binaries:", json.dumps(counts, sort_keys=True))


if __name__ == "__main__":
    main()

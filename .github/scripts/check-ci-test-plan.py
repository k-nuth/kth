#!/usr/bin/env python3
"""Check the configured CTest boundary before executing the complete CI suite."""
import argparse
from collections import Counter
import json
import pathlib
import subprocess


def check(plan, shards):
    tests = plan["tests"]
    failures = []
    for target in ("kth_node_test", "kth_domain_test"):
        group = [t for t in tests if t.get("command") and pathlib.Path(t["command"][0]).name == target]
        if len(group) != shards:
            failures.append(f"{target}: expected {shards} shards, got {len(group)}")
            continue
        directories = set()
        indices = set()
        for test in group:
            command = test["command"]
            properties = {p["name"]: p["value"] for p in test.get("properties", [])}
            if "--shard-count" not in command or command[command.index("--shard-count") + 1] != str(shards):
                failures.append(f"{test['name']}: no matching Catch2 shard count")
            if "--shard-index" in command:
                indices.add(command[command.index("--shard-index") + 1])
            cwd = properties.get("WORKING_DIRECTORY")
            env = properties.get("ENVIRONMENT", [])
            if not cwd or not any(e.startswith("TMPDIR=") for e in env):
                failures.append(f"{test['name']}: working directory/temporaries are not isolated")
            if cwd in directories:
                failures.append(f"{test['name']}: shares its working directory")
            directories.add(cwd)
        if indices != {str(i) for i in range(shards)}:
            failures.append(f"{target}: missing or repeated shard indices")
    # Preserve the non-Catch tests too: switching Linux from individual binary
    # steps to CTest must not accidentally discard secp256k1 or compile checks.
    names = {t["name"] for t in tests}
    for prefix in ("secp256k1.tests.", "secp256k1.noverify_tests.", "secp256k1.exhaustive_tests"):
        if not any(name.startswith(prefix) for name in names):
            failures.append(f"missing registered test family: {prefix}")
    # A generic secp256k1 family can remain green with the BCH module disabled.
    # These are the four entries discovered from `Module: schnorr_bch`.
    for family in ("tests", "noverify_tests"):
        for case in ("run_schnorr_bch_hash_context", "run_schnorr_end_to_end_tests",
                     "test_schnorr_sign_verify", "run_schnorr_compact_test"):
            name = f"secp256k1.{family}.{case}"
            if name not in names:
                failures.append(f"missing registered BCH Schnorr test: {name}")
    for name in ("kth_infrastructure_compile_fail_tests", "kth_compile_check_driver_tests",
                 "kth_capi_mining_info_callback", "kth_capi_async_callbacks"):
        if name not in names:
            failures.append(f"missing registered test: {name}")
    return failures


def catalog(command):
    result = subprocess.run(command + ["--list-tests", "--reporter", "json"],
                            check=True, capture_output=True, text=True)
    return Counter(t["name"] for t in json.loads(result.stdout)["listings"]["tests"])


def check_coverage(plan):
    """Compare Catch's actual selections, including its hidden-case semantics."""
    failures = []
    for target in ("kth_node_test", "kth_domain_test"):
        group = [t for t in plan["tests"] if t.get("command") and pathlib.Path(t["command"][0]).name == target]
        expected = catalog([group[0]["command"][0], "--order", "lex"])
        selected = Counter()
        for test in group:
            command = list(test["command"])
            at = command.index("--reporter")
            del command[at:at + 2]
            selected.update(catalog(command))
        if expected != selected:
            failures.append(f"{target}: shards lost or repeated test cases")
        else:
            print(f"{target}: all {sum(expected.values())} cases selected exactly once")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--shards", type=int, default=2)
    args = parser.parse_args()
    result = subprocess.run(["ctest", "--test-dir", str(args.build), "--show-only=json-v1"],
                            check=True, capture_output=True, text=True)
    plan = json.loads(result.stdout)
    failures = check(plan, args.shards)
    if not failures:
        failures.extend(check_coverage(plan))
    for failure in failures:
        print(f"::error::{failure}")
    if failures:
        return 1
    print(f"CTest plan: complete registry, {args.shards} isolated node/domain shards")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

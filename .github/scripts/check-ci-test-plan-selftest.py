#!/usr/bin/env python3
"""Require the real CLI to reject missing Schnorr and C-API callback controls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


CHECKER = Path(__file__).with_name("check-ci-test-plan.py")
BCH_CASES = ("run_schnorr_bch_hash_context", "run_schnorr_end_to_end_tests",
             "test_schnorr_sign_verify", "run_schnorr_compact_test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    actual = subprocess.run(["ctest", "--test-dir", str(args.build), "--show-only=json-v1"],
                            check=True, capture_output=True, text=True)
    plan = json.loads(actual.stdout)
    positive = subprocess.run([sys.executable, str(CHECKER), str(args.build)],
                              capture_output=True, text=True)
    if positive.returncode:
        raise AssertionError("positive registry failed:\n" + positive.stdout + positive.stderr)
    required = {f"secp256k1.{family}.{case}" for family in ("tests", "noverify_tests") for case in BCH_CASES}
    names = {t["name"] for t in plan["tests"]}
    if not required <= names:
        raise AssertionError("positive registry did not contain the BCH Schnorr controls")
    plan["tests"] = [test for test in plan["tests"] if test["name"] not in required]
    # Keep every other real entry and the actual Catch binaries. Only the
    # CTest listing is changed, exactly as disabling the BCH module would do.
    with tempfile.TemporaryDirectory(prefix="ci-test-plan-") as temporary:
        root = Path(temporary)
        fixture = root / "plan.json"
        fixture.write_text(json.dumps(plan))
        shim = root / "ctest"
        shim.write_text("#!" + sys.executable + "\nfrom pathlib import Path\n"
                        "print(Path(" + repr(str(fixture)) + ").read_text())\n")
        shim.chmod(0o755)
        env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"])
        negative = subprocess.run([sys.executable, str(CHECKER), str(args.build)],
                                  env=env, capture_output=True, text=True)
        for callback in ("kth_capi_mining_info_callback", "kth_capi_async_callbacks"):
            callback_plan = json.loads(actual.stdout)
            callback_plan["tests"] = [test for test in callback_plan["tests"] if test["name"] != callback]
            fixture.write_text(json.dumps(callback_plan))
            missing_callback = subprocess.run([sys.executable, str(CHECKER), str(args.build)],
                                              env=env, capture_output=True, text=True)
            if not missing_callback.returncode or "missing registered test: " + callback not in missing_callback.stdout + missing_callback.stderr:
                raise AssertionError("missing C-API callback control was not rejected:\n" + missing_callback.stdout + missing_callback.stderr)
    if not negative.returncode or "BCH Schnorr" not in negative.stdout + negative.stderr:
        raise AssertionError("missing BCH Schnorr module was not rejected:\n" + negative.stdout + negative.stderr)
    print("CTest registry controls: complete plan accepted; missing BCH Schnorr and C-API callback rejected")


if __name__ == "__main__":
    main()

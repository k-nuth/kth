#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Both dispatch modes resolve and identify distinct Conan binaries."""
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
ids = []
for mode in ["runtime", "compile_time"]:
    result = subprocess.check_output([
        "conan", "graph", "info", str(root / "conanfile.py"), "--version=0.0.0",
        "--lockfile=" + str(root / "conan.lock"), "--format=json",
        "-s", "compiler.cppstd=23", "-o", "sha256_dispatch=" + mode,
        "-o", "consensus=False"], text=True)
    node = json.loads(result)["graph"]["nodes"]["0"]
    assert node["options"]["sha256_dispatch"] == mode
    assert node["package_id"]
    assert not node.get("info_invalid"), node
    ids.append(node["package_id"])
assert ids[0] != ids[1], ids
print("Conan dispatch modes have distinct package IDs")

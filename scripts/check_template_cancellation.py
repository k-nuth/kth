#!/usr/bin/env python3
"""Run the public template cancellation regression against the actual worker TU.

Usage: python3 scripts/check_template_cancellation.py /path/to/cmake-build
Build kth_node_test first, using Unix Makefiles and GCC/Clang with TSan support.
The helper and public test TU are instrumented; other linked libraries retain
that build's instrumentation. This focused control is not a full TSan build.
Logs and temporary sources are retained, including on failure.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def target_flags(target):
    flags = {}
    for line in target.read_text().splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    return flags


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("build", type=Path)
    cli.add_argument("--implementation", type=Path,
                     help="alternate original worker TU for a baseline/negative control")
    cli.add_argument("--log-dir", type=Path)
    args = cli.parse_args()
    build = args.build.resolve()
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    source = Path(__file__).resolve().parent.parent
    implementation = args.implementation or source / "src/blockchain/src/pools/template_prevouts.cpp"
    logs = (args.log_dir or Path(tempfile.mkdtemp(prefix="template-cancellation-"))).resolve()
    logs.mkdir(parents=True, exist_ok=True)
    print(f"Full sources and logs: {logs}", flush=True)
    implementation = implementation.resolve()
    original_bytes = implementation.read_bytes()
    original = original_bytes.decode()
    manifest = {
        "implementation": str(implementation),
        "implementation_sha256": hashlib.sha256(original_bytes).hexdigest(),
        "build": str(build),
        "compiler": cache["CMAKE_CXX_COMPILER"],
    }
    for relative in ("CMakeCache.txt", "src/node/CMakeFiles/kth_node_test.dir/link.txt",
                     "src/node/CMakeFiles/kth_node_test.dir/flags.make",
                     "src/blockchain/CMakeFiles/blockchain.dir/flags.make"):
        manifest[relative + "_sha256"] = hashlib.sha256((build / relative).read_bytes()).hexdigest()
    (logs / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    observer = "kth_template_cancel_started"
    if observer in original:
        raise RuntimeError("the implementation must not contain a test entry observer")
    # Only the child's initial state acquisition is observed. The observer
    # establishes that the real worker entered before its caller sends cancel;
    # all cancellation reads/writes remain the implementation's actual code.
    pattern = (r"(refresh_on_chain_executor\([^\{]+\{\s*"
               r"(?:auto const owner = co_await ::asio::this_coro::executor;\s*)?"
               r"auto const cancellation = co_await ::asio::this_coro::cancellation_state;)")
    instrumented, count = re.subn(pattern, r"\1\n    kth_template_cancel_started();", original)
    if count != 1:
        raise RuntimeError(f"expected exactly one worker entry observer, found {count}")
    worker = logs / "template_prevouts.cpp"
    worker.write_text(f'extern "C" void {observer}();\n' + instrumented)
    test = source / "src/node/test/template_cancellation_owner.cpp"
    binary = logs / "template-cancellation"
    compiler = cache["CMAKE_CXX_COMPILER"]
    sanitizer = ["-O1", "-g", "-fsanitize=thread", "-fno-omit-frame-pointer"]
    for name, translation_unit, flags_file in (
        ("worker", worker, "src/blockchain/CMakeFiles/blockchain.dir/flags.make"),
        ("test", test, "src/node/CMakeFiles/kth_node_test.dir/flags.make"),
    ):
        flags = target_flags(build / flags_file)
        defines = [v for v in flags["CXX_DEFINES"] if not v.startswith("-DKTH_NODE_TEST_BINARY=")]
        if name == "test":
            defines.append(f'-DKTH_NODE_TEST_BINARY="{binary}"')
        command = [compiler, *defines, *flags["CXX_INCLUDES"], *flags["CXX_FLAGS"],
                   *sanitizer, "-c", str(translation_unit), "-o", str(logs / f"{name}.o")]
        with (logs / f"{name}-compile.log").open("w") as output:
            output.write(shlex.join(command) + "\n")
            output.flush()
            subprocess.run(command, cwd=build, stdout=output, stderr=subprocess.STDOUT, check=True)
    recipe = shlex.split((build / "src/node/CMakeFiles/kth_node_test.dir/link.txt").read_text())
    command = []
    inserted = False
    for token in recipe:
        if token.endswith(".cpp.o"):
            if not inserted:
                command.extend([str(logs / "test.o"), str(logs / "worker.o")])
                inserted = True
        elif not token.startswith("-Wl,--dependency-file="):
            command.append(token)
    if not inserted:
        raise RuntimeError("link recipe contained no original test translation units")
    command[command.index("-o") + 1] = str(binary)
    command.extend(["-fsanitize=thread"])
    with (logs / "link.log").open("w") as output:
        output.write(shlex.join(command) + "\n")
        output.flush()
        subprocess.run(command, cwd=build / "src/node", stdout=output, stderr=subprocess.STDOUT, check=True)
    env = dict(os.environ, TSAN_OPTIONS="halt_on_error=1:exitcode=66")
    run_log = logs / "run.log"
    with run_log.open("w") as output:
        result = subprocess.run([str(binary), "[template_cancel_owner]"], env=env,
                                stdout=output, stderr=subprocess.STDOUT, timeout=90)
    text = run_log.read_text()
    diagnostics = re.search(r"(?:WARNING|SUMMARY|FATAL): ThreadSanitizer", text)
    unchanged = implementation.read_bytes() == original_bytes
    if not unchanged:
        print("FAIL: original implementation changed during the control", flush=True)
    passed = result.returncode == 0 and not diagnostics and unchanged
    print(f"{'PASS' if passed else 'FAIL'}: exit {result.returncode}; {run_log}", flush=True)
    return int(not passed)


if __name__ == "__main__":
    raise SystemExit(main())

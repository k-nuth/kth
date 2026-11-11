#!/usr/bin/env python3
"""Validate both installed packages without rebuilding the tested variant."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def run(command, cwd):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, check=True)


def cache_value(cache, name):
    for line in cache.read_text().splitlines():
        if line.startswith(name + ":"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"{name} is absent from {cache}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--build-folder", type=Path, default=Path("build"))
    parser.add_argument("--march-id")
    args = parser.parse_args()
    root = Path.cwd()
    build = args.build_folder.resolve()
    # Match cmake_layout in the recipe. Refuse to label binaries from another
    # configuration as consensus=True; export-pkg deliberately does not build.
    cache = build / "build/Release/CMakeCache.txt"
    if cache_value(cache, "WITH_CONSENSUS") != "ON":
        raise RuntimeError("the tested build must have WITH_CONSENSUS=ON")
    if cache_value(cache, "CMAKE_BUILD_TYPE") != "Release":
        raise RuntimeError("the tested build must be Release")
    common = ["--version", args.version, "--lockfile=" + str(build / "conan.lock"),
              "-pr:b", args.profile, "-pr:h", args.profile]
    if args.march_id:
        common += ["-o", "march_id=" + args.march_id]
    start = time.monotonic()
    # tests is already excluded from the binary package ID by kthbuild. Keep
    # the original options, and exercise the actual recipe + test_package.
    # --no-remote also makes a missing dependency an error, never a rebuild.
    run(["conan", "export-pkg", "conanfile.py", *common, "--no-remote",
         "-of", build, "-o", "tests=True", "-o", "consensus=True"], root)
    tested_seconds = time.monotonic() - start

    # This is a distinct ABI (WITH_CONSENSUS is public), so it needs a distinct
    # build. Use the workspace and the same compiler cache, not a fresh source
    # copy inside Conan's cache. No third-party dependency may be rebuilt here.
    alternate = build.with_name(build.name + "-no-consensus")
    run(["conan", "install", "conanfile.py", *common, "--build=never", "--no-remote",
         "-of", alternate, "-o", "tests=False", "-o", "consensus=False"], root)
    binary_dir = alternate / "build/Release"
    toolchain = binary_dir / "generators/conan_toolchain.cmake"
    command = ["cmake", "-S", root, "-B", binary_dir,
               "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DCMAKE_BUILD_TYPE=Release",
               "-DENABLE_TEST=OFF", "-DWITH_CONSENSUS=OFF"]
    if shutil.which("ccache"):
        command += ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"]
    start = time.monotonic()
    run(command, root)
    jobs = os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", str(os.cpu_count() or 2))
    run(["cmake", "--build", binary_dir, "--parallel", jobs], root)
    run(["conan", "export-pkg", "conanfile.py", *common, "--no-remote",
         "-of", alternate, "-o", "tests=False", "-o", "consensus=False"], root)
    metrics = {"tested_package_seconds": round(tested_seconds, 2),
               "alternate_package_seconds": round(time.monotonic() - start, 2),
               "dependency_build_policy": "never"}
    (build / "package-validation.json").write_text(json.dumps(metrics, indent=2) + "\n")
    print(json.dumps(metrics), flush=True)


if __name__ == "__main__":
    main()

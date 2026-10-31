#!/usr/bin/env python3
"""Check currency option selection in the real preprocessed parser TU.

The default control requires BCH-only fork registrations to be absent for
BTC/LTC and present for BCH. --mode syntax additionally offers complete TU
compilation, which reports all errors, including unrelated baseline failures.
"""

import argparse
import pathlib
import re
import shlex
import subprocess
import tempfile


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("build", type=pathlib.Path, help="configured Unix Makefiles CMake build directory")
    cli.add_argument("--parser", type=pathlib.Path, help="alternate actual parser.cpp")
    cli.add_argument("--log-dir", type=pathlib.Path)
    cli.add_argument("--mode", choices=("options", "syntax"), default="options")
    args = cli.parse_args()
    build = args.build.resolve()
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    flags = {}
    target = build / "src/node/CMakeFiles/node.dir/flags.make"
    for line in target.read_text().splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    source = args.parser or pathlib.Path(cache["CMAKE_HOME_DIRECTORY"]) / "src/node/src/parser.cpp"
    logs = args.log_dir or pathlib.Path(tempfile.mkdtemp(prefix="parser-currencies-"))
    logs.mkdir(parents=True, exist_ok=True)
    failed = False
    for currency, name in (("BTC", "Bitcoin"), ("LTC", "Litecoin"), ("BCH", "Bitcoin Cash")):
        defines = [v for v in flags["CXX_DEFINES"] if not v.startswith("-DKTH_CURRENCY_")]
        defines += [f"-DKTH_CURRENCY_{currency}", f'-DKTH_CURRENCY_STR="{name}"',
                    f'-DKTH_CURRENCY_SYMBOL_STR="{currency}"']
        action = ["-E", "-P"] if args.mode == "options" else ["-fsyntax-only"]
        command = [cache["CMAKE_CXX_COMPILER"], *defines, *flags["CXX_INCLUDES"],
                   *flags["CXX_FLAGS"], *action, str(source.resolve())]
        log = logs / f"parser-{currency}.log"
        with log.open("w") as output:
            result = subprocess.run(command, cwd=build, stdout=output, stderr=subprocess.STDOUT)
        passed = result.returncode == 0
        if passed and args.mode == "options":
            text = log.read_text()
            names = ("leibniz_activation_time", "cantor_activation_time",
                     "asert_half_life")
            counts = {name: len(re.findall(r'app\s*\.\s*add_option\s*\(\s*"--fork\.'
                                          + name + r'"', text)) for name in names}
            expected = 1 if currency == "BCH" else 0
            passed = all(count == expected for count in counts.values())
            print(f"{currency}: expected {expected} registrations each; observed {counts}", flush=True)
        failed |= not passed
        print(f"{currency}: {'PASS' if passed else 'FAIL'}; compiler exit {result.returncode}; "
              f"full output: {log.resolve()}", flush=True)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())

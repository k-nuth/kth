#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check both dispatch modes against hashlib and inspect the compiled object.

CXX/CXXFLAGS describe the compiler and target, not the machine running it.
Backend flags are applied only to backend objects, just as in infrastructure's
CMake build. The primary object must select from its own target features.
"""
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CRYPTO = ROOT / "src/infrastructure/third-party/bitcoin-core"
CXX = shlex.split(os.environ.get("CXX", "c++"))
FLAGS = shlex.split(os.environ.get("CXXFLAGS", ""))


def run(args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs)


def main():
    macros = run(CXX + FLAGS + ["-dM", "-E", "-x", "c++", "-"], input="")
    backends = []
    defines = []
    if "#define __aarch64__ " in macros:
        defines = ["-DENABLE_ARM_SHANI"]
        backends = [("sha256_arm_shani.cpp", ["-march=armv8-a+crypto"])]
    elif "#define __x86_64__ " in macros:
        defines = ["-DENABLE_SSE41", "-DENABLE_AVX2", "-DENABLE_X86_SHANI"]
        backends = [("sha256_sse4.cpp", ["-msse4.1"]),
                    ("sha256_sse41.cpp", ["-msse4.1"]),
                    ("sha256_avx2.cpp", ["-mavx", "-mavx2"]),
                    ("sha256_x86_shani.cpp", ["-msse4.1", "-msha"])]
    data = bytes((i * 37 + 11) % 256 for i in range(64 * 17 + 1))[1:]
    expected = [hashlib.sha256(data).hexdigest()]
    for count in [0, 1, 2, 3, 4, 7, 8, 9, 17]:
        digest = b"".join(hashlib.sha256(hashlib.sha256(data[i*64:(i+1)*64]).digest()).digest()
                          for i in range(count)).hex()
        expected.extend([digest, digest])
    # Unoptimized too: the direct calls are a property of the source (calls
    # through constant function pointers), not of an optimization pass.
    for optimization in ["-O2", "-O0"]:
        check(optimization, defines, backends, expected)


def check(optimization, defines, backends, expected):
    with tempfile.TemporaryDirectory(prefix="sha256-dispatch-") as directory:
        work = Path(directory)
        common = CXX + FLAGS + ["-std=c++23", optimization, "-I" + str(CRYPTO)] + defines
        objects = []
        for name, extra in backends:
            obj = work / (name + ".o")
            subprocess.run(common + extra + ["-c", str(CRYPTO / "crypto" / name), "-o", str(obj)], check=True)
            objects.append(str(obj))
        for mode, extra in [("runtime", []), ("compile_time", ["-DKTH_SHA256_COMPILE_TIME=1"]),
                            ("portable", ["-DKTH_SHA256_COMPILE_TIME=1", "-DDISABLE_OPTIMIZED_SHA256"])]:
            obj = work / (mode + ".o")
            binary = work / mode
            subprocess.run(common + extra + ["-c", str(CRYPTO / "crypto/sha256.cpp"), "-o", str(obj)], check=True)
            symbols = run(["nm", "-C", str(obj)])
            # The original runtime dispatch has five mutable function pointers.
            pointer_data = re.findall(r"[ \t][bBdD][ \t].*\bTransform(?:D64(?:_[248]way)?)?$", symbols, re.M)
            if pointer_data and mode != "runtime":
                # An unoptimized ELF object keeps the constant pointers as
                # data that is read-only once relocated (.data.rel.ro). That is
                # not a mutable pointer: only writable sections count.
                table = run(["objdump", "-t", "-C", str(obj)])
                pointer_data = [line for line in table.splitlines()
                                if re.search(r"\bTransform(?:D64(?:_[248]way)?)?$", line)
                                and re.search(r"\s\.(?:data|bss)(?!\.rel\.ro)\S*\s", line)]
            if mode == "runtime":
                assert not backends or len(pointer_data) >= 2, symbols
            else:
                assert not pointer_data, symbols
                assert "AVXEnabled" not in symbols
                assert "sysctlbyname" not in symbols and "getauxval" not in symbols
                assert "GLOBAL__sub_I" not in symbols
                # Check the actual instructions too: no function-pointer calls
                # or tail jumps in the fixed-target primary SHA object.
                assembly = run(["objdump", "-d", str(obj)])
                assert not re.search(r"\b(?:blr|br)\s+x\d+|\b(?:callq?|jmpq?)\s+\*", assembly), assembly
                assert not re.search(r"\b(?:cpuid|xgetbv)\b", assembly), assembly
            subprocess.run(common + extra + [str(ROOT / "tests/sha256-dispatch/main.cpp"), str(obj),
                                            *objects, "-o", str(binary)], check=True)
            output = run([str(binary)]).splitlines()
            assert output[1:] == expected, (mode, output)
            if mode == "compile_time" and os.environ.get("EXPECTED_CT_IMPLEMENTATION"):
                assert output[0] == "implementation=" + os.environ["EXPECTED_CT_IMPLEMENTATION"], output[0]
            if mode == "portable":
                assert output[0] == "implementation=standard", output[0]
            print(optimization + " " + mode + ": " + output[0] + "; vectors and dispatch structure passed", flush=True)


if __name__ == "__main__":
    main()

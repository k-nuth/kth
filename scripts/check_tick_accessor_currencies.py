#!/usr/bin/env python3
"""Compile, link and run the real tick accessor definitions with BTC/LTC guards.

Unrelated baseline errors currently prevent complete BTC/LTC header/TU builds.
This scoped linkage control uses the real BCH class/constructor as a fixture,
then switches currency before compiling the unchanged tick-definition section
of chain_state.cpp. It does not claim a complete non-BCH build.
"""
import argparse
import pathlib
import shlex
import subprocess
import tempfile

ACCESSORS = ("is_faster_blocks_active", "ticks", "tick", "parent_tick", "block_interval")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("build", type=pathlib.Path, help="configured BCH Unix Makefiles build")
    cli.add_argument("--source", type=pathlib.Path, help="alternate actual chain_state.cpp")
    cli.add_argument("--log-dir", type=pathlib.Path)
    args = cli.parse_args()
    build = args.build.resolve()
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    flags = {}
    target = build / "src/domain/CMakeFiles/domain.dir"
    for line in (target / "flags.make").read_text().splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    source = args.source or pathlib.Path(cache["CMAKE_HOME_DIRECTORY"]) / "src/domain/src/chain/chain_state.cpp"
    text = source.read_text()
    # Preserve the actual preprocessor guards as well as every definition.
    section = text[text.index("// Tick schedule."):text.index("// Forks.", text.index("// Tick schedule."))]
    logs = args.log_dir or pathlib.Path(tempfile.mkdtemp(prefix="tick-accessors-"))
    logs.mkdir(parents=True, exist_ok=True)
    tools = {}
    for key in ("CMAKE_CXX_COMPILER", "CMAKE_NM", "CMAKE_OBJCOPY", "CMAKE_AR"):
        path = pathlib.Path(cache.get(key, ""))
        if not path.is_file():
            cli.error(f"configured {key} is missing or unavailable: {path}")
        tools[key] = str(path)
    compiler = tools["CMAKE_CXX_COMPILER"]
    compile_flags = [*flags["CXX_DEFINES"], *flags["CXX_INCLUDES"], *flags["CXX_FLAGS"]]
    original = target / "src/chain/chain_state.cpp.o"
    # Keep the actual constructor and layout, but prevent the existing BCH
    # accessor definitions from satisfying the symbols this control observes.
    symbols = subprocess.check_output([tools["CMAKE_NM"], "-g", "--defined-only", str(original)], text=True).splitlines()
    names = [line.split()[-1] for line in symbols if len(line.split()) >= 3]
    demangled_lines = subprocess.check_output(
        [tools["CMAKE_NM"], "-g", "--defined-only", "--demangle", str(original)], text=True).splitlines()
    demangled = [line.split(None, 2)[2] for line in demangled_lines if len(line.split(None, 2)) == 3]
    selected = {method: symbol for symbol, name in zip(names, demangled)
                for method in ACCESSORS if name == f"kth::domain::chain::chain_state::{method}() const"}
    if set(selected) != set(ACCESSORS):
        raise RuntimeError(f"expected all five original symbols, found {selected}")
    fixture = logs / "constructor-fixture.o"
    subprocess.run([tools["CMAKE_OBJCOPY"], *[f"--redefine-sym={symbol}=probe_original_{symbol}" for symbol in selected.values()],
                    str(original), str(fixture)], check=True)
    archive = logs / "libdomain-fixture.a"
    archive.write_bytes((build / "src/domain/libdomain.a").read_bytes())
    subprocess.run([tools["CMAKE_AR"], "d", str(archive), "chain_state.cpp.o"], check=True)
    driver = logs / "driver.cpp"
    driver.write_text('''#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/constants.hpp>
#include <limits>
int main() {
    using namespace kth;
    using namespace kth::domain;
    chain::chain_state::data values{};
    values.height = 12;
    values.bits.self = 0x207fffff;
    values.version.self = 4;
    values.timestamp.self = 1700007200;
    values.timestamp.retarget = 1700000000;
    for (unsigned i = 0; i != 11; ++i) {
        values.bits.ordered.push_back(0x207fffff);
        values.version.ordered.push_back(4);
        values.timestamp.ordered.push_back(1700000000 + 600 * i);
    }
    auto const cfg = chain::abla::default_config(static_max_block_size(config::network::regtest), false);
    values.abla_state = chain::abla::state(cfg, 0);
    chain::chain_state::checkpoints const checkpoints{};
    chain::chain_state state(std::move(values), script_flags_t{}, checkpoints,
        config::network::regtest, {0, 1700000000, 0x207fffff}, 3600, cfg,
        cantor_t(std::numeric_limits<uint64_t>::max()));
    if (state.is_faster_blocks_active()) return 1;
    if (!state.ticks().schedule().empty()) return 2;
    if (state.tick() != 7200) return 3;
    if (state.parent_tick() != 6600) return 4;
    if (state.block_interval() != 600) return 5;
}
''')
    driver_obj = logs / "driver.o"
    with (logs / "fixture-build.log").open("w") as output:
        subprocess.run([compiler, *compile_flags, "-c", str(driver), "-o", str(driver_obj)],
                       cwd=build, stdout=output, stderr=subprocess.STDOUT, check=True)
    link = shlex.split((build / "src/domain/CMakeFiles/kth_domain_test.dir/link.txt").read_text())
    link = [arg for arg in link if not arg.endswith(".cpp.o") and not arg.startswith("-Wl,--dependency-file=")]
    failed = False
    for currency in ("BTC", "LTC"):
        unit = logs / f"accessors-{currency}.cpp"
        unit.write_text('#include <kth/domain/chain/chain_state.hpp>\n'
                        '#undef KTH_CURRENCY_BCH\n' + f'#define KTH_CURRENCY_{currency}\n'
                        'namespace kth::domain::chain {\n' + section + '\n}\n')
        obj, binary = logs / f"accessors-{currency}.o", logs / f"accessors-{currency}"
        with (logs / f"{currency}-build.log").open("w") as output:
            result = subprocess.run([compiler, *compile_flags, "-c", str(unit), "-o", str(obj)],
                                    cwd=build, stdout=output, stderr=subprocess.STDOUT)
            if result.returncode == 0:
                command = [str(archive) if arg == "libdomain.a" else arg for arg in link]
                pos = command.index("-o")
                command[pos + 1] = str(binary)
                command[pos:pos] = [str(driver_obj), str(obj), str(fixture)]
                result = subprocess.run(command, cwd=build / "src/domain", stdout=output, stderr=subprocess.STDOUT)
        if result.returncode == 0:
            result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (logs / f"{currency}-run.log").write_bytes(result.stdout)
        passed = result.returncode == 0
        failed |= not passed
        print(f"{currency}: {'PASS' if passed else 'FAIL'}; exit {result.returncode}; logs {logs}", flush=True)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())

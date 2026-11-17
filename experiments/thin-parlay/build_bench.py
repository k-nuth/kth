"""Build against a configured static Knuth Makefiles graph; save exact commands."""

import argparse, json, pathlib, re, shlex, subprocess

p = argparse.ArgumentParser()
p.add_argument("--node-build", type=pathlib.Path, required=True)
p.add_argument("--parlay-source", type=pathlib.Path, required=True)
p.add_argument("--out", type=pathlib.Path, required=True)
p.add_argument("--boost-include", type=pathlib.Path)
p.add_argument("--sanitize", default="", choices=["", "address,undefined", "thread"])
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
a.out.mkdir(parents=True, exist_ok=True)
cache = (a.node_build / "CMakeCache.txt").read_text()
old = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.M).group(1)
flags = (
    a.node_build / "src/blockchain/CMakeFiles/blockchain.dir/flags.make"
).read_text()
args = []
for name in ["CXX_DEFINES", "CXX_INCLUDES"]:
    args += shlex.split(
        re.search("^" + name + " = (.*)$", flags, re.M).group(1).replace(old, str(root))
    )
link_dir = a.node_build / "src/node-exe"
link = shlex.split((link_dir / "CMakeFiles/node-exe.dir/link.txt").read_text())
libs = []
for x in link:
    if x.endswith(".a"):
        path = (pathlib.Path(x) if x.startswith("/") else link_dir / x).resolve()
        if path.name not in [
            "libnode.a",
            "libblockchain.a",
            "libdatabase.a",
            "libnetwork.a",
            "libutxoz.a",
            "liblmdb.a",
        ]:
            libs.append(str(path))
compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.*)$", cache, re.M).group(1)
if a.boost_include:
    args += ["-I" + str(a.boost_include)]
probe = subprocess.run(
    [
        compiler,
        "-std=c++23",
        *args,
        "-dM",
        "-E",
        "-x",
        "c++",
        "-include",
        "boost/version.hpp",
        "-",
    ],
    input="",
    capture_output=True,
    text=True,
    check=True,
)
boost_version = int(
    re.search(r"^#define BOOST_VERSION (\d+)$", probe.stdout, re.M).group(1)
)
args += ["-DBENCH_EXPECT_BOOST=" + str(boost_version)]
print("Using Boost version", boost_version, flush=True)
commands = {}
for backend in ["cfm", "parlay"]:
    cmd = [
        compiler,
        "-std=c++23",
        "-O2",
        *args,
        "-I" + str(root / "src/network/include"),
        "-I" + str(a.parlay_source / "include"),
    ]
    if backend == "parlay":
        cmd += ["-DBENCH_PARLAY=1"]
    cmd += [
        str(root / "experiments/thin-parlay/bench.cpp"),
        *libs,
        "-o",
        str(a.out / backend),
    ]
    if a.sanitize:
        cmd += ["-fsanitize=" + a.sanitize, "-fno-omit-frame-pointer"]
    commands[backend] = cmd
    with (a.out / (backend + "-build.log")).open("w") as log:
        subprocess.run(cmd, stdout=log, stderr=log, check=True)
(a.out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
print("Built CFM and Parlay with a shared driver and matched flags", flush=True)

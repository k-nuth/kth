import hashlib
import argparse, json, math, os, pathlib, platform, subprocess, time

p = argparse.ArgumentParser()
p.add_argument("--build", type=pathlib.Path, required=True)
p.add_argument(
    "--source-dir", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent
)
p.add_argument("--out", type=pathlib.Path, required=True)
p.add_argument("--parlay-source", type=pathlib.Path, required=True)
p.add_argument("--sizes", type=int, nargs="+", default=[131072, 1048576])
p.add_argument("--threads", type=int, nargs="+", default=[1, 4, 8])
p.add_argument(
    "--types",
    nargs="+",
    choices=["mempool", "spent_by", "pending_ip", "host_address"],
    default=["mempool", "spent_by", "pending_ip", "host_address"],
)
p.add_argument("--pairs", type=int, default=3)
p.add_argument("--seconds", type=float, default=0.35)
p.add_argument("--timeout", type=float, default=180)
a = p.parse_args()
if (
    any(n < 64 or n > 1048576 or n & (n - 1) for n in a.sizes)
    or any(t < 1 or t > 16 for t in a.threads)
    or not 0 < a.pairs <= 20
    or not 0 < a.seconds <= 10
    or not 0 < a.timeout
):
    p.error("invalid sizes, threads, pairs or duration")
if any(len(values) != len(set(values)) for values in [a.sizes, a.threads, a.types]):
    p.error("duplicate matrix entries")
a.out.mkdir(parents=True, exist_ok=False)
root = pathlib.Path(__file__).resolve().parents[2]


def git(*args, cwd=root):
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


meta = {k: str(v) if isinstance(v, pathlib.Path) else v for k, v in vars(a).items()}
meta.update(
    commit=git("rev-parse", "HEAD"),
    dirty=bool(git("status", "--porcelain")),
    parlay_commit=git("rev-parse", "HEAD", cwd=a.parlay_source),
    parlay_dirty=bool(git("status", "--porcelain", cwd=a.parlay_source)),
    platform=platform.platform(),
    started=time.time(),
    protocol="thin-adapters-paired-processes-v2",
    note="Actual node key/value shapes; synthetic full-range uniform keys, no P2P or full-node throughput. Mixed: 50% reads, 25% inserts, 25% erases. Hot update: 80% of requests to 64 keys. Same salts; IP hasher reproduces node SipHash with explicit salts. CFM reserves n; Parlay receives n as native constructor hint. CFM updates use in-place visit; Parlay uses a pure copy-transform on existing keys. Size is timed separately. Process peak RSS includes keys, values and all phases; not map-only allocation. No CPU pinning or dedicated-host isolation.",
)
if (a.build / "commands.json").exists():
    meta["build_commands"] = json.loads((a.build / "commands.json").read_text())
meta["cpu"] = (
    subprocess.check_output(
        ["sysctl", "-n", "machdep.cpu.brand_string"], text=True
    ).strip()
    if platform.system() == "Darwin"
    else platform.processor()
)
meta["binary_sha256"] = {
    b: hashlib.sha256((a.build / b).read_bytes()).hexdigest() for b in ["cfm", "parlay"]
}
meta["source_sha256"] = {
    name: hashlib.sha256((a.source_dir / name).read_bytes()).hexdigest()
    for name in ["bench.cpp", "adapters.hpp"]
}
if "build_commands" in meta:
    meta["compiler_version"] = subprocess.check_output(
        [meta["build_commands"]["cfm"][0], "--version"], text=True
    )
meta["load_start"] = os.getloadavg()
(a.out / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
base = {
    "insert",
    "find_hit",
    "find_miss",
    "duplicate",
    "mixed_50read_25insert_25erase",
    "traverse",
    "size_latency",
    "erase",
}
with (a.out / "samples.jsonl").open("w") as samples, (a.out / "processes.jsonl").open(
    "w"
) as processes:
    for kind in a.types:
        expected = base | (
            {"update_uniform", "update_hot64_80pct"}
            if kind in ["mempool", "spent_by"]
            else set()
        )
        for n in a.sizes:
            for threads in a.threads:
                for pair in range(-1, a.pairs):
                    order = ["cfm", "parlay"] if pair % 2 == 0 else ["parlay", "cfm"]
                    record = []
                    for backend in order:
                        stem = f"{kind}-n{n}-p{threads}-pair{pair}-{backend}"
                        cmd = [
                            str(a.build / backend),
                            kind,
                            str(n),
                            str(threads),
                            str(a.seconds),
                        ]
                        try:
                            r = subprocess.run(
                                cmd,
                                capture_output=True,
                                text=True,
                                timeout=a.timeout,
                                env=dict(os.environ, PARLAY_NUM_THREADS="4"),
                            )
                        except subprocess.TimeoutExpired as e:
                            (a.out / (stem + ".timeout")).write_bytes(
                                (e.stdout or b"") + b"\n" + (e.stderr or b"")
                            )
                            raise
                        (a.out / (stem + ".stdout")).write_text(r.stdout)
                        (a.out / (stem + ".stderr")).write_text(r.stderr)
                        if r.returncode:
                            raise RuntimeError(
                                f"{stem}: exit {r.returncode}: {r.stderr[-500:]}"
                            )
                        rows = [
                            json.loads(x)
                            for x in r.stdout.splitlines()
                            if x.startswith("{")
                        ]
                        info = [x for x in rows if x["phase"] == "build_info"]
                        if len(info) != 1 or info[0]["backend"] != backend:
                            raise RuntimeError("missing build/version info: " + stem)
                        boost = info[0]["boost_version"]
                        if "boost_version" in meta and meta["boost_version"] != boost:
                            raise RuntimeError("mixed Boost versions")
                        meta["boost_version"] = boost
                        rows = [x for x in rows if x["phase"] != "build_info"]
                        peak = [x for x in rows if x["phase"] == "process_peak_rss"]
                        rows = [x for x in rows if x["phase"] != "process_peak_rss"]
                        if (
                            len(peak) != 1
                            or {x["phase"] for x in rows} != expected
                            or len(rows) != len(expected)
                        ):
                            raise RuntimeError("missing/duplicate phases: " + stem)
                        for x in rows:
                            effective = (
                                1
                                if x["phase"] in ["traverse", "size_latency"]
                                else threads
                            )
                            if (
                                x["backend"] != backend
                                or x["type"] != kind
                                or x["n"] != n
                                or x["threads"] != effective
                            ):
                                raise RuntimeError("config mismatch: " + stem)
                            if x["phase"] == "size_latency":
                                if (
                                    x["calls"] != 5
                                    or not math.isfinite(x["ns_per_call"])
                                    or x["ns_per_call"] <= 0
                                ):
                                    raise RuntimeError("size latency: " + stem)
                            elif (
                                not math.isfinite(x["mops"])
                                or x["seconds"] <= 0
                                or x["ops"] <= 0
                            ):
                                raise RuntimeError("invalid sample: " + stem)
                            if x["phase"] in ["insert", "erase"] and (
                                x["ops"] != n
                                or x["balance"] != (n if x["phase"] == "insert" else -n)
                            ):
                                raise RuntimeError("operation accounting: " + stem)
                            record.append(dict(x, pair=pair, requested_threads=threads))
                        if pair >= 0:
                            processes.write(
                                json.dumps(
                                    dict(
                                        backend=backend,
                                        type=kind,
                                        n=n,
                                        requested_threads=threads,
                                        pair=pair,
                                        peak_rss_bytes=peak[0]["bytes"],
                                    )
                                )
                                + "\n"
                            )
                            processes.flush()
                    if pair >= 0:
                        for x in record:
                            samples.write(json.dumps(x) + "\n")
                        samples.flush()
                    print(f"{kind} n={n} threads={threads} pair={pair} OK", flush=True)
meta["finished"] = time.time()
meta["load_end"] = os.getloadavg()
(a.out / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")

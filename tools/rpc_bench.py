#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Saturation load generator for the Knuth JSON-RPC server.

N concurrent clients each fire RPC requests back-to-back over a kept-alive
connection for a fixed duration; reports throughput and latency percentiles.

This is the miner-polling HALF of a realistic mining benchmark. A faithful bench
is a network SIMULATION: transactions arriving (mempool churn), blocks arriving
(tip advances, which invalidates the template), and miners polling GBT at the
same time -- only then does the per-call template rebuild actually cost anything
(an empty mempool makes getblocktemplatelight as cheap as getblockcount). Driving
the tx/block inflow needs sendrawtransaction + submitblock, added in later PRs;
until then this measures the polling side alone.

Dependency-free (stdlib only). Example:

    tools/rpc_bench.py --url http://127.0.0.1:8332/ --user u --password p \\
        --method getblocktemplatelight --concurrency 16 --duration 10
"""

import argparse
import base64
import http.client
import json
import statistics
import threading
import time
from urllib.parse import urlparse


def worker(args, auth, stop_at, out):
    """Fire requests until stop_at; append per-request latencies (seconds)."""
    u = urlparse(args.url)
    body = json.dumps(
        {"jsonrpc": "2.0", "id": 1, "method": args.method, "params": []}
    ).encode()
    headers = {
        "Authorization": "Basic " + auth,
        "Content-Type": "application/json",
        "Connection": "keep-alive",
    }
    latencies, errors = [], 0
    conn = http.client.HTTPConnection(u.hostname, u.port or 8332, timeout=30)
    while time.monotonic() < stop_at:
        t0 = time.monotonic()
        try:
            conn.request("POST", u.path or "/", body=body, headers=headers)
            resp = conn.getresponse()
            payload = resp.read()
            if resp.status != 200 or b'"error":null' not in payload:
                errors += 1
            latencies.append(time.monotonic() - t0)
        except Exception:
            errors += 1
            try:
                conn.close()
            except Exception:
                pass
            conn = http.client.HTTPConnection(u.hostname, u.port or 8332, timeout=30)
    conn.close()
    out.append((latencies, errors))


def pct(values, p):
    if not values:
        return 0.0
    k = max(0, min(len(values) - 1, int(round((p / 100.0) * (len(values) - 1)))))
    return sorted(values)[k]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="http://127.0.0.1:8332/")
    ap.add_argument("--user", default="")
    ap.add_argument("--password", default="")
    ap.add_argument("--method", default="getblocktemplatelight")
    ap.add_argument("--concurrency", type=int, default=8)
    ap.add_argument("--duration", type=float, default=10.0)
    args = ap.parse_args()

    auth = base64.b64encode(f"{args.user}:{args.password}".encode()).decode()

    results, threads = [], []
    lock_free = []  # each thread appends its own tuple; list append is atomic in CPython
    stop_at = time.monotonic() + args.duration
    wall0 = time.monotonic()
    for _ in range(args.concurrency):
        t = threading.Thread(target=worker, args=(args, auth, stop_at, lock_free))
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    wall = time.monotonic() - wall0

    all_lat = [x for lats, _ in lock_free for x in lats]
    errors = sum(e for _, e in lock_free)
    total = len(all_lat)

    print(f"method       : {args.method}")
    print(f"concurrency  : {args.concurrency}")
    print(f"duration     : {wall:.2f} s")
    print(f"requests     : {total}  (errors: {errors})")
    print(f"throughput   : {total / wall:.0f} req/s")
    if all_lat:
        ms = lambda s: s * 1000.0
        print(f"latency mean : {ms(statistics.fmean(all_lat)):.2f} ms")
        print(f"latency p50  : {ms(pct(all_lat, 50)):.2f} ms")
        print(f"latency p90  : {ms(pct(all_lat, 90)):.2f} ms")
        print(f"latency p99  : {ms(pct(all_lat, 99)):.2f} ms")
        print(f"latency max  : {ms(max(all_lat)):.2f} ms")


if __name__ == "__main__":
    main()

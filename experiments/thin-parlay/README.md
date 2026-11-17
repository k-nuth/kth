# Thin Parlay operation adapters

`parlay_lookup_map` and `parlay_lookup_set` each contain exactly one member: the
native container. Static assertions enforce that the adapter adds no object
size. There are no adapter locks, stripes, counters, shared entries or owning
pointers. Per-operation locals only translate arguments and return values.

The map supports insert-if-absent, const visitation, contains, erase, pure
transforms (`Upsert`) and epoch-protected read-only traversal. The set supports
insert, contains, erase and traversal. Const visitors borrow entries only during
the call. Transforms can be retried and must have no external side effects.
Traversal is weak, not a coherent snapshot, and does not permit concurrent
clear/destruction. It returns no iterator or escaping borrowed reference.

This requires the ParlayHash `feature/stateful-hash-policies` change, not released
0.3.0: native ownership of per-instance Hash/KeyEqual, protected `ForEach`, and
nested/exception-safe epoch scopes. Stateful policy pointers exist only in
operation context; they are not stored in table entries. The empty-policy fast
path and existing bucket payload layout remain intact.

```
cmake -S experiments/thin-parlay -B /tmp/thin-adapters \
  -DPARLAYHASH_SOURCE_DIR=/path/to/parlayhash-policy-checkout
cmake --build /tmp/thin-adapters
ctest --test-dir /tmp/thin-adapters --output-on-failure
```

Use `-DSANITIZER=address,undefined` or `thread` for instrumented controls.
`test.cpp` covers size parity, policies, equivalent keys, growth, duplicate
claims, transforms and concurrent traversal. `knuth_types.cpp` additionally
checks actual node mempool/spent-by/IP/host types when compiled with the node's
normal dependency flags; it is not part of the dependency-free control target.

Deliberate limits: native size is still a scan, exposed as `slow_size()` so it
cannot silently replace a hot constant-time call. Mutable CFM visitors and
header-index publication are absent. Production selection is not changed by
these experiments; the rejected general adapter was removed from this PR and retained only in
the local `archive/general-concurrent-adapter` reference for reproducing its measurements. These thin adapters are not yet a production mempool
replacement, because its frequent size queries still need a native solution.
Keep instances scoped: the separate native global-destruction defect is outside
this experiment. No unreviewed library build is substituted into the Conan
requirement of the node.

## Paired node-type benchmarks

With an existing **static native Knuth build generated with Unix Makefiles**:

```
python3 experiments/thin-parlay/build_bench.py \
  --node-build /path/to/configured-node-build \
  --parlay-source /path/to/parlayhash-policy-checkout --out /tmp/thin-build \
  --boost-include /path/to/boost-1.92/include
python3 experiments/thin-parlay/run_bench.py --build /tmp/thin-build \
  --parlay-source /path/to/parlayhash-policy-checkout --out /path/to/new-results
```

The builder reuses the configured dependency definitions/include paths and
supporting static archives, compiles one shared driver twice in C++23 with `-O2`,
and records exact commands. The builder probes the selected Boost headers; a compile-time assertion and a runtime build-info row verify the version in both binaries. It does not rebuild or benchmark a complete node.
The runner records source/binary hashes, revisions and compiler information,
alternates backend order, excludes a warmup pair, keeps unique process logs,
applies timeouts and validates output before recording each completed pair.

Workloads use the real mempool-entry, spent-by, IP and host-address key/value
shapes. Keys are synthetic; timed access streams cover the full configured
range, not a short repeated working set. Mempool values hold distinct transaction
shared pointers; lookups copy a shared pointer as the real get path does.
IP hashing repeats the node's SipHash computation with explicit reproducible
salts, because its current random-salt hasher has no explicit-salt constructor.
Both backends use identical seeds and hash algorithms. CFM reserves the entry
count; Parlay receives the count as its native construction hint. Actual layout,
load and capacity policies remain implementation-specific.

Measure successful inserts, hit/miss lookups, duplicate rejection, mixed traffic
(50% reads / 25% inserts / 25% erases), traversal and erase. Map updates increment
existing values, uniformly or with 80% of requests targeting 64 hot keys. CFM
uses its native in-place visitor for that increment; Parlay uses a pure
copy-transform. Their quiescent counter sums must match the completed operation
count. Mixed insert/erase balances and final payloads are also checked.

Size-query latency is a separate diagnostic: five calls are timed before output formatting. Mixed traffic starts from a full table and evolves during each timed phase; this is a finite transition workload, not a fixed-density steady-state claim. Traversal is timed after writers
join. Peak RSS includes input keys, payloads, all workload phases and cleanup;
it is **not** map-only allocation or bytes per entry. Latencies are sampled every
1,024 operations and include validation costs. Finite insert/erase/traversal
phases can be short; no CPU affinity, frequency controls or exclusive-host
isolation are imposed. Report ranges and medians, not full-node speedup claims.

### Apple M6 screening, 2026-11-17

Apple M6 (12 logical CPUs, 24 GiB), macOS 27.0.1, Apple Clang 21,
C++23 `-O2`. The header-only [Boost.Unordered concurrent containers](https://www.boost.org/doc/libs/1_92_0/libs/unordered/doc/html/unordered/concurrent.html)
were compiled from Boost 1.92.0; supporting node archives came from an existing
native build. ParlayHash used dependency commit
`45278dd50c7ac72a8412af718ab74c478e80df27`.

The matrix covered 131,072 and 1,048,576 entries, 1/4/8 workers, all four types,
three measured process pairs and an excluded warmup pair per configuration.
Each timed traffic phase lasted 0.35 seconds. All 1,296 recorded phase rows
passed consistency checks. A small four-type control passed ASan+UBSan for both
backends with the same Boost headers. These are container experiments with real
node types and synthetic traffic, not full mempool or node benchmarks.

Selected medians at 1,048,576 entries and eight workers, in million operations/s
(higher is better):

| Type / operation | CFM | Thin Parlay |
| --- | ---: | ---: |
| Mempool / hit lookup | 38.72 | 34.57 |
| Mempool / insert | 23.75 | 33.50 |
| Mempool / hot update | 64.96 | 21.00 |
| Spent-by / hit lookup | 51.03 | 37.25 |
| Spent-by / erase | 26.20 | 32.44 |
| Pending IP / insert | 37.34 | 49.16 |
| Pending IP / hit lookup | 71.50 | 56.72 |
| Host address / hit lookup | 39.36 | 13.48 |

Eight-writer wins do not represent the node's currently serialized mempool
writes. With one writer, mempool insertion was essentially tied (15.84 versus
15.93), while erase favored CFM (17.15 versus 6.79). Read and mixed traffic
favored CFM across the four large-table types.

Native size scans remain a production blocker: at the large size, Parlay's
median query took approximately 0.90 ms for mempool, 24.85 ms for spent-by,
0.81 ms for pending IP and 435 ms for host addresses. CFM queries were below
0.12 microseconds in these samples; these tiny timings are timer-limited.

A separate three-pair experiment forced native indirect storage, keeping the
adapter's single-member layout. At the large size and eight workers, spent-by
insertion improved to 46.74 versus its paired CFM control's 30.95; hit lookup
still favored CFM (51.63 versus 46.47). Its size scan fell to 0.88 ms. Host
address lookup remained behind (39.77 versus 19.69), with a 46.81 ms size scan.
This motivates reviewing native storage selection and host-hash distribution,
without adding adapter locks or counters. It does not justify a production
backend change yet.

Raw paired logs, metadata, summaries, source snapshots and plots are retained
locally under the dated `thin-boost192` and `thin-boost192-indirect` result
folders. Earlier runs using Boost 1.91 are marked superseded and are excluded
from these numbers.

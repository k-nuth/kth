# Coinbase Merkle branch benchmark

With a native build configured with `ENABLE_TEST=ON`:

```sh
cmake --build <build-dir> --target kth_node_bench_coinbase_merkle
<build-dir>/src/node/kth_node_bench_coinbase_merkle
```

Compares the original scalar implementation with the production batched builder.
Both include transaction hashing, allocation and checking the returned branch.
The benchmark enables the same SHA256 runtime dispatch as the node and reports
its selected implementation. It warms both paths and reports median microseconds
per call across seven rounds, alternating measurement order.

Inputs are synthetic transactions with empty inputs/outputs and distinct
locktimes. This isolates branch construction with small transactions; these are
not whole-RPC timings or a claim about representative block transaction sizes.
Every measured result must equal the original implementation's branch.

Example native GCC 16 release run, `x86_shani(1way;2way)`:

| Non-coinbase transactions | Original (us) | Batched (us) | Speedup |
|---:|---:|---:|---:|
| 1 | 0.188 | 0.089 | 2.12x |
| 16 | 3.137 | 2.076 | 1.51x |
| 256 | 43.801 | 32.694 | 1.34x |
| 4096 | 687.555 | 522.290 | 1.32x |
| 16384 | 2751.060 | 2093.890 | 1.31x |

Results depend on CPU, SHA256 dispatch and transaction sizes.

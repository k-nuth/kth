# `to_data` micro-benchmarks: legacy boost::iostreams vs `byte_writer`

We swapped the whole serialization stack on the domain layer of the Knuth
node — replacing `data_sink` (boost::iostreams `back_insert_device`) +
`ostream_writer` (`std::ostream`-backed writer) with a bounds-checked
`byte_writer` writing into a caller-owned `data_chunk` sized ahead of
time to `serialized_size(...)`. The results, per-type, on the same
machine, same compiler, same fixtures:

| type | shape | **old** (ns/op) | **new** (ns/op) | speedup | IPC old → new |
| :--- | :--- | ---: | ---: | :---: | :---: |
| `point` | wire | 70.0 | **4.77** | **14.7×** | 4.98 → 8.32 |
| `point` | store | 71.0 | **4.76** | **14.9×** | 4.93 → 8.47 |
| `script` | 25 B, prefix | 71.4 | 10.18 | 7.0× | 4.81 → 6.12 |
| `script` | 400 B, prefix | 79.6 | 10.66 | 7.4× | 4.82 → 6.69 |
| `input` | wire | 90.5 | 12.87 | 7.0× | 4.79 → 6.37 |
| `output` | wire | 78.5 | 11.69 | 6.7× | 4.86 → 6.47 |
| `transaction` | 1 in / 2 out | 156.8 | 34.78 | 4.5× | 4.58 → 6.42 |
| `transaction` | 20 in / 20 out | 1189 | 372.8 | 3.2× | 4.15 → 5.32 |
| `block` | 10 tx | 1370 | 411.4 | 3.3× | 4.18 → 5.89 |
| `block` | 500 tx | 66 210 | 19 583 | **3.4×** | 4.20 → 6.12 |

All numbers are median-of-a-warm-run under nanobench (error <0.5%).

### Why it moves this much

- **Old path**: every write on a domain type went `write_byte / write_hash
  / write_*_bytes_little_endian` → `ostream_writer::write_*` →
  `std::ostream::write` → `boost::iostreams::write_device_impl<sink>` →
  `back_insert_device::write` → `push_back` on the `data_chunk`. That's
  five to seven virtual/interface indirections plus per-byte reallocs
  if the vector grew.
- **New path**: `byte_writer` holds a `std::span<uint8_t>` over the
  pre-sized buffer, tracks a position, and calls `memcpy` when it can.
  Every write is a bounds-check + copy. No iostreams. No growing
  vector. The compiler can inline the whole call chain — see the IPC
  jump.

### Real-world take

`block::to_data() [500 tx]` is the shape you actually hit during IBD:
we drop from ~66 µs down to ~19.5 µs per block. Multiply by ~800k
blocks in a full sync and that's north of **half an hour of wall-clock
recovered** on serialization alone. Nothing exotic — we just stopped
paying the boost::iostreams tax.

### Reproduce

```bash
cmake --build --preset conan-release -j"$(nproc)" \
      --target kth_domain_bench_to_data
./build/build/Release/src/domain/kth_domain_bench_to_data
```

### Setup

- CPU: AMD Ryzen 9 9950X3D
- OS: Linux 6.19.5-zen1 x86_64
- Compiler: gcc 15.2.0, `-O3 -DNDEBUG`, `-march` with AVX2/BMI2/etc.
- Bench harness: [nanobench](https://github.com/martinus/nanobench)
- Fixtures: deterministic (fixed Mersenne-Twister seed) so the shape
  and byte content are identical on both branches.

### Branches

- Baseline (legacy `data_sink` path): [PR #387][pr387]
- Fast path (`byte_writer` via `kth::to_data_chunk`): [PR #394][pr394]

Same benchmark file lives on both branches; the source difference is
one call: `x.to_data(...)` → `kth::to_data_chunk(x, ...)`.

[pr387]: https://github.com/k-nuth/kth/pull/387
[pr394]: https://github.com/k-nuth/kth/pull/394

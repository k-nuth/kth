# secp256k1-turbo

Lane-parallel (SIMD) batch verification of secp256k1 signatures for Knuth.

Each signature is verified independently in its own SIMD lane, so the result
is a per-signature verdict with exactly the semantics of
`secp256k1_ecdsa_verify` (low-S required, keys parsed like
`secp256k1_ec_pubkey_parse`) and `secp256k1_schnorr_verify` (the BCH scheme).
This is *not* algebraic batch verification: no randomizers, no shared
multi-scalar multiplication, and ECDSA is supported.

## Backends

| backend | lanes | where |
|---|---|---|
| `avx512_ifma` | 8 | x86-64 with AVX512F/DQ/BW/VL + IFMA (Ice Lake+, Sapphire Rapids, Zen 4, Zen 5) |
| `cuda` | whole batch | NVIDIA GPUs (one thread per signature), see "GPU backends" |
| `metal` | whole batch | Apple GPUs (one thread per signature) |
| `scalar_portable` | 1 | any; same algorithm, used as the reference in tests |
| `secp256k1_fallback` | 1 | libsecp256k1, one signature at a time |

The AVX-512 kernel lives in its own translation unit compiled with the AVX-512
flags and is only entered after a CPUID check, so the library builds and runs
on the project's baseline `-march` and picks the kernel at run time. When the
whole build already targets an IFMA microarchitecture (`-march` defines
`__AVX512IFMA__`, as a Knuth `march_id` for such a CPU does) the choice is
made at compile time and no CPUID runs. `KTH_SECP256K1_TURBO_BACKENDS`
(`auto` | `avx512ifma` | `none`) selects what gets compiled in; non-x86
targets (NEON) compile no vector kernel and always use libsecp256k1.
`best_backend()` returns `avx512_ifma` when available, otherwise the
libsecp256k1 fallback (the portable 1-lane backend is slower than
libsecp256k1 and exists for testing). Batches of fewer than two signatures go
through libsecp256k1 regardless.

## Design

* `vec_scalar.hpp` / `vec_avx512ifma.hpp`: the minimal vector wrapper. The
  only arithmetic primitives are `madd52lo`/`madd52hi` (the IFMA
  instructions; the scalar backend mirrors their exact semantics, including
  reading only the low 52 bits of each multiplicand), add/sub, shifts, masks,
  blend and a gather.
* `fe52.hpp`: field arithmetic mod p in radix 2^52 (5 limbs). The invariant
  is stricter than libsecp256k1's: every limb of every multiplicand is below
  2^52, so every operation ends with a carry pass. Reduction uses
  2^260 mod p and 2^520 mod p; inverse, sqrt and the Euler criterion are
  fixed-exponent ladders (uniform across lanes).
* `group.hpp`: Jacobian doubling (dbl-2009-l) and the *complete* mixed
  addition ported from libsecp256k1's `secp256k1_gej_add_ge`, with the point
  at infinity as a mask. Nothing branches on lane data.
* `kernel.hpp`: `u1*G + u2*Q` per lane. `u2` is GLV-split into two signed
  128-bit halves walked with fixed 4-bit windows (32 steps of 4 doublings,
  two per-lane table lookups each, gathers into a 15-entry `d*Q` table
  built in-kernel with one batched inversion). `u1` uses a shared two-tooth
  comb table of 256 affine points (`c*G + c'*2^128*G`) aligned with the
  same ladder, one lookup per step. Digit 0 is handled by adding entry 1
  and blending the old accumulator back, so the data flow is identical in
  all lanes. The final ECDSA check compares `X == r*Z^2` (and `(r+n)*Z^2`
  when `r + n < p`); the Schnorr check adds the quadratic-residue test of
  `Y*Z`.
* `scalar256.hpp`: mod-n arithmetic on the scalar side (Montgomery
  multiplication, Fermat inverse amortised with Montgomery's trick across the
  whole batch, the GLV decomposition from libsecp256k1).
* `batch_verify.cpp`: parsing with libsecp256k1's acceptance rules,
  preprocessing, lane layout, dispatch, and the fallback.

## Results

One core, batch of 2048 signatures, `kth_secp256k1_turbo_bench` (nanobench).

AMD Ryzen 9 9950X3D (Zen 5, 5.3 GHz under load), GCC 16:

| | ECDSA ns/sig | Schnorr ns/sig |
|---|---:|---:|
| libsecp256k1, one at a time (parse + verify) | 19,569 | 19,924 |
| secp256k1-turbo, portable 1-lane | 68,810 | 73,890 |
| secp256k1-turbo, AVX-512 IFMA 8-lane | 5,336 | 6,086 |
| speed-up | **3.7x** | **3.3x** |

The per-signature cost is flat from a batch of 8 upwards (one kernel run
of 8 lanes costs about 45 us); a batch of 1 is slower than libsecp256k1,
hence the fallback.

Apple M6 (arm64, no vector backend), Apple clang 21:

| | ECDSA ns/sig | Schnorr ns/sig |
|---|---:|---:|
| libsecp256k1, one at a time (parse + verify) | 13,153 | 13,803 |
| secp256k1-turbo, portable 1-lane | 45,622 | 51,395 |

On arm64 `best_backend()` is the libsecp256k1 fallback: NEON has no 64x64
lane multiply, and the M6 already verifies a signature in 13 us with the
scalar int128 code, so there is nothing to gain from a 2-lane kernel there.
The portable backend exists for testing, not for speed.

## GPU backends

`gpu/kernel_core.h` is one source for the device code of both GPU runtimes
and for a host build of the same code that the tests use. It is written in
the subset common to CUDA, the Metal Shading Language and C++: no standard
headers, no templates, no recursion, address spaces behind macros. A thread
verifies one signature from the bytes the node has (key, hash, signature)
to the verdict:

* Field: radix 2^32, 8 limbs, lazy reduction (values below 2^256, not
  necessarily below p; comparisons account for the one non-canonical
  representative v + p). On CUDA the products are accumulated column by
  column (Comba) in a three-word accumulator with the carry-chain PTX
  instructions `mad.lo.cc` / `madc.hi.cc` / `addc`, which ptxas lowers to
  one `IMAD.WIDE` plus one carry add per partial product; the reduction and
  the cheap operations (add, sub, small multiples, the 2^256 fold) are
  carry chains too. Written row by row in C++ with 64-bit accumulators, the
  same arithmetic is 1.6-1.8x slower: the compiler serialises every row
  behind one carry. The C++ form remains for Metal and the host (a radix
  2^26 variant with carry-free column sums was measured as well: no faster
  on NVIDIA, 2.5x slower on Apple GPUs, where 64-bit integers are
  emulated). Square root and the Euler test use the libsecp256k1 addition
  chain; the inverse is the variable-time safegcd (30-bit divsteps, as
  libsecp256k1's `modinv32_var`), six times cheaper than the chain and fine
  here because everything a verifier handles is public.
* Scalars mod n on the device: s^-1 (safegcd again), u1 = z s^-1, u2 =
  r s^-1 (Montgomery multiplication, plain C++, a few per signature) and the
  GLV split of u2, so the host only converts byte order, checks the key
  encoding and, for Schnorr, hashes the challenge; a signature travels as
  164 bytes written straight into the runtime's pinned or shared buffer.
* Group law, 4-bit ladder, two-tooth comb for G, ECDSA x-check with the
  (r + n) case and the BCH Schnorr quadratic-residue test: the same algorithm
  as the lane kernel, including the complete addition formula, so a thread
  never diverges on the group law and no input has an exceptional path. The
  per-thread table of d * Q (d = 1..15, affine, with beta * x) is built in
  place with one inversion (Montgomery's trick); the G table (256 entries,
  16 KB) is staged in shared memory per block on CUDA and read from a
  constant buffer on Metal.
* Code size is a first-order effect on a GPU: with the ladder step written
  out (4 doublings, 3 additions) the loop body is 17,000 instructions, more
  than the instruction cache, and every field operation in it ran at twice
  its stand-alone latency. The ladder is written as rolled inner loops with
  one copy of the doubling and one of the addition; that alone took the
  per-batch latency from 1.08 ms to 0.72 ms.
* A pool of four contexts (stream + pinned buffers on CUDA, shared-memory
  buffers on Metal) lets batches from several threads overlap on the device.

Build: `KTH_SECP256K1_TURBO_GPU` (`auto` | `cuda` | `metal` | `none`; the
conan option `secp256k1_gpu`, default `none` for the node). CUDA needs nvcc
(`CUDACXX` or `CMAKE_CUDA_COMPILER`), compiles for the build machine's GPU by
default (`KTH_SECP256K1_TURBO_CUDA_ARCHITECTURES=native`; set a list for a
distributed build) and links the static runtime; Metal needs no toolchain:
the kernel source is embedded and compiled by the system Metal compiler on
first use. At run time `backend_available(cuda|metal)` reports whether a
device is present and the kernel is ready; `best_backend(n)` returns the GPU
backend for batches of at least `gpu_min_batch()` signatures (default 512,
initial value from `KTH_SECP256K1_TURBO_GPU_MIN_BATCH`, `off` to keep every
batch on the CPU) and `best_backend()` otherwise, and the deferred collector
uses it. A batch the device cannot run (no device, driver error) is verified
on the CPU. `KTH_SECP256K1_TURBO_TRACE=1` prints the host/device split of
every GPU batch to stderr.

Results, end to end (byte-order conversion on the host, upload, kernel,
download), valid ECDSA signatures with compressed keys,
`kth_secp256k1_turbo_bench gpu`:

| batch | RTX 5070 Ti (CUDA 13.2, sm_120) | Apple M6 (Metal) |
|---:|---:|---:|
| 256 | 2.80 us/sig | 18.6 us/sig |
| 1,024 | 0.71 us/sig | 4.70 us/sig |
| 4,096 | 0.195 us/sig | 1.43 us/sig |
| 16,384 | 0.075 us/sig | 1.10 us/sig |
| 65,536 | 0.071 us/sig | 1.04 us/sig |
| kernel alone, 16,384 | 0.062 us/sig | |
| kernel alone, 65,536 | 0.058 us/sig | |

For scale: one Zen 5 core with the AVX-512 IFMA lanes needs 5.4 us per
signature (the 16 cores together about 2.9 M/s; the 5070 Ti reaches 14 M/s),
and one M6 core with libsecp256k1 13.2 us. A batch has a latency floor of
0.72 ms on the 5070 Ti (the time one thread takes for one signature:
0.09 ms of checks and scalar work, 0.10 ms for the table, 0.5 ms for the
ladder), so the crossover with one CPU core sits near 150 signatures there
and near 400 on the M6 (default `gpu_min_batch` 512, which also keeps the
CPU lanes for the batches where they are just as fast). Schnorr (BCH) costs
about 50% more than ECDSA on the GPU at large batches (the quadratic-residue
test is a second exponentiation chain). Block size and register caps were
measured within 3%; the three-threads-per-signature variant (one scalar
multiplication each, partial sums combined through shared memory) was not
faster once the code size was fixed, and was dropped. Reading GPU
performance counters needs administrator rights on the driver, so the
kernel was tuned with `clock64` probes and ablations rather than a
profiler.

The tests run the kernel on the host against GMP and against the 5x52 lane
implementation (field, group law, G table, whole verifications), and when a
device is present also run a field self-test on it against the host results
and verify batches of 4,096 twice (the device must be deterministic). On
Metal the functions are forced inline: left as calls, the compiler produced
non-deterministic results for the long squaring chains.

## Tests and benchmark

`kth_secp256k1_turbo_test` (Catch2) checks the field and scalar arithmetic
against GMP, the kernel against libsecp256k1's public API (including
degenerate inputs: zero scalars, `Q = +-G`, sums that land on infinity), the
AVX-512 lanes against the portable backend bit for bit, and the public API
against libsecp256k1 on valid, corrupted and malformed inputs. The tests
are built with `KTH_SECP256K1_TURBO_CHECK_LIMBS`, which checks the 52-bit
operand invariant in the portable backend and aborts when it does not hold,
in Release builds too. `kth_secp256k1_turbo_bench`
(nanobench) produces the table above.

## Algebraic batch verification (BCH Schnorr only)

`verify_schnorr_bch_all` wraps `secp256k1_schnorr_verify_batch`, a local
addition to the vendored libsecp256k1 Schnorr module: with random 128-bit
`a_i` it checks `sum a_i*(s_i*G - e_i*P_i - R_i) == infinity` with one
multi-scalar multiplication, where `R_i` is `r_i` lifted to the point with a
square `y`. The `R_i` terms keep their 128-bit `a_i` whole (only the
full-width `e_i` and `G` scalars are split with the endomorphism, so a
signature costs three 128-bit terms, not four); batches of up to 16 use
Strauss' algorithm, larger ones Pippenger's bucket method with the buckets
summed in affine coordinates (one shared inversion per round). It gives one
verdict for the whole batch, not one per signature, and does not apply to
ECDSA (the `y` of `R` cannot be recovered from `r` alone). The randomizers
are derived from a hash of every input plus an optional caller seed.

Zen 5, one core (same conditions as above):

| batch size | libsecp256k1 one at a time | algebraic batch | AVX-512 IFMA lanes |
|---:|---:|---:|---:|
| 2 | 20,294 ns/sig | 17,269 | 23,690 |
| 8 | | 15,060 | 6,220 |
| 32 | | 11,787 | 6,209 |
| 128 | | 9,940 | 6,213 |
| 512 | | 9,096 | 6,216 |
| 2048 | | 8,421 (2.41x) | 6,221 (3.26x) |

Apple M6, one core (no vector backend):

| batch size | libsecp256k1 one at a time | algebraic batch |
|---:|---:|---:|
| 2 | 14,128 ns/sig | 12,409 |
| 8 | | 11,196 |
| 32 | | 10,201 |
| 128 | | 8,630 |
| 512 | | 7,710 |
| 2048 | | 7,248 (1.95x) |

The square root that lifts each `R_i` (and the one that decompresses each
key) stays per signature, so the batch levels off; on an IFMA machine the lane
kernel is still faster from 8 signatures up. Where there is no AVX-512 IFMA the
algebraic batch is the speed-up available for Schnorr inputs. The two are not
combined here: doing so would mean lifting the `R_i` with the vector field and
vectorising the bucket accumulation, which is a separate piece of work.

## Why there is no NEON or WebAssembly backend

The lane kernel only pays off when a lane can multiply 52-bit limbs in one
instruction. NEON and WASM SIMD128 have no 64x64 lane multiply, only
32x32 -> 64 (`umull` / `i64x2.extmul_i32x4`), which forces radix 2^26 with
ten limbs and 100 products per field multiplication. A probe of exactly that
(`fe10x26`, two elements per `v128`, built with Emscripten 6.0.13 `-O3
-msimd128`, run under node 24 / V8) against the same algorithm in scalar
WASM gives, per field multiplication per element:

| | scalar WASM | SIMD128, 2 lanes | ratio |
|---|---:|---:|---:|
| Apple M6 (V8 on arm64) | 33.9 ns | 36.2 ns | 0.94x |
| Zen 5 (V8 on x64) | 61.5 ns | 50.1 ns | 1.23x |

The kernel also does about 1.5x the field work of libsecp256k1's wNAF
ladder per signature (fixed windows, complete additions, blended digit-0
adds), which the IFMA backend repays with its 8 lanes and 52-bit
multiplier; a 2-lane 32-bit backend cannot. On arm64 and in WASM the right
tools are libsecp256k1's scalar code (13 us per ECDSA verification on the
M6) and, for Schnorr, the algebraic batch below.

## Using it in the node: deferred signature checks

The conan option `signature_verification` selects how block validation
verifies signatures:

| value | behaviour |
|---|---|
| `secp256k1` (default) | libsecp256k1 inline in the script interpreter, one signature at a time |
| `secp256k1_turbo` | the interpreters defer the checks whose failure is final and the block validator verifies them in lane-parallel batches |

It maps to the CMake variable `KTH_SIGNATURE_VERIFICATION` and the define
`KTH_SIGVERIFY_TURBO_BATCH`, and applies to both script engines (the native
VM in `domain` and the BCHN interpreter in `consensus`).

A script interpreter cannot batch by itself: CHECKSIG needs its answer before
the next opcode. Under NULLFAIL (every BCH block since November 2017) a
non-empty signature that fails verification fails the whole script, so for
those checks the interpreter may assume validity, record the check
(`deferred::record_*`) and move on; the block validator (`batch_validate`)
calls `deferred::begin()` on each worker before its transactions and
`deferred::end()` after, which verifies everything the worker collected with
`best_backend()`. A failing batch rejects the block exactly where a failing
CHECKSIG would have, and the validator re-runs the transactions inline to
report the culprit. The interpreters open the deferral scope only around
CHECKSIG / CHECKSIGVERIFY, CHECKDATASIG / CHECKDATASIGVERIFY and Schnorr-mode
CHECKMULTISIG, and only when NULLFAIL is active; legacy ECDSA CHECKMULTISIG
(which tries keys in order and tolerates failed checks) and pre-NULLFAIL
scripts always verify inline. SigChecks accounting is unchanged.

The VMB test vectors run every script twice when the switch is on, inline
and deferred, and require the same verdict. Mempool (per-transaction)
validation stays inline.

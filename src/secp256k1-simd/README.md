# secp256k1-simd

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
| `scalar_portable` | 1 | any; same algorithm, used as the reference in tests |
| `secp256k1_fallback` | 1 | libsecp256k1, one signature at a time |

The AVX-512 kernel lives in its own translation unit compiled with the AVX-512
flags and is only entered after a CPUID check, so the library builds and runs
on the project's baseline `-march` and picks the kernel at run time. When the
whole build already targets an IFMA microarchitecture (`-march` defines
`__AVX512IFMA__`, as a Knuth `march_id` for such a CPU does) the choice is
made at compile time and no CPUID runs. `KTH_SECP256K1_SIMD_BACKENDS`
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

One core, batch of 2048 signatures, `kth_secp256k1_simd_bench` (nanobench).

AMD Ryzen 9 9950X3D (Zen 5, 5.3 GHz under load), GCC 16:

| | ECDSA ns/sig | Schnorr ns/sig |
|---|---:|---:|
| libsecp256k1, one at a time (parse + verify) | 19,569 | 19,924 |
| secp256k1-simd, portable 1-lane | 68,810 | 73,890 |
| secp256k1-simd, AVX-512 IFMA 8-lane | 5,336 | 6,086 |
| speed-up | **3.7x** | **3.3x** |

The per-signature cost is flat from a batch of 8 upwards (one kernel run
of 8 lanes costs about 45 us); a batch of 1 is slower than libsecp256k1,
hence the fallback.

Apple M6 (arm64, no vector backend), Apple clang 21:

| | ECDSA ns/sig | Schnorr ns/sig |
|---|---:|---:|
| libsecp256k1, one at a time (parse + verify) | 13,153 | 13,803 |
| secp256k1-simd, portable 1-lane | 45,622 | 51,395 |

On arm64 `best_backend()` is the libsecp256k1 fallback: NEON has no 64x64
lane multiply, and the M6 already verifies a signature in 13 us with the
scalar int128 code, so there is nothing to gain from a 2-lane kernel there.
The portable backend exists for testing, not for speed.

## Tests and benchmark

`kth_secp256k1_simd_test` (Catch2) checks the field and scalar arithmetic
against GMP, the kernel against libsecp256k1's public API (including
degenerate inputs: zero scalars, `Q = +-G`, sums that land on infinity), the
AVX-512 lanes against the portable backend bit for bit, and the public API
against libsecp256k1 on valid, corrupted and malformed inputs. The tests
are built with `KTH_SECP256K1_SIMD_CHECK_LIMBS`, which asserts the 52-bit
operand invariant in the portable backend. `kth_secp256k1_simd_bench`
(nanobench) produces the table above.

## Algebraic batch verification (BCH Schnorr only)

`verify_schnorr_bch_all` wraps `secp256k1_schnorr_verify_batch`, a local
addition to the vendored libsecp256k1 Schnorr module: with random 128-bit
`a_i` it checks `sum a_i*(s_i*G - e_i*P_i - R_i) == infinity` with one
multi-scalar multiplication (`secp256k1_ecmult_multi_var`, Strauss or
Pippenger depending on the size), where `R_i` is `r_i` lifted to the point
with a square `y`. It gives one verdict for the whole batch, not one per
signature, and does not apply to ECDSA (the `y` of `R` cannot be recovered
from `r` alone). The randomizers are derived from a hash of every input plus
an optional caller seed.

Zen 5, one core (same conditions as above):

| batch size | libsecp256k1 one at a time | algebraic batch | AVX-512 IFMA lanes |
|---:|---:|---:|---:|
| 2 | 19,924 ns/sig | 19,501 | 23,073 |
| 8 | | 16,357 | 6,071 |
| 32 | | 16,008 | 6,066 |
| 128 | | 14,216 | 6,080 |
| 512 | | 12,603 | 6,075 |
| 2048 | | 11,390 (1.75x) | 6,076 (3.3x) |

Apple M6, one core (no vector backend):

| batch size | libsecp256k1 one at a time | algebraic batch |
|---:|---:|---:|
| 2 | 13,803 ns/sig | 14,455 |
| 8 | | 13,049 |
| 32 | | 12,541 |
| 128 | | 11,240 |
| 512 | | 9,925 |
| 2048 | | 9,282 (1.49x) |

The algebraic batch only amortises slowly (Pippenger's bucket method still
costs a few hundred field multiplications per point, plus a square root per
signature to lift `R`), so on an IFMA machine the lane kernel wins at every
size. Where there is no AVX-512 IFMA the algebraic batch is the only
speed-up available for Schnorr inputs. The two are not combined here: doing
so would mean lifting the `R_i` with the vector field and vectorising the
bucket accumulation, which is a separate piece of work.

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
| `secp256k1_simd` | the interpreters defer the checks whose failure is final and the block validator verifies them in lane-parallel batches |

It maps to the CMake variable `KTH_SIGNATURE_VERIFICATION` and the define
`KTH_SIGVERIFY_SIMD_BATCH`, and applies to both script engines (the native
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

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

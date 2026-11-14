# SHA-256 dispatch

Infrastructure's SHA-256 and its batched double SHA-256 of 64-byte blocks have
several implementations: x86 SHA-NI, ARM SHA2, SSE4.1 with optional AVX2, and
portable code. The Conan option `sha256_dispatch` says when one is chosen:

| Option | Chosen | Calls | The binary |
|---|---|---|---|
| `runtime` | when the library is loaded, by CPU detection | through function pointers | uses the best implementation the CPU it runs on has |
| `compile_time` | when it is compiled, from the instructions of the target | direct | uses the implementation of the target it was built for, on any CPU |
| `auto` (default) | see below | | |

`auto` is `compile_time` when the package is built for the CPU of the machine
(`march_strategy=optimized`) and `runtime` otherwise:

- A package built for the CPU of the machine may use every instruction that CPU
  has, so the implementation detection would choose is known when compiling.
- A package built for a reference microarchitecture (the published ones are
  x86-64-v3) runs on CPUs that have more than it assumes. x86-64-v3 has no
  SHA-NI: with `compile_time` such a package would use SSE4.1 and AVX2 on every
  CPU, including the ones with SHA-NI, where `runtime` uses it.

To choose explicitly, pass `-o 'kth/*:sha256_dispatch=compile_time'` or
`=runtime`. In a direct CMake build the variable is `KTH_SHA256_DISPATCH`, with
`runtime` as its default and no `auto`. The two modes have different Conan
package IDs.

## Compile-time dispatch

The implementation is the best one whose instructions the compiler was told the
target has: SHA-NI needs `__SHA__` and `__SSE4_1__`, ARM SHA2 needs
`__ARM_FEATURE_SHA2`, and so on. That the compiler can build an implementation
is not enough. A target with none of them, WebAssembly included, uses the
portable code.

Configuration warns when the mode is `compile_time` and the target does not
enable the SHA extensions, because that binary will not use them anywhere.

The selection is a set of constant function pointers. A call through one is a
direct call, and a test of a null one is removed, so the code that uses them is
the same in both modes. There is no CPU detection, no mutable pointer and no
initialization at startup.

`SHA256AutoDetect()` stays callable, for the code that calls it at startup: it
returns the name of the fixed implementation and ignores the restriction it is
given. `SHA256Implementation()` returns the implementation in use in both modes.

## What it is for

It removes the selection at startup and the mutable pointers. It does not make
hashing faster: the call through a pointer is predicted, and one SHA-256
compression costs far more than the call. See the measurements in the pull
request that introduced it.

## Checks

`tests/sha256-dispatch/check.py` builds the three variants (runtime, compile
time, and compile time without optimized implementations) at `-O2` and `-O0`,
compares single and batched hashes with Python's hashlib (unaligned input,
partial writes, in-place batches, batch sizes 0 to 17), and inspects the object
of the compile-time variants: no mutable dispatch pointer, no indirect call or
jump, no CPU query, no static initializer. `CXX` and `CXXFLAGS` describe the
target, and `EXPECTED_CT_IMPLEMENTATION` the implementation it must select.

`tests/sha256-dispatch/check_conan.py` checks that the two modes resolve and
have different package IDs.

CI runs both nightly, on release branches and tags, and on pull requests that
touch the SHA-256 sources or these checks.

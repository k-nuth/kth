# Imported BCHN and libauth vectors

The effective imported inventory is 1,758 BCHN script cases and 89,821
libauth/VMB transaction cases (91,579 total). No production code or expected
validation result changes. Existing import exclusions and overrides remain in
place; this change does not claim to enable upstream vectors that were excluded
before it.

## Source and import boundary

Previously, `generate_script_tests.py --format test_cases_split` adapted BCHN's
`script_tests.json` into 18 `script_vm_auto_*.cpp` files, registering one Catch2
test per case. VMB imports came from `kth-tools/generate_vmb_tests.py`, which
adapted `test/scripts/vmb_tests/bch_YEAR_CATEGORY/*.vmb_tests.json`, the
`scriptonly.json` metadata and `vmb_overrides.json`. Those 506 files already used
arrays and one Catch2 test per source file, but included the full inline
transaction validator and Catch2 dynamic-section machinery in every TU.

The packing boundary is the **adapted C++ output**, not the upstream JSON.
This preserves actual script encodings, flags, input amounts, truncated
VMB descriptions, input indices, script-only metadata, validation expectations,
and exclusions without trying to reconstruct historical import decisions.
`imported_vectors_inventory.json` records the original 524 source groups with
counts and SHA-256 hashes of the effective rows, flags, expectations and import
annotations. Reordering rows within a source, dropping or duplicating cases,
and changing expectations all change this inventory.

To repack a baseline checkout or a fresh import staged outside the build:

```sh
python3 test/scripts/pack_imported_vectors.py \
  --source "$IMPORT_STAGE" --output "$PACKED_STAGE" \
  --inventory "$PACKED_STAGE/inventory.json"
```

Use the original import tools in a staging directory first. Do not emit their
per-case C++ output into the compiled test directory. Compare the resulting
inventory against the committed inventory before replacing the packed data.
For a deliberate upstream vector update, review the inventory differences as
part of that update. Packing does not update flags or expectations itself.

## Compilation and execution

There are 13 suite registrations: one BCHN suite, and twelve VMB suites
(2022/2023/2025/2026, each invalid/nonstandard/standard). Immutable literal data
lives in thirteen `imported_*_data.cpp` translation units with small accessor
functions. The original `script_vm_auto_*.cpp` and `vmb_auto_*.cpp` filenames are retained
as data fragments included by exactly one owning TU. They are not compiled
individually. Keeping their literal rows in place makes the PR diff reviewable
without deleting and re-adding hundreds of megabytes of vector data.
No large table is defined in a header shared by multiple TUs. The VMB validator
is compiled once in `vmb_tests.cpp`; its body is unchanged. The BCHN validator
remains in `script.cpp`.

Keeping embedded tables follows the existing project's vector convention and
requires no new JSON dependency, runtime data staging, current-directory lookup,
or source-tree installation. Loading external JSON would also require faithfully
reimplementing the import adaptations or distributing another adapted dataset.
Tests run directly from a build directory and through CTest without data paths.

The runner performs the same validation for every row. It contains Catch2 fatal
assertions at the vector boundary so that failures do not abort independent
vectors. Both ordinary and unexpected-exception failures retain the originating
suite/file, index/ID, numeric flags, expected outcome, and relevant script or
transaction/UTXO inputs. The original VMB validation order (structural,
contextual, then script checks) is retained, including the current treatment of
`script_only`; packing does not reinterpret this metadata.

## Coverage and negative controls

```sh
python3 test/scripts/test_imported_vectors.py
# Same golden inventory passes against the original checkout; layout is RED:
python3 test/scripts/test_imported_vectors.py --source "$BASE/src/domain/test/chain"
```

The original layout fails specifically on 2,264 registrations versus 13. The
new layout passes both the exact-inventory and compilation-shape controls.
CMake registers `imported_vector_inventory` with CTest when Python 3.9+ is
available (matching the project's optional Python-test convention).

To verify all completed cases, including a run with deliberately bad expectations,
use a fresh trace file and a single test process:

```sh
KTH_IMPORTED_VECTOR_TRACE="$TRACE" \
  "$BUILD/src/domain/kth_domain_test" '[vm][auto],[vmb]'
python3 test/scripts/pack_imported_vectors.py --check \
  --source src/domain/test/chain \
  --inventory test/scripts/imported_vectors_inventory.json --trace "$TRACE"
```

Trace comparison uses a multiset of source/index pairs, so a duplicated execution
cannot compensate for a missing case. The trace is optional and is not enabled
for execution timing. It must not be shared between concurrent test processes.

## Measurement

See the measurements recorded with this change for compiler, configuration,
compile/link/run commands, baseline identity, and before/after results. Measure
object compilation separately from dependency construction, linking, Catch2 test
discovery and test execution. Use the same compiler, optimization, dependency
configuration and job count for both revisions. Avoid overlapping the two builds.

The retained compilation harness can target either configured revision:

```sh
python3 test/scripts/benchmark_imported_vectors.py \
  --build "$BUILD" --output "$EVIDENCE" --jobs 2 --rebuild
```

It removes only the selected object files, records the exact compiler commands,
and times a Ninja invocation targeting those objects. Do not point it at a build
used by another task. CMake regeneration should be completed before measuring.
For link timing, replay only the compiler's link command from
`ninja -t commands kth_domain_test`, excluding its trailing Catch2 discovery
command. Run the imported filter above without tracing for execution timing.

### Local result, 2026-10-30

Baseline: `a4b4192d371172d2e0d7a53ad7d5cd68766b6c2a`, tree
`f590fa5e4b0d426bbbf5045f1e36d7498106a064`. The validated result commit/tree
is recorded in the PR, avoiding a self-referential hash in this file.

Environment: Apple M6, 24 GiB RAM, macOS arm64, SDK 27.0; Apple Clang
21.0.0 (`clang-2100.3.34.2`), libc++, C++23, `-O3 -DNDEBUG -fPIE`;
CMake 4.4.4, Ninja 1.13.2.git.kitware.jobserver-pipe-1, two compilation jobs.
Both builds used the same Conan dependency toolchain, `ENABLE_TEST=ON`,
`WITH_CONSENSUS=ON`, `KTH_ASIO_STANDALONE=ON`, `CURRENCY=BCH`, and no compiler
cache, PCH, LTO or sanitizers. No dependency pins changed. Dependency preparation
(including building missing local binaries) is excluded. The existing lockfile
needed `--lockfile-partial` for its absent libiconv entry; the tracked lockfile
was not rewritten.

Both configurations used:

```sh
cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$DEPENDENCIES/build/Release/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_NODE_EXE=OFF -DBUILD_C_API=OFF
```

The object-target command was `ninja -C "$BUILD" -j2 <selected objects from
compile_commands.json>`, as reproduced by the compilation harness. The 525
baseline objects comprise 18 BCHN data/test TUs, 506 VMB data/test TUs and
`script.cpp`. The 16 result objects comprise thirteen data TUs, the shared
runner, the VMB validator and `script.cpp`.

| Phase (wall time) | Baseline | Result |
| --- | ---: | ---: |
| Affected object compilation | 436.864 s | 6.914 s |
| Warm compiler link replay, excluding discovery | 0.190 s | 0.144 s |
| Imported filter, one process, trace disabled | 84.473 s | 4.462 s |

Compilation decreased **98.42% (63.18x)**. The 47 ms link difference is too small to interpret from a single sample. Execution
improved as well because the loop no longer repeatedly traverses dynamic
sections. The baseline passed 872,831 assertions in 2,264 tests; the result
passed 872,844 assertions in 13 tests. The extra thirteen assertions check the
executed count of each suite.

The baseline and the final filename-preserving layout were each measured once.
These use normal filesystem caches on a shared machine, not a cold-cache or
statistical benchmark. The before/after builds and
execution measurements did not overlap. CTest scheduling is now coarser (13
suite tests); parallel CI execution time was not measured. No sanitizer result
is claimed. The affected-object logs have no compiler diagnostics. Both full
supporting builds reproduce three reserved-macro warnings in the BCH Schnorr
module, one ignored-`nodiscard` warning in `domain/src/message/header.cpp`,
and a duplicate-library warning from the linker. These remain visible and
were not suppressed or changed in this test-only work.

The negative control changed BCHN vector `#0` from success to `invalid_script`
and forced success for just VMB ID `h44xm` (2022 invalid, source
`before_chip_cashtokens.vmb_tests.json`, row 0). The binary reported exactly two
failures with source, index/ID, flags and inputs. It still completed all 91,579
vectors; the trace multiset matched with no missing or extra executions. This
exercises fatal-assertion containment in BCHN as well as a nonfatal VMB failure.
The original bytes were restored and their hashes checked before rebuilding;
the restored run and its complete execution trace passed. A separate mutation
of a BCHN expectation was also rejected by the inventory control.

## Reviewing the change

The original generated imports and the packed data tables are marked
`linguist-generated` in `.gitattributes`. GitHub collapses those diffs by default;
the runner, validators, import tools and golden inventory remain visible.
This changes presentation only: the files remain text, can be opened directly
or expanded individually, and the full local textual diff remains available.
The inventory and execution controls above verify their complete contents.

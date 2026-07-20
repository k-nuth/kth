# ParlayHash (vendored)

A header-only concurrent hash map from CMU (<https://github.com/cmuparlay/parlayhash>).

Vendored here — not pulled via Conan — because there is no reliable Conan
package for it. It is one of the two candidate backends for the Knuth mempool's
concurrent map (the other is `boost::concurrent_flat_map`); the mempool selects
between them at compile time via the `mempool_backend` Conan option
(`cfm` | `parlay`).

## Pinned upstream version

- Repository: <https://github.com/cmuparlay/parlayhash>
- Commit: `081408ba6111ce78b5add9129f8d902fa2fea58f`
- Date: 2025-08-19 ("added unit tests, fixed some interface bugs")
- License: MIT (© 2023 cmuparlay) — see `LICENSE`, compatible with Knuth's MIT.

## What is included

Only the subtree needed by `parlay_hash/unordered_map.h`:

- `include/parlay_hash/` — the map itself (`unordered_map.h`, `parlay_hash.h`,
  `bigatomic.h`, `parallel.h`, `unordered_set.h`).
- `include/parlay/` — the parallel-primitives headers it transitively includes
  (`bigatomic.h` pulls `parlay/primitives.h` unconditionally).
- `include/utils/` — epoch-based reclamation and locks.

Excluded from upstream: `include/old_parlay_hash/` (superseded) and a handful of
editor scratch/autosave files (`#*#`, `foo.h`, `s.h`).

## Usage notes

- Use without defining `USE_PARLAY`: `parlay_hash/parallel.h` then provides a
  serial fallback and the Parlay task scheduler is not linked. Plain
  `std::thread` needs no thread registration.
- Build with `-DNDEBUG` in production; otherwise `utils/epoch.h` enables
  guard-byte checks.
- The headers are included as SYSTEM headers so their deprecation warnings do
  not pollute the Knuth build.

## Updating

Re-copy the three subtrees from the pinned commit, delete the excluded files,
and update the commit hash above.

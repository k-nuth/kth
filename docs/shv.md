# Simplified Header Verification

[CHIP-2026-02](https://gitlab.com/0353F40E/mmr-chip) proposes a compact
commitment to a sequence of Bitcoin Cash block hashes. A light client keeps a
Merkle Mountain Range (MMR) accumulator and verifies historical headers with
inclusion proofs. The accumulator does not replace header linkage, proof of
work, or the decision to trust an initial checkpoint. SHV requires no consensus
change.

## MMR primitives

The implementation lives in `src/domain`, under `math`, alongside pure domain
primitives. It depends on hashes and containers, with no chain, database or
network dependency. The blockchain layer can later provide the block hashes
and maintain the index through block connection and disconnection.

A leaf is a block hash. A parent is double-SHA256 of the left hash followed by
the right hash, with no prefixes or height encoding. Peaks are ordered from
largest to smallest mountain. Bagging the peaks duplicates the smaller subtree
until its height matches the next larger peak, then merges them. The result is
the same as Bitcoin's Merkle root over the same hashes, including Electrum's
`cp_height` commitment. The empty root is the all-zero hash.

The server MMR stores only internal nodes, in creation order: exactly
`n - popcount(n)` hashes for `n` leaves. Block hashes remain external. Callers
provide the preceding leaf when appending to an odd-sized MMR, the final leaf
when querying an odd-sized commitment, and the leaf-level sibling when making
a proof. These hashes must come from the same leaf sequence as the stored
nodes; the MMR cannot authenticate externally supplied leaves on its own.
It can query historical prefixes, generate proofs to a peak or to the root,
and remove leaves from the end by truncating the internal nodes. Removing
leaves permits later chain reorganization handling without rebuilding the
whole index.

The client accumulator keeps only the leaf count and `popcount(n)` peaks. It
can extend its state, verify inclusion proofs and bootstrap from a proof to
the root for the final leaf. Bootstrap checks the supplied trusted root before
returning a state. A client that retains only peaks cannot recover removed
peaks by itself; rewinding it requires a previous trusted state or a proof
authenticated against a trusted commitment.

All sizes are leaf counts, and all leaf indices start at zero. A commitment
at block height `h` contains `h + 1` leaves, including genesis. In particular,
Electrum's `cp_height` becomes `cp_height + 1` when passed as a commitment size.

## Hash order and proof validation

The API takes `kth::hash_digest` values in internal byte order, as produced by
hashing a raw header. Displayed block hashes and Electrum JSON branches have
their bytes reversed. Convert display hex with `kth::decode_hash`; use
`kth::encode_hash` to display an internal hash. Raw header bytes are not reversed.

Bitcoin's duplication rule allows different leaf counts to produce the same
root (CVE-2012-2459). Root proof verification and bootstrap reject a sibling
equal to the current hash when that sibling is on the left, throughout the
proof path. Right-side duplicates needed for Bitcoin's padding remain valid.
Proofs must also have the exact length for their declared size and an in-range
leaf index. A root authenticates membership relative to its trust assumptions;
it does not independently validate the header chain.

Proofs to a peak are checked against trusted accumulator peaks and their known
mountain sizes. They allow duplicate leaves within that trusted structure.
Their sibling count must match the containing mountain's height.

The tests include the official vectors from `mmr-accumulator/test_vectors/`
and BCHN's `src/test/data/mmr_test_vectors.json` (the same vector set), covering
Bitcoin Merkle roots, early block hashes, Electrum proofs and the duplicate
subtree attack.

## Following pieces

This piece supplies the MMR primitives and tests. It does not yet maintain an
MMR for the active chain or expose SHV through the node. Later work will add
the chain index with block connect/disconnect handling, P2P `getshv`/`shv`
messages with service bit `NODE_SHV` (`1 << 9`), and RPC `getshv` and `shvroot`.

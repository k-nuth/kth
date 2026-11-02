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
network dependency. The blockchain layer supplies the connected block hashes
and maintains the index through block connection and disconnection.

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

## Enabling the chain index

Start Knuth with `--node.mmr_index=true`, or set `node.mmr_index = true` in
the configuration file. The default is false. Startup reconstructs the MMR
from the header index in height order, from genesis through the connected
UTXO tip. Downloading headers or storing block bodies does not advance it.
Connected blocks extend the index; disconnecting blocks truncates it before
the replacement chain is published. Roots and proofs are unavailable during
a chain transition.

With the index enabled, Knuth advertises `NODE_SHV` (`1 << 9`) in the version
handshake. With it disabled, Knuth clears that service bit and ignores
`getshv` messages.

## P2P requests and replies

The `getshv` and `shv` payloads use BCHN's SHV MR wire format. `CompactSize`
means canonical Bitcoin variable-length integers. Heights start at zero;
a commitment at height `h` includes `h + 1` block hashes.

`getshv` begins with a CompactSize request count, followed by these fields
for each request:

| Field | Encoding |
|---|---|
| `type` | One byte: bit 0 selects root (1) or peak (0); bit 1 selects hash (1) or height (0) |
| `commitment_height` | CompactSize |
| `block_height_or_hash` | 32 bytes: internal-order block hash, or little-endian height with bytes 8 through 31 zero |

Bits 2 through 7 are reserved and must be zero. A batch may contain at most
100 requests. A larger batch receives a 20-point misbehavior penalty and no
reply. Invalid individual entries are omitted: reserved bits, a malformed
height, an unknown or disconnected block hash, a height beyond the commitment,
or a commitment beyond the connected tip. A valid batch still receives `shv`
when every entry is omitted. If the enabled index is temporarily unavailable,
the response may be empty.

`shv` begins with a CompactSize response count. Successful entries preserve
request order and contain:

| Field | Encoding |
|---|---|
| `type` | The request's type byte |
| `commitment_height` | CompactSize |
| `block_height` | CompactSize, resolved even for hash selections |
| `header` | Raw 80-byte block header, without the `headers` message's transaction-count byte |
| `proof` | CompactSize sibling count, followed by 32-byte internal-order hashes, from leaf upward |
| `target` | 32-byte internal-order root or containing peak |

For example, a proof-to-peak request for block 5 committed at height 10 is
`01 00 0a 05` followed by 31 zero bytes. Change the type byte to `01` for a
proof to the root. A client hashes the returned header and verifies the
branch with `mmr_accumulator::verify_proof_to_peak` or `verify_proof_to_root`
against its trusted accumulator. It must match the returned heights and type
to its request. A returned target alone does not establish trust.

## RPC

With RPC compiled and enabled, `getshv ["root", commitment_height, block_height]`
returns `branch`, raw `header` hex and `root`. Using `"peak"` returns `peak`
instead of `root`. JSON hash strings use display order; wire hashes use
internal order. Verbose `getblockheader` includes `shvroot` for indexed
connected headers, and `getblockchaininfo` includes it at the connected tip.
See [JSON-RPC](json-rpc.md#header-inclusion-proofs-getshv) for arguments and errors.

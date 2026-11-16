# Compact block filters for light wallets

A light wallet can find its transactions without telling a node what it owns.
It downloads a small filter for every block, tests each filter itself, and
fetches only the blocks that match. kth serves those filters, and the filter
headers a wallet checks them against.

```ini
node.compact_filters = true
```

It is off by default. When on, the node sets the compact filters service bit
(bit 8) in its version message and answers three requests, for the regular
filter (filter type 0), the script filter (filter type 1) and the input prefix
filter (filter type 2), and for blocks of its connected chain:

| Request | Answer |
|---|---|
| `getcfilters`: a filter type, a start height, a stop block | One `cfilter` for each block from the start height up to the stop block, at most 1,000 |
| `getcfheaders`: a filter type, a start height, a stop block | One `cfheaders`: the filter header of the block before the start height, and the filter hash of each block, at most 2,000 |
| `getcfcheckpt`: a filter type, a stop block | One `cfcheckpt`: the filter header of every thousandth block up to the stop block |

A request the node cannot answer is ignored, as other Bitcoin Cash nodes do,
and the wallet asks another peer: another filter type, a stop block that is not
on the connected chain, a start height above it, a range past the limit, or
filter headers the node has not computed that far yet.

## The regular filter

The filter of a block holds the outpoint of every input after the coinbase, as
it stands in the transaction, and every output script that is not empty. It is
a Golomb-coded set with P = 19 and M = 784931, keyed by the first 16 bytes of
the block hash. Everything in it comes from the block itself.

The filter hash is the double SHA-256 of the filter as it is served. The filter
header of a block is the double SHA-256 of its filter hash followed by the
filter header of the block before; before the first block it is all zeros.

## The script filter

The script filter of a block (filter type 1) holds what a wallet watches for:
the locking script of every output the block creates and of every output it
spends, the token category of each of them that carries tokens, and the hash
of every transaction of the block, the coinbase included. A wallet matches its
own scripts against it, received and spent alike, a token wallet matches a
category, and a wallet that has only a transaction's hash finds the block that
confirmed it. A script that is empty or starts with `OP_RETURN` is left out.

Each of those is an entry: a 32-byte key (the SHA-256 of the script, of `0x01`
and the category, or of `0x02` and the transaction hash in the order it is
stored, as in an outpoint), the index of the transaction, the index of the
input or output (0 for a transaction), and which of the five it is. A category
is the hash of the transaction that created it, so the two tags keep the key
of a category apart from the key of that transaction. The entries are sorted,
and the filter is served with the root of a Merkle tree over them in front of
the Golomb-coded set of their keys. The filter hash covers both, so the filter
headers commit to the entries of every block.

That root lets a wallet settle a disagreement between two nodes without
downloading the block:

| Request | Answer |
|---|---|
| `getcfrange`: a filter type, a block, a key prefix of 0 to 256 bits | One `cfrange`: the entries whose keys start with the prefix, the entry before them and the one after, and the hashes that rebuild the root from that run. It shows the block has no other entry with the prefix. A range too large for one message is not answered; a longer prefix splits it |
| `getcfproof`: a filter type, a block, the index of an entry | One `cfproof`: the entry with its branch to the root, the transaction it names with its Merkle branch in the block and, for a spent output, the transaction that created the output with its own block and Merkle branch. A wallet checks all of it against block headers |

The outputs a block spends are not in the block. The node keeps them in the
undo record it writes when it validates a block, so the script filter is served
for the blocks that have that record. A node synced the default way validates
from its last checkpoint on, and has no record at or below it: its script
filter headers stop at the first block that spends an output without one, and
requests above that are not answered. A node synced with
`chain.full_validation = true` has the records, and the script filter, from the
first block.

## The input prefix filter

The input prefix filter of a block (filter type 2) is for wallets of reusable
payment addresses. A payment to such an address is found by one of its inputs:
the sender grinds one of the first 30 inputs of the transaction until the
double SHA-256 of the input starts with 4 to 16 bits of the receiver's scan
key. The filter holds, for every transaction after the coinbase and each of its
first 30 inputs, the first two bytes of the double SHA-256 of the input as it
stands in the transaction (the outpoint, the script and the sequence), read as
a big-endian number: the sorted distinct values, 0 to 65,536 of them.

They go on the wire as a count, a CompactSize, and when there are any, a Rice
parameter k in one byte and the gap of each value from the one before (from 0
for the first), Rice coded: the gap shifted right by k in one-bits, a zero-bit,
then the low k bits of the gap, most significant bit first, the last byte
padded with zero-bits. k is the largest number with 2^k at most 45426 / N
(integer division), and 0 when that is 0, so a set has one encoding and every
node serves the same bytes. The filter hash and the filter headers are as for
the other types.

A wallet whose scan key starts with the b bits of p tests whether the filter
has a value in `[p << (16 - b), (p + 1) << (16 - b))`, one test on the sorted
values, and downloads the blocks that have one. It builds the same values from each
block it downloads, so a node that serves another set is caught. No request
names the wallet's prefix: the node sees only which blocks it downloads.

The filter is built from the block alone, so it is served for every block the
node holds, however the node synced. Nothing in it is a key a wallet looks up,
so it has no entry tree, and a `getcfrange` or a `getcfproof` for it is not
answered.

## What the node keeps

Nothing for each block.

- A filter is built from its block when a peer asks, a script filter from the
  block and its undo record, and an input prefix filter from the block alone.
  The most recent ones stay in memory, up to 32 MiB for each filter type.
- A filter hash is kept in memory the same way, up to 262,144 of them for each
  filter type.
- The entries of a script filter are built again for a `getcfrange` or a
  `getcfproof`; those of the last two blocks asked about stay in memory.
- Filter headers chain back to the first block, so one is kept for every
  thousand blocks: the ones a `cfcheckpt` carries. They are in memory and in
  a file in the database directory, `compact_filter_checkpoints` for the
  regular filter, `script_filter_checkpoints` for the script filter and
  `input_prefix_filter_checkpoints` for the input prefix filter, 64 bytes a
  record: the block hash and its filter header. Any other filter header is
  chained from the record below it.

Those files are the only thing stored. For a chain of a million blocks each is
64 kB. The first time, the node computes them with one pass over its blocks, in the
background; after that it adds a record for every thousand blocks connected.
Records above a reorganization are dropped and computed again. A request that
needs a record the pass has not reached is ignored until it has. Deleting the
file only makes the node compute it again.

## Cost

Building a filter means reading its block. A `getcfilters` reads up to 1,000
blocks, and a `getcfheaders` up to 2,000, plus up to 999 to chain from the
record below the range, unless their filters or hashes are still in memory. The
requests run in the peer's own service, so a slow or greedy peer holds only
itself.

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
filter (filter type 0) and for blocks of its connected chain:

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

## What the node keeps

Nothing for each block.

- A filter is built from its block when a peer asks. The most recent ones stay
  in memory, up to 32 MiB.
- A filter hash is kept in memory the same way, up to 262,144 of them.
- Filter headers chain back to the first block, so one is kept for every
  thousand blocks: the ones a `cfcheckpt` carries. They are in memory and in
  the file `compact_filter_checkpoints` in the database directory, 64 bytes a
  record: the block hash and its filter header. Any other filter header is
  chained from the record below it.

That file is the only thing stored. For a chain of a million blocks it is 64
kB. The first time, the node computes it with one pass over its blocks, in the
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

# Serving compact block filters

A node built with the option `neutrino` (see [optional-features.md](optional-features.md))
can serve compact block filters to light wallets (Neutrino, BIP157/158). The
wallet downloads a small filter for each block, tests it itself, and fetches
only the blocks that match, so the node never learns what the wallet watches.

## The filter types served

| Filter type | What it holds | Setting |
|---|---|---|
| 0, the regular filter | the outpoint of every input after the coinbase, and every output script that is not empty | `node.compact_filters` |
| 2, the input prefix filter | the first two bytes of the double SHA-256 of each of the first 30 inputs of every transaction after the coinbase, for wallets of reusable payment addresses | `node.input_prefix_filters`, with `node.compact_filters` |

Both are built from the block alone. Filter type 1, the script filter, is not
served.

Both are off by default:

```
[node]
compact_filters = true
input_prefix_filters = true
```

With `compact_filters` on, the node sets the compact filters service bit and
answers three requests for the regular filter; with `input_prefix_filters` on
too, it answers them for the input prefix filter as well:

| Request | Answer | Limit |
|---|---|---|
| `getcfilters` | one `cfilter` for each block, lowest first | 1,000 blocks |
| `getcfheaders` | one `cfheaders`: the filter header before the range and the filter hash of each block | 2,000 blocks |
| `getcfcheckpt` | one `cfcheckpt`: the filter header of every thousandth block up to the stop block | |

A request the node cannot answer is ignored, as other Bitcoin Cash nodes do,
and the wallet asks another peer: a filter type it does not serve, a stop block
that is not on the connected chain, a start height above it, a range past the
limit, or filter headers not computed that far yet.

## The input prefix filter

A wallet of reusable payment addresses ("paycode:") is paid by a transaction one
of whose first 30 inputs the sender ground until the double SHA-256 of the input
starts with 4 to 16 bits of the wallet's scan key, its prefix. The input prefix
filter of a block holds the first 16 bits of that hash for each such input, so
the wallet matches its prefix against the filters, downloads only the blocks
that may pay it, and names its prefix to no one.

The filter is the sorted distinct values as a CompactSize count and, when there
are values, one byte the count gives: the Rice parameter for up to 19,196
values, then the Rice-coded gaps between them; or, from 19,197 values, 16 and a
bitmap of the 65,536 values (8,192 bytes). Each set of values has one encoding,
so every node serves the same bytes. Its filter hash and filter headers chain as
for the regular filter. It has no service bit of its own: it is served beside
the regular filter, by a node with the compact filters bit, and a wallet asks
such a node for it.

## What the node keeps

A filter header chains back to the first block, so the node keeps the filter
hash and the filter header of every block of the connected chain, for each
filter type it serves: 64 bytes a block and a type, about 60 MB a type for a
chain of a million blocks. They are held in memory and in one file a type, in
the database directory: `compact_filter_headers` for the regular filter and
`input_prefix_filter_headers` for the input prefix filter. `getcfheaders` and
`getcfcheckpt` are answered from them and read no block.

The files are computed by one task that reads each block once for every type
it lacks: the whole chain the first time a setting is on, then each block as it
is connected. A type turned on later is computed from the first block without
holding back the others. Until a type's records reach a block, requests about
that block for that type are not answered. A file the node cannot trust is
computed again.

Filters themselves are not kept. Each `cfilter` is built from its block when a
peer asks for it.

## Not on a pruning node

Both the filters and the filter headers come from the blocks, all of them, so
`node.compact_filters` with `db.prune_keep_blocks` is refused when the node
starts, and so is `node.input_prefix_filters`, which needs `node.compact_filters`.

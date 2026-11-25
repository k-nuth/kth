# Serving compact block filters

A node built with the option `neutrino` (see [optional-features.md](optional-features.md))
can serve compact block filters to light wallets (Neutrino, BIP157/158). The
wallet downloads a small filter for each block, tests it itself, and fetches
only the blocks that match, so the node never learns what the wallet watches.

It is off by default:

```
[node]
compact_filters = true
```

With it on, the node sets the compact filters service bit and answers three
requests for the regular filter (filter type 0):

| Request | Answer | Limit |
|---|---|---|
| `getcfilters` | one `cfilter` for each block, lowest first | 1,000 blocks |
| `getcfheaders` | one `cfheaders`: the filter header before the range and the filter hash of each block | 2,000 blocks |
| `getcfcheckpt` | one `cfcheckpt`: the filter header of every thousandth block up to the stop block | |

A request the node cannot answer is ignored, as other Bitcoin Cash nodes do,
and the wallet asks another peer: another filter type, a stop block that is not
on the connected chain, a start height above it, a range past the limit, or
filter headers not computed that far yet.

## What the node keeps

A filter header chains back to the first block, so the node keeps the filter
hash and the filter header of every block of the connected chain: 64 bytes a
block, about 60 MB for a chain of a million blocks. They are held in memory and
in one file, `compact_filter_headers`, in the database directory. `getcfheaders`
and `getcfcheckpt` are answered from them and read no block.

The file is computed by reading each block once: the whole chain the first time
the setting is on, then each block as it is connected. Until it reaches a block,
requests about that block are not answered. A file the node cannot trust is
computed again.

Filters themselves are not kept. Each `cfilter` is built from its block when a
peer asks for it.

## Not on a pruning node

Both the filters and the filter headers come from the blocks, all of them, so
`node.compact_filters` with `db.prune_keep_blocks` is refused when the node
starts.

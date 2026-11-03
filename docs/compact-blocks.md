# Compact block relay

Knuth supports BIP152 version 1 on Bitcoin Cash. Short transaction IDs use txids;
transactions have no witness encoding. After the handshake, Knuth advertises
version 1 and asks at most three connected peers for high-bandwidth announcements.
Other peers can still request compact blocks.

For a recent connected tip, a received `cmpctblock` for its next block is rebuilt
from prefilled transactions and the mempool. The next block's header may already
be known. Missing transactions are requested with `getblocktxn`, with a ten-second
response deadline. At most three reconstructions run at once, with one request
per peer. A rebuilt block enters `block_chain::organize()`, the submission path
used for a locally found block, and passes the same validation and connection.

Ambiguous short IDs, an incorrect response, a merkle mismatch, an unanswered
request, or a refused submission return the header to the normal full-block
download. These reconstruction failures do not ban the peer. A body range that
has already opened keeps using the normal download; compact reconstruction does
not replace a writer already serving that range. Initial sync also keeps the
normal header and full-block download path.

Knuth answers `getdata(MSG_CMPCT_BLOCK)` with a `cmpctblock` for a connected block
on the active chain when version 1 was negotiated, prefilling the coinbase.
Otherwise it returns the full block, or `notfound` when the block is unavailable.
`getblocktxn` returns exactly the requested transactions in index order. Indexes
are absolute in memory and differential on the wire. Invalid, out-of-range, or
unavailable requests are ignored. Counts are bounded before allocation, and
transaction requests cannot name more positions than a block can contain.

A peer's `sendcmpct(true, 1)` selects compact announcements when one new block
extends a block that peer knows. `sendcmpct(false, 1)` disables those announcements;
unsupported versions leave the preference unchanged. Other announcements keep
using `headers` or `inv`. The source and peers that already know the block are
skipped. Announcement sends never wait for queue space: a slow peer can miss an
announcement without delaying block connection or other peers.

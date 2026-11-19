# Wallet support in the node

A wallet asks Knuth about its own scripts, token categories, transactions and
reusable payment address (RPA) prefixes, and Knuth answers. It needs no index
and no separate server:

- **On a full node** the answers come from the node's own chain: its stored
  blocks, its chain state and its mempool. The blocks from the wallet's
  birthday are read once, and every block that connects afterwards is read as
  it arrives. Nothing is asked of any peer and nothing new is stored.
- **On a node that holds only headers and filters** (a phone, a browser, a
  light build) the answers come from peers. Script filters (filter type 1) and
  input prefix filters (filter type 2) are synced over P2P, checked as chains
  against the checkpoints every serving peer gives. Whole blocks are downloaded
  where the filters match, spread over peers. Unconfirmed transactions come
  from ordinary relay. No request names what the wallet watches: no
  `getcfrange` or `getcfproof` with a watched key, no `filterload`, no
  `getdata` of a transaction picked for the wallet. Which blocks are
  downloaded names nothing either: each block comes over a connection of its
  own, with three other blocks of its 144-block bucket that are the same in
  every session, and an RPA lookup downloads every block of its range.

The requests and the answers are the same either way: `kth::node::wallet::service`
in C++, `kth/capi/node/wallet.h` in C.

## CashTokens and BCMR

With no index either: a token category's genesis, unspent outputs, supply
(total, reserved, circulating), holders and NFTs are rebuilt from the blocks
its category key matches, and a BCMR identity's authchain is followed one
spend of its identity output at a time, to the authhead and its publication
output. The node never fetches a registry: an app downloads it from the
published URIs and checks the bytes against the published hash
(`registry_matches`).

## The local Electrum listener

For wallets that do not speak P2P, a full node can answer the Electrum
protocol (Fulcrum's method names, arguments and result shapes) from its own
chain:

```ini
node.wallet_electrum = 127.0.0.1:50001
```

It is off by default. **Only a loopback address is accepted** (127.0.0.0/8, or
`[::1]` for IPv6); any other address is refused at startup. The listener is
for the node owner's own wallets on the same machine. A node answering
Electrum on any other address would be a server that learns what other
people's wallets watch, and Knuth does not serve that. Wallets on other
machines use P2P, where nothing they send names what they watch.

`kth-wallet` runs the peer path with the same listener, also on a loopback
address by default, for a wallet that has no full node of its own.

Besides Fulcrum's methods the listener answers three of Knuth's own, under
names no Fulcrum method uses: `kth.token.category` (a category id),
`kth.token.holdings` (a script hash or an address: what it holds per
category) and `kth.identity.resolve` (an authbase, then optionally a
registry's authchain extension as raw transactions).

# Wallet support in the node

A node built with the options `neutrino` and `wallet` (see
[optional-features.md](optional-features.md)) answers the wallet of its own
user about its scripts, token categories, transactions and reusable payment
address (RPA) prefixes. It needs no index and no separate server.

The answers come from the node's own chain: its stored blocks, its chain state
and its mempool. The blocks from the wallet's birthday are read once, in
parallel batches, and every block that connects afterwards is read as it
arrives. A reorganization rolls each watched target back to the fork and the
new branch is read. Nothing is asked of any peer and nothing new is stored: the
only memory is what the watched targets own and a bounded cache of blocks.

- **Histories, balances and unspent outputs** of a script, a script known by
  its SHA-256, or a token category, with the mempool's transactions apart.
- **A transaction by its hash alone**, with its Merkle branch, even one whose
  outputs are all spent: from the mempool, the watched histories, the block at
  a height hint, and then the stored blocks read from the tip down.
- **RPA**: the transactions with one of their first 30 inputs in a prefix,
  confirmed in a range of heights or in the mempool.
- **Broadcast** to the node's own mempool, which relays it.

The calls are `kth::node::wallet::service` in C++, made with
`service::from_chain`, and `kth/capi/node/wallet.h` in C, where requests and
answers are the Electrum protocol's JSON-RPC (below), answered in the process
with no listener, so every binding reads the same shapes.

## The local Electrum listener

For wallets that do not speak P2P, the node can answer the Electrum protocol
(Fulcrum's method names, arguments and result shapes) from its own chain:

```ini
node.wallet_electrum = 127.0.0.1:50001
```

It is off by default. **Only a loopback address is accepted** (127.0.0.0/8, or
`[::1]` for IPv6); any other address is refused at startup. The listener is
for the node owner's own wallets on the same machine. A node answering
Electrum on any other address would be a server that learns what other
people's wallets watch, and Knuth does not serve that.

`node.wallet_birthday` is the first height read for a wallet that gives none
(0, the default, reads from genesis). The setting is refused by a build
without the option `wallet`, and on a pruning node (`db.prune_keep_blocks`):
the wallet reads the blocks back to its birthday.

A subscription is recorded as the very answer its client got, so a change
after that answer is never lost between two readings, and only the
subscriptions a change touched are read again.

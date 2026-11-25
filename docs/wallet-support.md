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

The calls are `kth::node::wallet::service`, made with `service::from_chain`.

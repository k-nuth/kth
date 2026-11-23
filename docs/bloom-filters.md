# Bloom filters for light wallets (BIP37)

A light wallet can load a bloom filter into a node. The node then sends it only
the transactions that match the filter, and for a block a proof that those
transactions are in it.

It is an optional feature of the build ([optional-features.md](optional-features.md)):
the Conan option `bloom_filters`, off by default.

```sh
conan install . -o bloom_filters=True ...
```

In a build with it, it is on unless turned off, as on other Bitcoin Cash nodes,
and the node sets the bloom filter service bit (bit 2) in its version message.
To turn it off:

```ini
node.bloom_filters = false
```

A build without it never sets the service bit, behaves as described under
[With the setting off](#with-the-setting-off), and refuses to start with
`node.bloom_filters = true`.

A bloom filter tells the node roughly what the wallet watches: the wallet
chooses how many false positives to hide among. A wallet that wants the node to
learn nothing uses compact block filters instead.

## The peer's filter

The filter belongs to one peer and lives in that peer's session for as long as
the connection. The node keeps nothing for it. Every peer starts with a filter
of no bytes, which holds everything.

| Message | What it does |
|---|---|
| `filterload` | Sets the peer's filter: a bit field of at most 36,000 bytes, at most 50 hash functions, a tweak and the update flags. The peer is announced transactions from then on, whatever its version message asked for |
| `filteradd` | Adds an element of at most 520 bytes to the filter |
| `filterclear` | Puts back the filter that holds everything, and keeps announcements on |

A filter or an element past those limits scores the peer.

## What the filter does

- **Blocks.** A `getdata` for a filtered block is answered with a
  `merkleblock`: the block header, the number of transactions, and the part of
  the merkle tree that proves the matched ones are in the block. The matched
  transactions follow, each in a `tx` message. A peer that loaded no filter
  gets every transaction of the block.
- **Announcements.** The peer is announced only transactions its filter
  matches, and the answer to `mempool` lists only those.

A transaction matches when the filter holds its hash, a data push of one of its
output scripts, an outpoint one of its inputs spends, or a data push of one of
its input scripts. When an output matches, its outpoint is added to the filter
as the update flags say, so the transaction that spends it matches too: always,
never, or only for outputs that pay to a public key or a bare multisig. For a
block the outputs of every transaction are matched before the inputs, because a
transaction can stand before the one it spends.

## With the setting off

The service bit is not set. A peer that sends `filterload` or `filteradd`
anyway is disconnected, and scored when its protocol version is 70011 or
higher, new enough to read the service bit (BIP111). A `filterclear` only turns
the peer's announcements on. A `getdata` for a filtered block is not answered.

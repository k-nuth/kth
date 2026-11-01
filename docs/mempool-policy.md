# Mempool fee policy

How Knuth decides which transactions its mempool takes, how it stays bounded,
and what it tells peers. All of it is Knuth's own policy, set by the options
below.

## The floor

A transaction must pay at least `node.byte_fee_satoshis` per byte of its
serialized size (default 0.1). On BCH nothing is charged per signature
operation by default (`node.sigop_fee_satoshis = 0`): signature work is
already bounded at admission by the SigChecks rules (per-input density, 3000
per transaction). An operator can still set a per-sigop price.

## A bounded mempool

`node.mempool_max_bytes` (default 320000000, ten 32 MB blocks) caps the total
serialized size of pooled transactions. It counts serialized bytes, not
memory. When an admission takes the pool over the cap, the transaction with
the lowest fee per byte is evicted, with its descendants, until the pool fits.
At equal fee rates the one seen last goes first, so a newcomer cannot push out
a transaction that pays as much and was there first. If the newcomer is the
cheapest, it is the one evicted, and it is refused as paying too little.
`0` turns the cap off.

## A floor that rises while the pool is full

After an eviction the floor becomes the evicted fee rate plus
`node.byte_fee_satoshis`, so what was evicted cannot come straight back at
the price that just lost. The excess over the configured floor halves every
12 hours and is dropped once it is under half of the configured floor.

## Expiry

A transaction still unconfirmed `node.mempool_expiry_hours` after it was first
seen (default 336, 14 days) is removed with its descendants when a block
connects, so its inputs are free to be spent again. The first-seen time is
saved in `mempool.dat`, so a restart does not reset it, and a saved
transaction already past it is not taken again. `0` keeps transactions until
they confirm or conflict.

## What peers are told

Right after the handshake each peer gets a `feefilter` (BIP133) with the
current floor in satoshis per 1000 bytes: 100 by default. Peers then send the
transactions the mempool would take and nothing it would refuse. When the
floor moves, every peer is told again, at most once every ten minutes.

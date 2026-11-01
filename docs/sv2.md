# Stratum V2 Template Provider

Knuth can serve block templates to Stratum V2 clients (a Job Declarator or a
pool) over the Template Distribution Protocol. The provider is part of node
core and does not need a build with JSON-RPC.

## Running

It is off by default. Enable it in the config file:

```ini
sv2.enabled = true
sv2.bind    = 127.0.0.1   # localhost only by default
sv2.port    = 8442
```

The same settings are `--sv2.enabled`, `--sv2.bind` and `--sv2.port` on the
command line.

The listener speaks plaintext SV2 frames until the Noise transport is added, so
it has no encryption and no authentication. Keep it on localhost or another
trusted interface.

## What it does so far

- Answers `SetupConnection` for the Template Distribution Protocol, version 2,
  with `SetupConnection.Success`. Any other protocol, a version range without 2,
  or a feature flag gets `SetupConnection.Error` and the connection is closed.
  No Template Distribution flags are defined yet.
- Keeps each connection's `CoinbaseOutputConstraints`, which the client must
  send right after setup.
- Answers `RequestTransactionData` with `RequestTransactionData.Error`
  (`template-id-not-found`), since no template has been sent yet, and logs and
  ignores `SubmitSolution`.
- Discards messages of extensions it does not implement, as the specification
  requires.
- Closes the connection on a malformed frame, a message only a server sends, a
  message type the protocol does not define, or a payload over 1 MiB.

Sending templates, transaction data and block submission come next.

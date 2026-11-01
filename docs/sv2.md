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

## Template flow

- Answers `SetupConnection` for the Template Distribution Protocol, version 2,
  with `SetupConnection.Success`. Any other protocol, a version range without 2,
  or a feature flag gets `SetupConnection.Error` and the connection is closed.
  No Template Distribution flags are defined yet.
- The client sends `CoinbaseOutputConstraints` right after setup. Each constraints
  message gets a `NewTemplate` for the current tip, followed by `SetNewPrevHash`
  naming that template. Template IDs strictly increase within each connection.
- When the node connects a new tip, ready clients receive a `NewTemplate` with
  `future_template = true`, then `SetNewPrevHash`, without making another request.
  Templates use the same transaction selection as Knuth's mining RPCs.
- Selection reserves room for the client's additional coinbase outputs, a
  100-byte coinbase scriptSig, the fixed coinbase fields, and block overhead.
  The wire field `coinbase_output_max_additional_sigops` reserves part of BCH's
  block SigChecks budget. BCH uses SigChecks rather than Bitcoin's sigops limit.
- The coinbase prefix starts with the BIP34 height push and is at most 8 bytes.
  The client can spend the subsidy plus selected transaction fees. Knuth adds no
  mandatory coinbase outputs; BCH has no SegWit witness commitment.
- `RequestTransactionData.Success` contains the template's transactions in block
  order, excluding the coinbase, with empty `excess_data`. Each connection keeps
  a bounded set of recent templates. A new tip discards the previous tip's
  templates. Requests for those IDs get `stale-template-id`; unknown IDs get
  `template-id-not-found`.
- `SubmitSolution` reconstructs the full block from the supplied header fields
  and coinbase plus the template's transactions, then submits it through the
  node's block validation and connection pipeline. The outcome is logged; the
  protocol defines no response. Solutions for unknown or stale IDs are logged
  and ignored.
- Discards messages of extensions it does not implement, as the specification
  requires.
- Closes the connection on a malformed frame, a message only a server sends, a
  message type the protocol does not define, or a client payload over 1 MiB.

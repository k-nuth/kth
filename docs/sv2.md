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
sv2.noise   = true        # default
```

Every setting is also available on the command line with a `--` prefix.

Clients complete the `Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256` handshake
before sending `SetupConnection`. Both the six-byte frame header and every
Template Distribution payload are authenticated and encrypted. The handshake
deadline is `sv2.handshake_timeout_seconds` (default 10). A missing handshake or
an authentication failure closes the connection; there is no plaintext fallback.

To serve plaintext clients on a trusted local interface, explicitly set
`sv2.noise=false`. Remote clients should use Noise.

## Keys and certificates

On the first Noise-enabled start, Knuth creates `.sv2-static-key` and
`.sv2-authority-key` in its working directory, like the RPC cookie. They are
separate raw 32-byte secp256k1 secret keys with owner-only permissions (`0600`).
Existing files must contain exactly one valid secret key, belong to the node's
user, and have no group or other permissions. Invalid files are refused and
never replaced. Key and certificate paths must name regular files, without
symbolic links. Back up both keys: replacing the authority key changes what
clients must pin. Override their paths with `sv2.static_key_file` and
`sv2.authority_key_file`; parent directories must already exist.

By default, Knuth signs a fresh certificate for its static key in every
handshake, so a node that runs for months keeps serving a current one. Each
certificate starts one hour before the node's clock, so a client whose clock is
a little behind still accepts it, and lasts `sv2.certificate_validity_seconds`
after it (default 2592000, or 30 days). The authority key stays the same across
restarts.

Wherever Knuth checks a certificate itself, its own clock may be up to 10
seconds outside the certificate's validity times. A certificate carries whole
seconds, and a server that signs from its current second would otherwise be
refused by a client whose clock is a moment behind it.

Startup prints `Noise authority public key: <key>`. Give that public key to the
Job Declarator or pool over a trusted channel and configure it as the client's
authority public key. It is the SV2 section 4.7 encoding: Base58Check of
`01 00` followed by the 32-byte BIP340 x-only authority public key. SV2 clients
that accept a mining URL use the key in its path, for example:

```text
stratum2+tcp://your-node:8442/<authority-public-key>
```

To keep the authority secret offline, provide a certificate and the pinned
authority public key instead:

```ini
sv2.static_key_file       = /secure/provider.key
sv2.certificate_file      = /etc/kth/provider.cert
sv2.authority_public_key  = <authority-public-key>
```

In this mode Knuth does not open or generate `sv2.authority_key_file`. The
certificate file contains exactly the 74 raw bytes of `SIGNATURE_NOISE_MESSAGE`
(SV2 section 4.5.2): little-endian version U16 (zero), `valid_from` U32,
`not_valid_after` U32, then a 64-byte BIP340 signature. Sign the SHA-256 digest of
the first ten bytes followed by the provider's 32-byte x-only static public key
using the authority secret key. The certificate uses BIP340 Schnorr, which is
different from BCH transaction Schnorr signatures. Knuth verifies the authority
signature, certificate version, validity times and static key match before
binding the listener. Once an offline certificate expires, every handshake is
refused and logged; supply a renewed certificate and restart before that. Secret
file loading requires POSIX file permissions.

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
  a bounded set of recent templates. Once a new tip arrives, requests for the
  templates of earlier tips get `stale-template-id`; unknown IDs get
  `template-id-not-found`.
- A BCH block can hold more than `RequestTransactionData.Success` carries: one
  frame is under 16 MiB, and its list at most 65,535 transactions. Such a
  template is still sent, since a client that mines on the merkle path never
  needs the transactions. A request for them gets `template-too-large`; a
  solution is taken as for any other template.
- `SubmitSolution` reconstructs the full block from the supplied header fields
  and coinbase plus the template's transactions, then submits it through the
  node's block validation and connection pipeline. The outcome is logged; the
  protocol defines no response. The templates of the tip just replaced still
  take solutions until the next tip: such a block competes at the same height
  as the new tip, and the chain decides which one it keeps. Solutions for
  unknown IDs, or for templates of an older tip, are logged and ignored.
- Discards messages of extensions it does not implement, as the specification
  requires.
- Closes the connection on a malformed frame, a message only a server sends, a
  message type the protocol does not define, or a client payload over 1 MiB.

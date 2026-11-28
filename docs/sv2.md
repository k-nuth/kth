# Stratum V2 Template Provider

Knuth can serve block templates to Stratum V2 clients (a Job Declarator or a
pool) over the Template Distribution Protocol. It does not need a build with
JSON-RPC.

Stratum V2 is an [optional feature](optional-features.md): it needs a build
with the Conan option `sv2`, off by default.

```
conan install . -o sv2=True ...
```

A build without it refuses to start with `sv2.enabled = true`.

## Running

In a build with the option the provider is still off by default. Enable it in
the config file:

```ini
sv2.enabled = true
sv2.bind    = 127.0.0.1   # localhost only by default
sv2.port    = 8442
```

The same settings are `--sv2.enabled`, `--sv2.bind` and `--sv2.port` on the
command line.

By default the listener speaks plaintext SV2 frames, with no encryption and no
authentication, as it did before Noise was added. Keep a plaintext listener on
localhost or another trusted interface.

## Noise

With `sv2.noise = true` every connection completes the
`Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256` handshake (sv2-spec 4) before
`SetupConnection`, and both the six-byte frame header and every payload are
then encrypted and authenticated. There is no plaintext fallback: a client that
sends anything else is closed. Pools and Job Declarators that reach their
Template Provider over a network need it.

```ini
sv2.enabled = true
sv2.noise   = true
```

Noise is off by default, so a node that served plaintext clients keeps serving
them after an upgrade. Turning it on is a step for the operator anyway: every
client has to be given the authority public key below.

A connection has 30 seconds to complete the handshake, `SetupConnection` and its
first `CoinbaseOutputConstraints`. Noise does not authenticate the client, so a
client that completes the handshake and then stays silent is closed at the same
deadline as one that never sends its constraints.

### Keys

On the first start with Noise, Knuth creates two raw 32-byte secp256k1 secret
keys with owner-only permissions (`0600`) in its data directory (`db.directory`):

- `sv2_static_key`, the provider's static key;
- `sv2_authority_key`, the authority's key, which signs the provider's
  certificates.

They are kept with the chain, so a node started from another directory keeps
its identity. Other paths can be given with `sv2.static_key_file` and
`sv2.authority_key_file`; a relative path is taken from the data directory, and
its parent directory must exist. A key is written to a temporary file beside
it, flushed to disk and only then given its name, so a write that fails or is
interrupted never leaves a short key behind.

An existing key file must hold exactly one valid secret key, belong to the
node's user and have no group or other permissions. The file itself must not
be a symbolic link; the directories on its path may be. A file that does not
pass is refused and never replaced. Back up both keys: a new authority key
changes what every client has to pin.

Startup logs `Noise authority public key: <key>`. Give that key to the Job
Declarator or pool over a trusted channel and configure it as the client's
authority public key. It is the sv2-spec 4.7 encoding: Base58Check of `01 00`
followed by the 32-byte BIP340 x-only authority public key. SV2 clients that
take a URL carry the key in its path, for example:

```text
stratum2+tcp://your-node:8442/<authority-public-key>
```

`sv2.authority_public_key` may also be set beside the authority key file; the
node then refuses to start if the file holds another key.

### Certificates

With the authority key on the node, Knuth signs a certificate for its static
key at start-up and signs a new one when less than half of its lifetime is
left, so every handshake shows one with at least half a lifetime ahead and a
node that runs for months keeps working. Each certificate starts one hour
before the node's clock, so a client whose clock is a little behind still takes
it, and lasts `sv2.certificate_validity_seconds` (default 2592000, 30 days).

To keep the authority key off the node, give a certificate signed elsewhere
and the authority public key instead:

```ini
sv2.certificate_file     = provider.cert
sv2.authority_public_key = <authority-public-key>
```

Knuth then never opens or creates an authority key file. The certificate file
holds exactly the 74 bytes of `SIGNATURE_NOISE_MESSAGE` (sv2-spec 4.5.2): the
version as a little-endian U16 (zero), `valid_from` and `not_valid_after` as
U32, then a 64-byte BIP340 signature by the authority of the SHA-256 of the
first ten bytes followed by the provider's 32-byte x-only static public key.
BIP340 is not the Schnorr scheme of BCH transactions.

The first start with `sv2.certificate_file` and no certificate there yet creates
the static key and stops with an error that gives its public key in hex: that
is the key to sign. Once a certificate is there, a missing static key file is
not created again, since a new key could not match it. On every start the
signature, the version, the validity window and the static key are checked
before the listener is bound, and the log gives the static public key and the
end of the certificate. When an offline certificate expires while the node
runs, every handshake is refused and the log says so; put a renewed one in
place and restart before then.

### Start-up checks

With `sv2.enabled` and `sv2.noise`, the node does not start when its keys or
its certificate cannot be used: a malformed or unreadable key file, one others
can read, an authority public key that is malformed or does not match, an
invalid, expired or mismatched certificate, or a certificate lifetime of zero.
The log gives the reason.

Key files need POSIX owner-only permissions. A build for Windows refuses
`sv2.noise`; plaintext still works there.

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

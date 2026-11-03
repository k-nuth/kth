# Stratum V2

Knuth can serve block templates to Stratum V2 clients (a Job Declarator or a
pool) over the Template Distribution Protocol. It can also serve solo miners
over the Mining Protocol on the same listener. Both are part of node core and
do not need a build with JSON-RPC.

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
protocol payload are authenticated and encrypted. The handshake
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
  with `SetupConnection.Success`. Unsupported protocols, a version range without
  2, or a Template Distribution feature flag get `SetupConnection.Error` and
  the connection is closed.
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

## Mining Protocol

Mining is off by default. Enable it with `sv2.mining.enabled=true` and set
`sv2.mining.address` to a full CashAddr, including the prefix for the node's
network: `bitcoincash` on mainnet, `bchreg` on regtest, or `bchtest` on the test
networks. A missing address, wrong prefix, invalid checksum, unsupported address
type or disabled listener prevents startup. P2PKH, P2SH and P2SH32 destinations
are supported, including their token-aware address forms. The address receives
the full subsidy and transaction fees; `user_identity` is only a miner label.

For example, on a regtest node, replace the address placeholder with your
wallet's complete regtest CashAddr:

```ini
sv2.enabled        = true
sv2.bind           = 127.0.0.1
sv2.port           = 8442
sv2.noise          = true
sv2.mining.enabled = true
sv2.mining.address = <your-bchreg-cashaddr>
```

Configure the miner with `stratum2+tcp://127.0.0.1:8442/<authority-public-key>`
and the authority key printed by Knuth, as described above. It uses the same
Noise handshake, keys, deadline and frame protection as Template Distribution.
Plaintext still requires the explicit `sv2.noise=false` setting.

- Mining `SetupConnection` uses protocol 0, version 2. Standard jobs and version
  rolling requirements are supported. Work selection and unknown flags are
  refused. Success flags are zero: the server accepts both channel types and
  permits version rolling.
- Standard channels receive `NewMiningJob` with a merkle root computed by the
  node. Extended channels receive `NewExtendedMiningJob` with coinbase prefix
  and suffix, the merkle branch, a unique extranonce prefix and 12 to 32 bytes
  of miner-controlled extranonce space. Requests for more than 32 are refused.
  Each channel has its own group ID; closing that ID closes its channel too.
- The first job is a future job, followed by Mining Protocol `SetNewPrevHash`
  to activate it. Every open channel receives fresh work when a tip connects.
  Jobs from the previous tip are refused, including before replacement work
  is available during a chain transition.
- Jobs use the same `fetch_mining_template` transaction selection as Template
  Distribution. The engine accepts a list of coinbase outputs; this server
  supplies one payout output. BCH coinbases start with the BIP34 height push,
  have no SegWit fields or witness commitment, and cannot create CashTokens.
  Selected transactions retain their CashTokens outputs in ordinary BCH
  serialization. A job generation value in the coinbase keeps replacement work
  on the same tip distinct.
- The initial target aims for one share per 20 seconds at `nominal_hash_rate`,
  capped by the miner's `max_target`. A zero rate uses that maximum, which lets
  regtest miners request easy shares. `UpdateChannel` can change the rate or
  maximum; `SetTarget` announces a changed target for subsequent jobs. Already
  activated jobs keep their target.
- Shares must name the right channel type and active job, use exactly the
  negotiated extranonce size, and hash at or below the job target. Version
  rolling is limited to bits 5 through 28. The timestamp must be at least the
  job's `ntime_start` and at most 7200 seconds above it (local policy).
  Successful shares are acknowledged individually. A hash meeting the network
  target is submitted through the same validation, connection and relay path
  as Template Distribution's `SubmitSolution`.
- Each connection allows 32 open channels and 1024 channel-open requests.
  Client frames are limited to 1 MiB, and job and duplicate histories are
  bounded. After 1024 accepted shares on one job, that channel receives fresh
  work with a different coinbase, without waiting for the next tip. Previous
  jobs become stale. A slow connection has its own bounded input queue; found
  blocks are submitted before waiting for the miner to read their acknowledgements.

Mining errors are printable strings (SV2 section 3.5 allows implementation
specific codes):

| Message | Code | Meaning |
|---|---|---|
| `SetupConnection.Error` | `protocol-version-mismatch` | The range excludes version 2. |
| | `unsupported-feature-flags` | The error flags identify unsupported requirements. |
| | `unsupported-protocol` | Mining is disabled or the protocol is unsupported. |
| `OpenMiningChannel.Error` | `duplicate-request-id` | The request ID has already been used. |
| | `too-many-channels` | The connection's channel limit was reached. |
| | `min-extranonce-size-too-large` | The requested extranonce cannot fit. |
| | `requires-standard-jobs` | This connection requested standard jobs only. |
| `UpdateChannel.Error` | `invalid-channel-id` | No such channel is open. |
| `SubmitShares.Error` | `invalid-channel-id` | No such channel is open. |
| | `invalid-channel-type` | The submission type does not match the channel. |
| | `invalid-job-id` | The job is unknown or no longer retained. |
| | `stale-share` | A retained job has been invalidated. |
| | `invalid-extranonce-size` | The extranonce length differs from the negotiated size. |
| | `invalid-ntime` | The timestamp is outside the job's bounds. |
| | `invalid-version` | The miner changed bits outside the rolling mask. |
| | `difficulty-too-low` | The header hash exceeds the job's share target. |
| | `duplicate-share` | This proof of work was already accepted. |
| | `share-limit` | The bounded share history is full; fresh work replaces this job. |

Malformed frames, invalid hash rates (NaN, infinity, negative values or negative
zero), and server-only messages close the connection. Job Declaration, custom
jobs, pool failover and Stratum V1 are separate features.

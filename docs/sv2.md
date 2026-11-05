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
zero), and server-only messages close the connection. Custom jobs, pool
failover and Stratum V1 are separate features.

## Job Declaration

Job Declaration is off by default (`sv2.jd.enabled=false`). Enabling it requires
the listener, Mining Protocol and its valid solo payout address, both upstream
addresses and pinned Noise authority keys and a user identity. `sv2.jd.mode`
defaults to `coinbase-only`, which sends the pool a merkle path instead of the
transaction list, so no SV2 message limit bounds the template. Invalid enabled
settings stop startup before the chain or listener starts.
Dormant JD settings are ignored when JD is disabled. The solo fallback pays
`sv2.mining.address`.

For example, to negotiate full-template mode with a pool and its JDS:

```ini
sv2.enabled                        = true
sv2.mining.enabled                 = true
sv2.mining.address                 = <your-network-cashaddr>
sv2.jd.enabled                     = true
sv2.jd.mining_address              = pool.example:3333
sv2.jd.mining_authority_public_key = <pool-noise-authority-key>
sv2.jd.jds_address                 = jds.example:3334
sv2.jd.jds_authority_public_key     = <jds-noise-authority-key>
sv2.jd.user_identity               = account.worker
sv2.jd.mode                        = full-template
sv2.jd.response_timeout_seconds    = 10
```

Addresses are `host:port` or `[IPv6]:port`, with an explicit port from 1 to
65535. DNS labels are at most 63 bytes and the whole hostname at most 253;
URLs, paths, credentials and whitespace are refused. Identities are 1 to 255
UTF-8 bytes. Authority keys use the same Base58Check encoding described above.
Each upstream always uses Noise, including when the local listener explicitly
uses plaintext. A different signing authority, a certificate outside its
validity window or an unauthenticated frame ends that attempt.

The Mining connection requests work selection and version rolling, then opens
one extended channel with the configured identity. It requests eight extranonce
bytes: four for local channel allocation and four reserved for downstream
miners to roll. Grants of eight bytes, including behind a four-byte pool prefix,
and larger grants are accepted when the prefix plus extranonce fits 32 bytes.
A fixed-version upstream is refused.
The node retains its own transaction selection; jobs supplied by the pool do
not replace local work.

`full-template` sets `DECLARE_TX_DATA` (specification section 6.2) on the JD
connection. `coinbase-only` leaves that flag clear. The Mining connection does
not negotiate extensions. After `SetupConnection.Success` on the Job Declaration
connection, and before any other Job Declaration message, the node sends
`RequestExtensions` (extension 0x0001) asking for Requested Payout Outputs.
The identifier is `requested_payout_outputs_extension` (`0x5052`), an
experimental identifier that is not in the SV2 extension registry. sv2-spec
PR #249 proposed the extension as `0x0004` and was closed; changing the
constant is the whole switch if it is ever registered.

`RequestExtensions.Success` that includes the identifier turns the extension
on for every later declaration. `RequestExtensions.Error` with an empty
required list, a success that omits the identifier, or no answer within
`sv2.jd.response_timeout_seconds` leaves it off. That silence is not a refusal
and does not close the connection. A required extension this node does not
implement closes the attempt and falls back to solo.

With the extension off, each new template asks the JDS for
`AllocateMiningJobToken` and builds the coinbase from the allocation's
`coinbase_tx_outputs`. Those outputs arrive with zero value; the whole coinbase
revenue is then placed in the first output, which is the pool payout.

With the extension on, the same token is allocated first. The length of its
`coinbase_tx_outputs` is only a reservation. Before `DeclareMiningJob`
(full-template) or `SetCustomMiningJob` (coinbase-only) the node sends
`RequestPayoutOutputs` with that token, `available_payout_value` equal to the
template's `coinbase_tx_value_remaining`, the template height, previous hash
and nbits, and `coinbase_prefix` (the coinbase scriptSig bytes before the
extranonce, the same bytes the job will declare). At most one such request is
in flight.

`RequestPayoutOutputs.Success` with `EXACT_OUTPUTS` set (flag bit 0; every
other bit must be clear) makes the coinbase outputs exactly the returned
outputs, in that order, and no others. The coinbase uses the returned version,
input sequence and lock time, and its scriptSig is `coinbase_prefix` followed
by the extranonce. Without the flag, the returned version, sequence and lock
time must be zero and are ignored. The coinbase then uses version 1, sequence
`0xffffffff` and lock time 0, and it includes every returned output. In both
cases those outputs already sum to the template revenue, so the node does not
move that revenue into the first output. The serialized outputs must fit the
token's reservation and must not carry token data. A malformed success falls
back to solo rather than retrying.

`PayoutOutputsStale` means tokens allocated before the notice are stale. The
reply already in flight is consumed, then the node allocates a new token and
requests again. `stale-payout-outputs` on a token from before that notice is
the same race. One such answer for a token allocated after the latest notice
is retried; a second is a refusal. A notice on every reply cannot allocate
forever: eight restarts are allowed and the next one falls back to solo. Any
other `RequestPayoutOutputs.Error`, or no answer within
`sv2.jd.response_timeout_seconds`, also falls back to solo.

Either way, `full-template` then sends `DeclareMiningJob` with the coinbase
prefix and suffix and the transaction list, answers `ProvideMissingTransactions`
with the raw transactions, and uses the new token from
`DeclareMiningJob.Success`. `coinbase-only` does not send `DeclareMiningJob`
and uses the token already allocated. Either mode then sends
`SetCustomMiningJob` on the mining connection.

Declarations wake on connected-tip notifications and upstream replies. Only
an outstanding response arms a deadline; an idle client does not poll the chain.

Bitcoin Cash differs from the parts of the specification written for Bitcoin.
There is no segregated witness, so the declared "wtxid" list carries ordinary
transaction ids in the template's CTOR order. The coinbase has no witness and
no witness commitment. BIP34 stays in the coinbase script. `excess_data` is
empty because there is no witness reserved value to carry.

Downstream miners keep mining that same template through the Mining Protocol
server. Until a pool coinbase is ready, and again after fallback, they are
paid `sv2.mining.address`. When the extension supplies the outputs, the pool
coinbase those miners receive is that returned coinbase, including a split of
the revenue. With JD enabled, extended channels open with a fixed
four-byte rollable reserve. When the pool coinbase is installed, each accepted
channel nests inside the upstream extranonce and receives `SetExtranoncePrefix`
before the next job. A downstream request for more than four rollable bytes is
refused with `min-extranonce-size-too-large`, even before the upstream channel
exists. Standard channels need no rollable bytes and also receive pool jobs.
Any upstream space beyond the downstream roll is used for local allocation,
so every accepted channel fits the grant. With JD disabled, the Mining Protocol
sizes described above are unchanged. The scriptSig generation and the
extranonce prefix return to the solo channel's own values on fallback; the
template's transactions do not change.

Pool and solo payout changes wake connected miners when they are published.
The listener does not poll for those changes, and stopping it joins every
connection even while the upstreams are quiet or Job Declaration is disabled.

A share that meets the upstream channel's target is forwarded as
`SubmitSharesExtended` with the custom job id and the extranonce bytes after
the pool's prefix, using exactly the granted extranonce size. A share that does
not meet that target is not forwarded.
The target is the one the custom job was sent with; a later `SetTarget` does
not retarget it. A share that meets the network target is submitted locally
through the same block-connection path as solo mining. In `full-template`,
after `DeclareMiningJob.Success`, that solution is also `PushSolution` to the
JDS (specification section 6.4.9). `coinbase-only` has no declared job for
`PushSolution` to name, so the block is connected locally and not pushed.

Fallback follows specification section 6.2. The node returns downstream work
to the solo payout, and logs why, when the JDS does not answer
`AllocateMiningJobToken` or `RequestPayoutOutputs` within
`sv2.jd.response_timeout_seconds`, the JDS sends `DeclareMiningJob.Error` or
any `RequestPayoutOutputs.Error` other than a bounded stale retry, the pool
refuses `SetCustomMiningJob`, the pool refuses a share on an accepted job, or
either connection drops. Negotiation silence is not one of these cases. The
next job after the pool coinbase was in use pays `sv2.mining.address`. A
payout refusal that arrives before that coinbase was published does not emit
another downstream job; miners already on the solo payout keep it. Setup and
extended-channel refusals include the upstream's error code in the fallback
log, bounded in length and restricted to printable ASCII. This PR does not
reconnect and does not try another pool.

Both connections run independently of the listener, the template build and
downstream miners. A slow or silent upstream cannot hold those up: solo jobs
are served while a token, a payout-output request or a declaration is
outstanding, and a network-target share is connected without waiting for the
pool or the JDS to accept it.
Resolution, connection, handshake, partial-frame assembly and writes have
deadlines using `sv2.handshake_timeout_seconds`; setup, the extended channel
and each declaration step use `sv2.jd.response_timeout_seconds`. With JD
enabled, both settings must be from 1 to 600 seconds. A healthy idle
connection has no outstanding response deadline. OS DNS resolution
cancellation still needs validation on Linux; numeric IP addresses avoid the
resolver's OS lookup. Received payloads are at most 1 MiB. Sent messages may
fill a whole SV2 frame, whose U24 length allows 2^24 - 1 bytes. In
`full-template`, a template of more than 65,535 transactions (the SEQ0_64K
list in `DeclareMiningJob`), or a `ProvideMissingTransactions.Success` larger
than one frame, is not declared: that template is mined solo and the log says
why. `coinbase-only` has neither limit. The output queue holds at most eight
frames. Shares that arrive before the custom
job id is known are retained up to 32 and then dropped. An output queue that
refuses a declaration or a share ends the attempt and continues solo. No
reconnect or alternate pool is tried.

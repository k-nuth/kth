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
- A Mining connection that opens no channel within
  `sv2.mining.open_channel_timeout_seconds` (default 60) of its
  `SetupConnection.Success` is closed for inactivity, and the closure is logged
  (SV2 section 5.3.2). A refused open does not count. Only the wait for the
  first channel is limited: a connection whose channels all close later stays
  open. Template Distribution connections have no channels and no such deadline.
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
  maximum; `SetTarget` announces the changed target. Every job is sent as a
  future job, so the new target applies at once to the job being mined as well
  as to later ones (SV2 section 5.3.21). There is no grace period: once a
  harder target is sent, shares that meet only the previous one are refused.
- Shares must name the right channel type and active job, use exactly the
  negotiated extranonce size, and hash at or below the channel's current
  target. Version rolling is limited to bits 5 through 28. The timestamp must
  be at least the job's `ntime_start` and at most 7200 seconds above it (local
  policy).
  Successful shares are acknowledged individually. A hash meeting the network
  target is submitted through the same validation, connection and relay path
  as Template Distribution's `SubmitSolution`. That includes a hash that
  misses a share target harder than the network's, which a device maximum can
  set: the share itself is still refused as `difficulty-too-low`.
- Each connection allows 32 open channels and 1024 channel-open requests.
  Client frames are limited to 1 MiB, and job and duplicate histories are
  bounded. After 1024 accepted shares on one job, that channel receives fresh
  work with a different coinbase, without waiting for the next tip. A slow
  connection has its own bounded input queue; found blocks are submitted
  before waiting for the miner to read their acknowledgements.
- Only a new tip makes jobs stale. Work replaced on the same tip can still
  find a block, so each channel keeps its three newest jobs of the tip, and a
  share on any of them is checked against that job's own coinbase and merkle
  data and the channel's current target. Each job has its own duplicate
  history of 1024 shares; a share that meets the network target is accepted
  and submitted even once that history is full. A share on an older job of
  the tip gets `stale-share`.

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
| | `stale-share` | The job is from an earlier tip, or past the three newest of this one. |
| | `invalid-extranonce-size` | The extranonce length differs from the negotiated size. |
| | `invalid-ntime` | The timestamp is outside the job's bounds. |
| | `invalid-version` | The miner changed bits outside the rolling mask. |
| | `difficulty-too-low` | The header hash exceeds the channel's current share target. A hash that meets the network target is still submitted as a block. |
| | `duplicate-share` | This proof of work was already accepted. |
| | `share-limit` | This job's share history is full; fresh work replaces it. A block is still taken. |

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

Both upstream `SetupConnection` messages identify the node (specification
section 3.6.1): `vendor` is `Knuth` followed by the node's version, for example
`Knuth 1.5.2`, and `hardware_version` is `Knuth Bitcoin Cash full node`.
`firmware` and `device_id` are empty.

The Mining connection requests work selection and version rolling, then opens
one extended channel with the configured identity. It requests eight extranonce
bytes: four for local channel allocation and four reserved for downstream
miners to roll. Grants of eight bytes, including behind a four-byte pool prefix,
and larger grants are accepted when the prefix plus extranonce fits 32 bytes.
A fixed-version upstream is refused.
The node retains its own transaction selection; jobs supplied by the pool do
not replace local work.

Every downstream channel mines on that one upstream channel, so its
`nominal_hash_rate` is the sum of the `nominal_hash_rate` of every open
downstream channel, standard and extended, over all Mining Protocol connections
(specification sections 5.3.2 and 5.3.9). The pool may set the channel's target
from it. The channel request carries the sum when it is sent, which is zero
while no miner is connected; a change while the request is outstanding is sent
once the channel opens. Whenever the sum changes, because a channel opens, a
miner sends `UpdateChannel`, or a channel or its whole connection closes, the
node sends `UpdateChannel` with the new sum and the channel's opening
`max_target`, so an update never asks the pool for a harder target. Updates go
out at most once a second, as section 5.3.7 allows; changes within that second
are sent together as the latest sum. A sum beyond the F32 range is sent as the
largest finite F32 value. An `UpdateChannel.Error` naming this channel is logged
and mining continues: the pool keeps the channel as it was, and the refused sum
is not sent again. One naming another channel, including the group channel, or
a malformed one ends the attempt like any other invalid upstream message.

`full-template` sets `DECLARE_TX_DATA` (specification section 6.2) on the JD
connection. `coinbase-only` leaves that flag clear. For each new template the
node asks the JDS for `AllocateMiningJobToken` and builds the coinbase from
the allocation's `coinbase_tx_outputs`. Those outputs arrive with zero value;
the whole coinbase revenue is then placed in the first output, which is the
pool payout. `full-template` sends `DeclareMiningJob` with the coinbase prefix
and suffix and the transaction list, answers `ProvideMissingTransactions` with
the raw transactions, and uses the new token from `DeclareMiningJob.Success`.
`coinbase-only` does not send `DeclareMiningJob` and uses the allocated token.
Either mode then sends `SetCustomMiningJob` on the mining connection.

Declarations wake on connected-tip notifications and upstream replies. Only
an outstanding response arms a deadline; an idle client does not poll the chain.

Bitcoin Cash differs from the parts of the specification written for Bitcoin.
There is no segregated witness, so the declared "wtxid" list carries ordinary
transaction ids in the template's CTOR order. The coinbase has no witness and
no witness commitment. BIP34 stays in the coinbase script. `excess_data` is
empty because there is no witness reserved value to carry.

Downstream miners keep mining that same template through the Mining Protocol
server. Until a pool coinbase is ready, and again after fallback, they are
paid `sv2.mining.address`. With JD enabled, extended channels open with a fixed
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
The target is the last one the pool sets before `SetCustomMiningJob.Success`,
including a `SetTarget` that crosses `SetCustomMiningJob`. A `SetTarget` after
the Success does not retarget the accepted job (specification section 5.3.21).
A share that meets the network target is submitted locally
through the same block-connection path as solo mining. In `full-template`,
after `DeclareMiningJob.Success`, that solution is also `PushSolution` to the
JDS (specification section 6.4.9), but only when the block was mined on the
declaration the JDS holds now. A block on a replaced declaration's coinbase,
still valid on the same tip, is connected locally, logged and not pushed.
`coinbase-only` has no declared job for `PushSolution` to name, so the block
is connected locally and not pushed.

Fallback follows specification section 6.2. The node returns downstream work
to the solo payout, and logs why, when the JDS does not answer
`AllocateMiningJobToken` within `sv2.jd.response_timeout_seconds`, the JDS
sends `DeclareMiningJob.Error`, the pool refuses `SetCustomMiningJob`, the
pool refuses a share on an accepted job, or either connection drops. The next
job after the pool coinbase was in use pays `sv2.mining.address`. Setup and
extended-channel refusals include the upstream's error code in the
fallback log, bounded in length and restricted to printable ASCII.
This PR does not reconnect and does not try another pool.

Both connections run independently of the listener, the template build and
downstream miners. A slow or silent upstream cannot hold those up: solo jobs
are served while a token or declaration is outstanding, and a network-target
share is connected without waiting for the pool or the JDS to accept it.
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
refuses a declaration, a share or a hash rate update ends the attempt and
continues solo. No reconnect or alternate pool is tried.

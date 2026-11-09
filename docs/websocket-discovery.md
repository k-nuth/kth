# Experimental WebSocket endpoint discovery, version 1

This extension announces **WS/WSS endpoints**, not execution environments.
A native client can use the same endpoints as a browser. It carries connection
hints for the existing BCH protocol; it does not change BCH consensus rules.
The extension is experimental, opt-in and not a network-wide assigned standard.

## Existing protocols and choice

The `version` message communicates the BCH protocol version, service flags,
addresses, a bounded user-agent and other handshake fields. `getaddr` requests
legacy addresses; `addr` carries timestamp, services, IP and TCP port. BIP155
`sendaddrv2`/`addrv2` add network address types and negotiate receiving them.
They still do not represent a WebSocket scheme, DNS hostname and HTTP path.
KTH also has an `xversion` placeholder which currently ignores its payload;
using it would need a separately specified and assigned key-space policy.

Alternatives considered:

* New BIP155 network identifiers or service bits would require coordinated
  allocation and still need a definition for scheme, hostname and route. This
  implementation does not appropriate them or reinterpret TCP ports as WS.
* `xversion` keys could carry capabilities, but KTH does not implement its
  key/value semantics or a coordinated key assignment for this purpose.
* A versioned `sendwsaddr` handshake message explicitly opts in, like feature
  negotiation with `sendaddrv2`. It needs no service-bit or network-ID allocation.
  Existing KTH 1.6.0 and BCHN 29.2.0 peers ignore this unknown command and
  finish the normal BCH handshake. User agents remain identification, as recommended by BIP14.

## Negotiation

When enabled, a node sends one `sendwsaddr` after receiving and validating its
peer's `version`, and before sending its own `verack`. Its payload is exactly one
byte: `0x01`, meaning endpoint discovery version 1. It announces understand/relay,
not an incoming WebSocket listener. Endpoint transport preferences remain in
`getwsaddr`, not in this announcement.

A peer must send its announcement after its `version` and before its `verack`.
Malformed lengths, duplicates during the handshake and announcements before
`version` fail the handshake. The frame length is checked before allocating or
reading the body. Unknown version bytes are well-formed but do not negotiate
version 1. An announcement after `verack` cannot enable discovery for that session.

Only if both sides opt in, and after successful BCH `version`/`verack`, may either
send `getwsaddr` or `wsaddr`. Handshake completion passes the immutable negotiation
result to the session owner. Discovery does not inspect software names or versions.

A normal native user agent is `/Knuth:VERSION/`; the browser client uses
`/Knuth:VERSION(browser)/`. No discovery component is added by default. A disabled
peer sends no announcement and does not activate discovery. Ordinary peers retain
normal block/header synchronization and TCP address exchange.

### Explicit legacy migration mode

Existing experimental nodes use `/wsdisc:1/` in **both** user agents instead of a
handshake announcement. Enable `net.ws_discovery_legacy_agent=true` on a native
node, or call `header_sync::configure_legacy_discovery(true)` before starting a
browser session, only when discovery interoperability with those nodes is needed.
The C counterpart is `kth_node_header_sync_configure_legacy_discovery`. It has the
same settled-only admission as other HeaderSync configuration and returns false
while operations are outstanding. The mode is disabled by default and included in
the runtime settings snapshot.

In this mode a node still sends `sendwsaddr`. It additionally advertises the
legacy component and, **only when no announcement was received**, accepts the
old user-agent negotiation. Both single- and double-slash legacy components remain
recognized. An explicit unknown version never downgrades through a user-agent
marker; malformed announcements still fail. A marker received without the local
legacy opt-in does not enable discovery. No software-brand sniffing is used.

The new and old `getwsaddr`/`wsaddr` payloads are identical. A mixed deployment can
use legacy mode during rollout, then disable it once both peers support the
announcement. A new session without legacy mode can still synchronize with an old
peer, but cannot discover endpoints through it. Other discovery implementations
should coordinate the experimental command before deploying it more broadly.

The capability means “understands and can relay this discovery protocol.” It
**does not** mean “has an incoming WebSocket listener.” A TCP-only node can
participate in discovery and relay verified-public address hints. A browser
can advertise the capability, request peers and return an empty peer list.
Responding to `getheaders` over an established connection likewise does not
make a browser an incoming endpoint.

## Wire layout

The standard BCH message heading, network magic, command and checksum apply.
All payload integers below are unsigned and **little endian**, including the
port, deliberately separate from legacy address formats. There are no optional
trailing fields; unknown version, trailing bytes or malformed fields reject the
whole message. New formats require another explicitly negotiated version.

`getwsaddr` has an exactly ten-byte payload:

| Field | Bytes | Meaning |
|---|---:|---|
| version | 1 | Must be 1 |
| transports | 1 | Bit 0 = WS, bit 1 = WSS; nonzero, no other bits |
| request_id | 8 | Nonzero request identifier |

The receiver can request only WSS when its environment requires encrypted
connections. An enabled TCP peer may request and relay either transport.

`wsaddr` is a solicited response:

| Field | Bytes | Meaning |
|---|---:|---|
| version | 1 | Must be 1 |
| request_id | 8 | Must match the outstanding request |
| count | 1 | Zero through 16 |
| endpoint records | variable | Exactly count records |

Each endpoint record has:

| Field | Bytes | Meaning |
|---|---:|---|
| transport | 1 | Exactly 1 for WS or 2 for WSS |
| expires | 4 | Unix UTC seconds, in the future and at most 24 hours ahead |
| port | 2 | Nonzero destination port |
| host_length | 1 | 1 through 253 |
| host | host_length | Canonical ASCII DNS hostname or IP literal, no brackets |
| path_length | 2 | 1 through 256 |
| path | path_length | HTTP origin-form route, beginning with `/` |

Payloads are at most 16384 bytes. A zero-count response is valid and does not
imply a protocol error. Duplicate `(transport, host, port, path)` keys inside
one reply reject the reply. Expiry is not part of the identity.

## Validation and public eligibility

Only lowercase ASCII DNS names and canonical IP strings are stored. Hostnames
are case-folded, IPv6 is canonicalized, and IPv4-mapped IPv6 becomes IPv4. DNS
labels follow LDH syntax with at most 63 bytes per label, no trailing dot, no
credentials, scope identifiers or embedded ports. International names must be
supplied as ASCII A-labels. A public candidate needs a multi-label name with a
nonnumeric final label; local, internal, reserved and special-routing suffixes
are refused by default.

Ports are explicit and paths preserve the route. Queries, fragments,
backslashes, controls, non-ASCII route bytes, dot traversal, malformed percent
escapes and encoded delimiters/control bytes are refused. Percent hex digits
are normalized to uppercase. URI schemes are exactly lowercase `ws://` or
`wss://`; userinfo is not supported. IPv6 URL literals need brackets, but the
wire host field does not contain them. Default ports are applied only when
parsing an explicit configured WS/WSS URL, never inferred from a TCP address.

The default numeric policy excludes private, loopback, link-local, unspecified,
multicast, documentation, benchmark, transition and other selected
special-purpose IP ranges. IPv6 eligibility is conservatively limited to
ordinary global unicast. A syntactically public DNS name remains an unverified
candidate until the discovery owner resolves it and classifies **all** answers
as public. The owner must reject empty resolution and any nonpublic answer.
The store does no DNS or network I/O and does not claim reachability or ownership.
Numeric public literals can be classified without DNS and relay immediately.

An explicitly enabled LAN mode permits operator-selected private endpoints.
Private records still need an explicit LAN approval event before relay; merely
receiving private gossip does not grant approval. Unspecified and multicast
addresses remain invalid even in LAN mode. DNS answers can change; successful
classification is not authentication or a permanent liveness promise. Consumers
must apply their own connection policy and validate WSS certificates. Never use
an announcement as permission to access private services or trust chain data.

## Resource and relay policy

One coroutine owns each peer's discovery session policy. It admits at most one
incoming request and one outgoing request per 60 seconds. There is at most one
outstanding request; a reply must arrive within 30 seconds, match its identifier,
contain only requested transports and satisfy the payload/count bounds. A valid
reply consumes that outstanding request exactly once. Unsolicited replies and
replays do not enter the store. No unsolicited announcement broadcast exists in
version 1: relay occurs only through solicited bounded replies.

One discovery task owns the endpoint store. Other tasks send bounded explicit
events to that owner; they do not mutate the store directly. The store has at
most 256 candidates/approved records combined, deduplicated by endpoint key.
At capacity it refuses a new key; gossip cannot evict an approved endpoint by
flooding the store. Repeated gossip does not extend an existing verification
lease. DNS candidates cannot be selected for relay until an owner approval
arrives. Public numeric candidates can be selected immediately. Replies return
at most 16 eligible endpoints and rotate selected entries behind the remaining
entries to avoid permanently hiding later destinations. All records expire on
incoming ingest/approval/selection events without polling.

The node must additionally bound pending discovery-owner work and DNS requests.
A refused request is not silently accepted: it is dropped or the requesting
session is closed according to integration policy, without creating unbounded
queued work. Incoming transport message limits should reject oversized discovery
headings before allocating their full BCH payload.

## Listener identity and transport

The listener bind is local routing configuration, not an advertised endpoint.
Only explicit public URL configuration is considered for local advertisement,
and it must pass public-address verification unless LAN mode was deliberately
enabled. A bind of `0.0.0.0`, loopback or a private address never invents a public
URL. The public `wss://` host/port/path can refer to a TLS reverse proxy whose
private WS backend binds elsewhere. Browser pages served over HTTPS must select
WSS endpoints. A WSS URL must not silently downgrade to WS.

On both TCP and WebSocket, the BCH `version`/`verack`, peer manager, framing,
headers requests and headers service are the same. WebSocket message boundaries
are unrelated to BCH message boundaries. Discovery does not provide block,
UTXO, wallet or header-coordinator implementation.

Connections are intended to persist after header synchronization. Normal closure
is not proof of malicious data; connection recovery should use explicit backoff
and transport-aware close outcomes, not a 15-second disconnect/reconnect loop.
Proxy source addresses must not be trusted as end-client identity without an
explicit trusted-proxy policy. This extension does not grant blanket ban immunity
to localhost, proxies or WebSocket clients.

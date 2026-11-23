# Outbound P2P over Tor

The SOCKS5 proxy is an optional feature: the node has it when built with the
Conan option `socks5=True` ([optional-features.md](optional-features.md)). A
build without it refuses to start with `net.proxy` set; it does not connect
directly in its place.

Run a Tor client with a SOCKS5 listener, then set the node's outbound proxy:

```ini
net.proxy = 127.0.0.1:9050
net.proxy_randomize = true
# Repeat net.peer with the clearnet peers you want to use.
# net.peer = peer.example.org:8333
```

Use the numeric address and port of your Tor client's SOCKS listener. IPv6 proxy
addresses use brackets, for example `[::1]:9050`. An empty `net.proxy` disables
proxying. A malformed proxy address or a proxy failure makes the connection fail;
the node never falls back to connecting directly.

When configured, the proxy carries every outbound P2P connection, including
configured peers and peers selected from the peer database. Destination hostnames
are sent to the proxy for resolution, without local DNS lookup. At startup and
when reseeding, built-in seeds and `net.seed` hostnames are sent through SOCKS5 as
domain-name P2P seed connections. A DNS seed that does not also serve P2P may
refuse the connection; configure `net.peer` entries if needed to bootstrap an
empty peer database. Addresses learned from peers can then populate it.

`net.proxy_randomize` defaults to `true` and offers both unauthenticated SOCKS5
and username/password authentication. If the proxy selects username/password,
the node sends fresh credentials for each connection; if it selects no
authentication, the node continues without credentials. Tor keeps streams with
different SOCKS credentials on separate circuits by default (`IsolateSOCKSAuth`). Set
`net.proxy_randomize` to `false` to offer only unauthenticated SOCKS5.

Proxied sessions keep their requested destination as their identity, rather than
the proxy's socket address. Numeric peers retain their per-peer database and ban
records. Hostname destinations have distinct runtime identities but no peer
database records, bans, misbehavior scores, performance samples, or cooldowns
because their numeric addresses are unknown. An error that would ban a numeric
peer on its own disconnects a hostname peer.

The connection deadline (`net.connect_timeout_seconds`, 5 by default) covers the
whole exchange with the proxy, which answers only once it has reached the
destination: through Tor that includes building a circuit. Raise it if outbound
connections time out.

`net.ws_discovery` is refused together with `net.proxy`: it resolves the names
peers announce with the system's resolver, which no proxy carries.

These options affect outbound P2P traffic. They do not change inbound listening,
RPC, or mining services. Onion peer identities, onion address advertisement,
`onion`/`onlynet` options, and I2P are not supported by this feature.

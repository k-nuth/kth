# Outbound P2P over Tor

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

When configured, the proxy carries outbound IP, DNS and onion P2P connections, including
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
records. Tor v3 onion addresses are validated, kept in the peer database, and
retain their own ban and misbehavior records. They are sent to the proxy as
domain-name CONNECT requests and are never resolved locally or dialled directly.
Learned onion peers are kept and relayed in `addrv2` even without a proxy; automatic
selection skips them until a proxy is configured. Overlay addresses are never
included in legacy `addr` messages. Other hostname destinations have distinct runtime identities but no peer
database records, bans, misbehavior scores, performance samples, or cooldowns
because their numeric addresses are unknown. An error that would ban a numeric
peer on its own disconnects a hostname peer.

These options affect outbound P2P traffic. They do not change inbound listening,
RPC, or mining services. `onion`/`onlynet` options and an inbound Tor service are
not provided. [I2P support](i2p.md) uses its own SAM bridge and takes precedence
over the SOCKS proxy for I2P destinations.

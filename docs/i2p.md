# P2P over I2P

kth connects to and accepts BCH peers through an I2P router's SAM 3.1 bridge.
With i2pd, enable the bridge in `i2pd.conf`:

```ini
[sam]
enabled = true
address = 127.0.0.1
port = 7656
```

Start i2pd, then configure kth:

```ini
net.i2psam = 127.0.0.1:7656
net.i2p_accept_incoming = true
net.inbound_connections = 8
# Repeat with the addresses of BCH peers on the same BCH network.
# net.peer = <52 lowercase base32 characters>.b32.i2p:0
```

Java I2P also works with its SAM application bridge enabled. The bridge address
must be a numeric IP and nonzero port; IPv6 uses brackets, for example
`[::1]:7656`. An empty `net.i2psam` (the default) disables I2P. The router can
start later: kth creates its session in the background and reconnects with
backoff if the router disconnects. Manual I2P peers retry when the session is
ready. SAM exchanges have a deadline of at least 90 seconds, or
`net.connect_timeout_seconds` when larger, because I2P tunnel construction can
take a minute.

`net.i2p_accept_incoming` defaults to `true`. On the first successful connection
to the router, kth generates an Ed25519 destination and saves its private key in
`i2p_private_key` alongside the peer database (`peers.dat`, configured with
`net.hosts_file`). Set `net.i2p_private_key_file` to choose another path. The file
has owner read/write permissions only. Keep it private and back it up to preserve
your address. An unreadable, malformed or unwritable key file prevents the I2P
session from starting; kth does not silently replace it.

The log reports `[sam] I2P address: <address>.b32.i2p` once the session is ready.
Share that address with other BCH operators as `net.peer = <address>.b32.i2p:0`.
When incoming connections are enabled, kth advertises it to peers that negotiated
`sendaddrv2`. I2P accepts share the `net.inbound_connections` limit with TCP peers.
Setting `net.inbound_port = 0` disables the TCP listener while allowing I2P accepts.
Setting `net.i2p_accept_incoming = false` uses an in-memory transient destination
and does not read or write the private key file or advertise an I2P address.
That transient address lasts for the SAM session and changes when it is recreated.

SAM 3.1 has no ports. Use port 0 for I2P peers; a nonzero I2P port is not dialled.
IP and onion peer records require a nonzero port. Learned I2P and Tor v3 addresses
are retained in the peer database and relayed in `addrv2`, including when their
transport is disabled. Legacy `addr` messages contain only IP addresses. Bans,
misbehavior scores and duplicate checks distinguish overlay identities from
each other and from the router or SOCKS proxy's IP address.

I2P names go only to SAM. Onion names go only to the configured
[SOCKS5 proxy](outbound-tor.md). Neither is resolved locally or dialled directly;
unavailable transports are excluded from automatic peer selection. Ordinary IP
and DNS peers retain their existing connection behavior. There is no `onlynet`
option: to keep a node on I2P alone, give it no seeds (regtest has none), disable
TCP with `net.inbound_port = 0`, and configure only I2P `net.peer` entries.

A [Stratum V2 Job Declaration](sv2.md#job-declaration) upstream with an I2P
address uses the same bridge through a separate session with a transient
destination, whatever `net.i2p_accept_incoming` is.

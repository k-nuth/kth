# Optional features

Some features are not part of the Bitcoin Cash protocol that every node speaks:
a node works on the network without them, and other implementations may or may
not have them. Each of those is built only when asked for.

## The rule

An optional feature has:

- **A Conan option** named after the feature, **off by default**. It sets the CMake
  option `KTH_WITH_<FEATURE>` and the compile definition of the same name.
- **Its sources behind that option.** With the option off, nothing of the
  feature is compiled, and its headers refuse to be included.
- **Its tests in an executable of their own**, built only with the option on.
- **One workflow** that builds with the option on and runs those tests, and no
  others. The main workflows build with every optional feature off.

One option covers a whole feature: what is already in the tree and what later
pull requests add to it.

## Features

| Option | Feature | Tests | Workflow |
|---|---|---|---|
| `neutrino` | Compact block filters for light wallets (Neutrino, BIP157/158): the Golomb-coded set, the regular filter of a block, the script filter of a block with its entry tree, the six filter messages (`getcfilters`, `cfilter`, `getcfheaders`, `cfheaders`, `getcfcheckpt`, `cfcheckpt`), the four messages of the script filter tree (`getcfrange`, `cfrange`, `getcfproof`, `cfproof`), and serving the regular filter to peers ([compact-filters.md](compact-filters.md)) | `kth_domain_neutrino_test`, `kth_node_neutrino_test` | `neutrino.yml` |
| `socks5` | Outbound connections through a SOCKS5 proxy, for a node behind Tor: `net.proxy` and `net.proxy_randomize` ([outbound-tor.md](outbound-tor.md)). A build without it refuses to start with `net.proxy` set, and never connects directly in its place | `kth_network_socks5_test`, `kth_node_socks5_test` | `socks5.yml` |
| `i2p` | BCH peers over I2P through a router's SAM 3.1 bridge, outbound and inbound: `net.i2psam`, `net.i2p_accept_incoming` and `net.i2p_private_key_file` ([i2p.md](i2p.md)). A build without it refuses to start with `net.i2psam` set. Every build keeps and relays the I2P and Tor v3 addresses peers announce | `kth_network_i2p_test`, `kth_node_i2p_test` | `i2p.yml` |
| `bloom_filters` | Serving wallets that load a bloom filter (BIP37): `filterload`, `filteradd`, `filterclear`, `merkleblock` answers and filtered announcements, under `node.bloom_filters` ([bloom-filters.md](bloom-filters.md)). A build without it never sets the bloom filter service bit and refuses to start with `node.bloom_filters = true` | `kth_node_bloom_filters_test` | `bloom-filters.yml` |
| `sv2` | Stratum V2 for pools and miners: the frame header, the protocol's data types, the Template Distribution messages and the connection that reads and writes them (sv2-spec 03 and 07), and the Template Provider that serves templates to a pool or a Job Declarator, under `sv2.enabled` ([sv2.md](sv2.md)). A build without it refuses to start with `sv2.enabled` set | `kth_node_sv2_test` | `sv2.yml` |

`neutrino` needs `currency=BCH`: the regular filter it builds is the one of
Bitcoin Cash, which is not the basic filter of BIP158.

## Building with a feature on

```
conan install conanfile.py --version 0.0.0 -of build --build=missing \
    -o tests=True -o neutrino=True
cmake --preset conan-release -DGLOBAL_BUILD=ON -DENABLE_TEST=ON
cmake --build --preset conan-release --target kth_domain_neutrino_test kth_node_neutrino_test
```

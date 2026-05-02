# knuth <a target="_blank" href="https://github.com/k-nuth/kth/releases">![GitHub Releases][badge.release]</a> <a target="_blank" href="https://github.com/k-nuth/kth/actions">![Build status][badge.GithubActions]</a> <a href="#">![CPP][badge.cpp]</a> <a target="_blank" href="https://deepwiki.com/k-nuth/kth">![Ask DeepWiki][badge.deepwiki]</a> <a target="_blank" href="https://t.me/knuth_cash">![Telegram][badge.telegram]</a>

> High-performance Bitcoin Cash development platform.

Knuth is a high-performance implementation of the Bitcoin protocol aimed at users that need extra performance and flexibility — wallets, exchanges, block explorers and miners.

## Not just a node

Knuth is a multi-crypto full node, but also a development platform.

The core is written in C++23. On top of it we ship a set of libraries and modules in several languages that you can use as the foundation for your own application:

[C++](https://github.com/k-nuth/kth/tree/master/src/node) · [C](https://github.com/k-nuth/kth/tree/master/src/c-api) · [JavaScript](https://github.com/k-nuth/js-api) · [TypeScript](https://github.com/k-nuth/js-api) · [JS/TS WebAssembly](https://github.com/k-nuth/js-wasm) · [C#](https://github.com/k-nuth/cs-api) · [Python](https://github.com/k-nuth/py-api)

You can also build your own binding in any language on top of our [C library](https://github.com/k-nuth/kth/tree/master/src/c-api).

## Core Components

### 🏗️ Core Infrastructure (this repository)

#### 🚀 Node Executable
The complete Bitcoin Cash node, ready to run. Suitable as a standalone full node, miner backend, or as the storage tier for an application that needs blockchain access.

#### 🔧 C++ Library
The C++23 library at the core of the node, exposing direct access to every protocol primitive. This is the layer to use when you want maximum performance and the full surface area.

#### 🌐 C API
A stable C ABI on top of the C++ library. This is the foundation every other-language binding builds on, and the layer to use from C, language bindings, FFI, or anything that prefers a versioned C interface to a templated C++ one.

### 🌍 Language bindings (separate repositories)

Built on top of the C API:

| Language | Repository | Notes |
|---|---|---|
| 🟨 JavaScript / TypeScript | [`js-api`](https://github.com/k-nuth/js-api) | Full-featured Node.js binding |
| 🟦 WebAssembly (browser) | [`js-wasm`](https://github.com/k-nuth/js-wasm) | Browser-compatible WASM binding |
| 🟣 C# | [`cs-api`](https://github.com/k-nuth/cs-api) | .NET on Windows, Linux and macOS |
| 🐍 Python | [`py-api`](https://github.com/k-nuth/py-api) | Pythonic interface |

## 📚 Documentation

- **Project site & guides**: [kth.cash/docs](https://kth.cash/docs/)
- **Ask DeepWiki**: [deepwiki.com/k-nuth/kth](https://deepwiki.com/k-nuth/kth) — auto-generated, conversational documentation indexed over the codebase. Useful when you want to ask "where is X handled?" or "show me an example of Y" without grepping the tree yourself.
- **Branch & contribution conventions**: [`docs/BRANCH_CONVENTIONS.md`](docs/BRANCH_CONVENTIONS.md)
- **Build with tests**: [`docs/BUILD_WITH_TESTS.md`](docs/BUILD_WITH_TESTS.md)
- **Version history & repo transition**: [`docs/VERSION_HISTORY.md`](docs/VERSION_HISTORY.md)

## 🧪 Testing

Knuth ships a comprehensive test suite that runs automatically during builds and in CI. Tests are enabled by default in every build configuration.

The build scripts take a Conan package version as their first argument (it gets stamped into the lockfile, not the binary). Substitute `<VERSION>` with the latest release tag from [GitHub Releases](https://github.com/k-nuth/kth/releases) — or any string if you only care about a local dev build.

```bash
# Build and test (Release)
./scripts/build.sh <VERSION>

# Build and test (Debug)
./scripts/build-debug.sh <VERSION>

# Run only tests (after building)
./scripts/run-tests.sh

# Test-focused build
./scripts/test.sh <VERSION>
```

For details, see [`docs/BUILD_WITH_TESTS.md`](docs/BUILD_WITH_TESTS.md).

## Getting Started

### Prerequisites

Every Knuth component is published as a single unified Conan package.

1. Install the build helper:
   ```bash
   $ pip install kthbuild --user --upgrade
   $ conan config install https://github.com/k-nuth/ci-utils/raw/master/conan/config2023.zip
   ```

2. Install the unified Knuth package. Replace `<VERSION>` with the latest release tag from [GitHub Releases](https://github.com/k-nuth/kth/releases):
   ```bash
   $ conan install --requires=kth/<VERSION> --update --deployer=direct_deploy
   ```

The deployment lays out:

- `bin/kth` — the node executable
- `lib/` — every static library (`libnode.a`, `libc-api.a`, …)
- `include/kth/` — C++ and C API headers

### 🚀 Running the node executable

```bash
# Run the node
$ ./kth/bin/kth
```

### 🔧 Using the C++ library

The example below constructs a node with mainnet defaults and queries the current chain tip from the local database. No `config.cfg` file required — the `configuration` constructor pulls in the network defaults for you.

```cpp
// example.cpp
#include <print>

#include <kth/node.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/domain/config/network.hpp>

int main() {
    // Mainnet defaults — equivalent to what the node-exe ships with.
    kth::node::configuration cfg{kth::domain::config::network::mainnet};
    kth::node::executor node{cfg};

    std::size_t height = 0;
    if (node.node().chain().get_last_height(height)) {
        std::println("Current height: {}", height);
    }
    return 0;
}
```

Compile against the static libraries shipped under `lib/`:

```bash
$ g++ -std=c++23 example.cpp -I./kth/include -L./kth/lib \
    -lnode -lblockchain -ldomain -linfrastructure
```

### 🌐 Using the C API

The C API mirrors the same pattern: `kth_config_settings_default(kth_network_mainnet)` returns a pre-populated settings struct that you hand to `kth_node_construct`.

```c
// hello_knuth.c
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <kth/capi.h>

int main(void) {
    // Mainnet defaults; no config file required.
    kth_settings settings = kth_config_settings_default(kth_network_mainnet);
    kth_node_t node = kth_node_construct(&settings, /*stdout_enabled=*/1);

    kth_chain_t chain = kth_node_get_chain(node);
    kth_size_t height = 0;
    if (kth_chain_sync_last_height(chain, &height) == kth_ec_success) {
        printf("Current height: %" PRIu64 "\n", (uint64_t)height);
    }

    kth_node_destruct(node);
    return 0;
}
```

Link against the C API library:

```bash
$ gcc hello_knuth.c -I./kth/include -L./kth/lib -lc-api
```

For deeper guides see the [project documentation](https://kth.cash/docs/).

## Releases

Latest release across the family:

| Component | Version |
|---|---|
| <img alt="kth" src="https://github.com/k-nuth/cs-api/raw/master/docs/images/kth-purple.png" width="35" height="35" /> Mono-repo | <img src="https://img.shields.io/github/v/release/k-nuth/kth?display_name=tag&style=for-the-badge&color=3b009b&logo=bitcoincash" /> |
| <img alt="C++" src="https://kth.cash/images/libraries/cpp.svg" width="35" height="35" /> C++ | <img src="https://img.shields.io/github/v/release/k-nuth/kth?display_name=tag&style=for-the-badge&color=00599C&logo=cplusplus" /> |
| <img alt="C" src="https://kth.cash/images/libraries/c.svg" width="35" height="35" /> C | <img src="https://img.shields.io/github/v/release/k-nuth/kth?display_name=tag&style=for-the-badge&color=A8B9CC&logo=c" /> |
| <img alt="JavaScript / TypeScript" src="https://kth.cash/images/libraries/javascript.svg" width="35" height="35" /> JS / TS (`@knuth/bch`) | <img src="https://img.shields.io/npm/v/@knuth/bch?logo=npm&style=for-the-badge" /> |
| <img alt="JS/TS WebAssembly" src="https://kth.cash/images/libraries/wasm.svg" width="35" height="35" /> JS / TS WebAssembly (`@knuth/js-wasm`) | <img src="https://img.shields.io/npm/v/@knuth/js-wasm?logo=npm&style=for-the-badge" /> |
| <img alt="C#" src="https://kth.cash/images/libraries/csharp.svg" width="35" height="35" /> C# (NuGet `kth-bch`) | <img src="https://img.shields.io/nuget/v/kth-bch?logo=nuget&label=release&style=for-the-badge" /> |
| <img alt="Python" src="https://kth.cash/images/libraries/python.svg" width="35" height="35" /> Python (PyPI `kth`) | <img src="https://img.shields.io/pypi/v/kth?logo=python&style=for-the-badge&color=3776AB" /> |

For the story behind the version numbers (and why Node appears to "skip" 0.59 → 0.67), see [`docs/VERSION_HISTORY.md`](docs/VERSION_HISTORY.md).

## Key Features

### Performance matters

Knuth was built as a high-performance node. The build system can detect the microarchitecture of your processor at build time and produce an optimized binary for it. If you don't want to wait for compilation, we ship pre-built binaries targeting [Intel's Haswell microarchitecture](https://en.wikipedia.org/wiki/Haswell_(microarchitecture)). The build system handles the choice automatically.

### Modular architecture

The codebase is organized as a set of focused modules — `infrastructure`, `domain`, `consensus`, `database`, `blockchain`, `network`, `node`, `c-api`. Each one is small enough to read in an afternoon and replace if you need to. Protocol changes can be introduced in Knuth significantly faster than in monolithic reference implementations.

### Cross-platform

Knuth runs on any 64-bit system. We routinely test x86-64 on FreeBSD, Linux, macOS and Windows; ARM64 on macOS is also part of the CI matrix. If you hit a problem on a platform that isn't in the matrix, please [let us know](https://github.com/k-nuth/kth/issues).

## Language Bindings

<a href="https://github.com/k-nuth/kth/tree/master/src/node"><img alt="C++" src="https://kth.cash/images/libraries/cpp.svg" width="80" height="80" /></a>
<a href="https://github.com/k-nuth/kth/tree/master/src/c-api"><img alt="C" src="https://kth.cash/images/libraries/c.svg" width="80" height="80" /></a>
<a href="https://github.com/k-nuth/js-api"><img alt="JavaScript" src="https://kth.cash/images/libraries/javascript.svg" width="80" height="80" /></a>
<a href="https://github.com/k-nuth/js-api"><img alt="TypeScript" src="https://kth.cash/images/libraries/typescript.svg" width="80" height="80" /></a>
<a href="https://github.com/k-nuth/js-wasm"><img alt="JS/TS WebAssembly" src="https://kth.cash/images/libraries/wasm.svg" width="80" height="80" /></a>
<a href="https://github.com/k-nuth/cs-api"><img alt="C#" src="https://kth.cash/images/libraries/csharp.svg" width="80" height="80" /></a>
<a href="https://github.com/k-nuth/py-api"><img alt="Python" src="https://kth.cash/images/libraries/python.svg" width="80" height="80" /></a>

## Donations

Knuth is community-backed. Donations subsidize development costs, general maintenance and support.

`bitcoincash:qrlgfg2qkj3na2x9k7frvcmv06ljx5xlnuuwx95zfn`

See [fund.kth.cash](https://fund.kth.cash/) for active Flipstarter campaigns.

## License

Knuth is released under the MIT license. See [`COPYING`](COPYING) or [opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).

## Contributing

### Branch naming conventions

To optimize CI/CD throughput we route branches by prefix:

| Prefix | Use for | CI |
|---|---|---|
| `docs/` | Documentation-only changes | Fast (~30s) |
| `style/` | Formatting, copyright headers, comment fixes | Fast |
| `chore/` | Maintenance, dependency bumps | Fast |
| `noci/` | Explicitly skip heavy CI | Fast |
| anything else (`feature/`, `fix/`, `refactor/`, …) | Code changes | Full pipeline |

📖 See [`docs/BRANCH_CONVENTIONS.md`](docs/BRANCH_CONVENTIONS.md) for detailed examples.

## Contact

[Telegram](https://t.me/knuth_cash) group, or email info@kth.cash.

## Security disclosures

Please report security issues to `security@kth.cash`.


<!-- Links -->
[badge.Cirrus]: https://api.cirrus-ci.com/github/k-nuth/kth.svg?branch=master
[badge.GithubActions]: https://img.shields.io/endpoint.svg?url=https%3A%2F%2Factions-badge.atrox.dev%2Fk-nuth%2Fkth%2Fbadge&style=for-the-badge
[badge.version]: https://badge.fury.io/gh/k-nuth%2Fkth-kth.svg
[badge.release]: https://img.shields.io/github/v/release/k-nuth/kth?display_name=tag&style=for-the-badge&color=3b009b&logo=bitcoincash
[badge.cpp]: https://img.shields.io/badge/C++-23-blue.svg?logo=c%2B%2B&style=for-the-badge
[badge.deepwiki]: https://img.shields.io/badge/Ask-DeepWiki-3b009b?style=for-the-badge&logo=readthedocs&logoColor=white
[badge.telegram]: https://img.shields.io/badge/telegram-badge-blue.svg?logo=telegram&style=for-the-badge

<!-- [badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg -->

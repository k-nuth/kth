# Bitprim <a target="_blank" href="http://semver.org">![Version][badge.version]</a> <a target="_blank" href="https://travis-ci.org/bitprim/bitprim-node-exe">![Travis status][badge.Travis]</a> <a target="_blank" href="https://ci.appveyor.com/projects/bitprim/bitprim-node-exe">![Appveyor Status][badge.Appveyor]</a> 

> Multi-Cryptocurrency full-node and development platform

*Bitprim* allows you to run a full [Bitcoin Cash](https://www.bitcoincash.org/)/[Bitcoin](https://bitcoin.org/)/[Litecoin](https://litecoin.org/) node,
with all four main features:
  * Wallet
  * Mining
  * Full blockchain
  * Routing

*Bitprim* also works as a cryptocurrency development platform with several programmable APIs:
  * C++
  * C
  * C#
  * Python
  * Javascript
  * Rust
  * Golang

... and networking APIs: 
  * bitprim-insight: A Bitprim implementation of the Insight-API
  * JSON-RPC
  * Libbitcoin BS-BX protocol

## Installation Requirements

- 64-bit machine.
- [Conan](https://www.conan.io/) package manager, version 1.4.0 or newer. See [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended).

## Installation Procedure

The *Bitprim* executables can be installed on Linux, macOS, FreeBSD, Windows and others. These binaries are pre-built for the most usual operating system/compiler combinations and hosted in an online repository. If there are no pre-built binaries for your platform, a build from source will be attempted.

So, for any platform, an installation can be performed in 2 simple steps:

1. Configure the Conan remote
```
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
```

2. Install the appropriate executable

```
# For Bitcoin Cash (lastest version)
conan install bitprim-node-exe/0.X@bitprim/stable 
```

For BTC and LTC please refer to [documentation](https://bitprim.github.io/docfx/content/user_guide/installation.html)

In you want to tune the installation for better performance, please refer to [this](https://bitprim.github.io/docfx/content/user_guide/installation.html#advanced-installation).


## Running the node

In order to run the full node, you have to initialize the database and then run the node:

1. Run the following to initialize the database:

```./bn -i```

2. Finally, run the node:

```./bn```

The above commands use the default configuration hardcoded in the executable. You can use a configuration file to customize the behavior of the node. In the [bitprim-config](https://github.com/bitprim/bitprim-config) repository you can find some example files.

1. Initialize the database using a configuration file:

```./bn -i -c <configuration file path>```

2. Run the node using a configuration file:

```./bn -c <configuration file path>```

## Detailed documentation

* [Documentation Site](https://bitprim.github.io/docfx/index.html)
* [Build manually from source](https://bitprim.github.io/docfx/content/user_guide/installation.html)
* [API's documentation](https://bitprim.github.io/docfx/content/developer_guide/introduction.html)

## Changelog

To view the change logs and release notes please go [here](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes.md)

<!-- Links -->
[badge.Appveyor]: https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim-node-exe?svg=true&branch=release-0.12.0
[badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg
[badge.Travis]: https://travis-ci.org/bitprim/bitprim-node-exe.svg?branch=release-0.12.0
[badge.version]: https://badge.fury.io/gh/bitprim%2Fbitprim-node-exe.svg

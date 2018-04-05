# Bitprim <a target="_blank" href="http://semver.org">![Version][badge.version]</a> <a target="_blank" href="https://travis-ci.org/bitprim/bitprim-node-exe">![Travis status][badge.Travis]</a> [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim-node-exe?svg=true&branch=master)](https://ci.appveyor.com/projects/bitprim/bitprim-node-exe) <a target="_blank" href="https://gitter.im/bitprim/Lobby">![Gitter Chat][badge.Gitter]</a>

> Multi-Cryptocurrency full-node and development platform

*Bitprim* allows you to run a full [Bitcoin](https://bitcoin.org/)/[Bitcoin Cash](https://www.bitcoincash.org/)/[Litecoin](https://litecoin.org/) node,
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
- [Conan](https://www.conan.io/) package manager, version 1.1.0 or newer. See [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended).

## Installation Procedure

The *Bitprim* executables can be installed on Linux, macOS, FreeBSD, Windows and others. These binaries are pre-built for the most usual operating system/compiler combinations and hosted in an online repository. If there are no prebuilt binaries for your platform, a build from source will be attempted.

So, for any platform, an installation can be performed in 2 simple steps:

1. Configure the Conan remote
```
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
```

2. Install the appropriate executable

```
# For Bitcoin Cash
conan install bitprim-node-exe/0.8@bitprim/stable -o currency=BCH 
# ... or (BCH is the default crypto)
conan install bitprim-node-exe/0.8@bitprim/stable 

# For Bitcoin Legacy
conan install bitprim-node-exe/0.8@bitprim/stable -o currency=BTC

# For Litecoin
conan install bitprim-node-exe/0.8@bitprim/stable -o currency=LTC
```

## Building from source Requirements

In the case we don't have prebuilt binaries for your plarform, it is necessary to build from the source code, so you need to add the following requirements to the previous ones:

- C++11 Conforming Compiler.
- [CMake](https://cmake.org/) building tool, version 3.4 or newer.

## Running the node using the default configuration

In order to run the full node you have to initialize the database and then run the node:

Run the following to initialize the database:

```./bn -i```

... then, run the node:

```./bn```

The above commands use the default configuration hardcoded in the executable. You can use a configuration file to customize the behavior of the node. In the [bitprim-config](https://github.com/bitprim/bitprim-config) repository you can find some example archives.

Initialize the database using a configuration file:

```./bn -i -c <configuration folder route>```

Run the node using a configuration file:

```./bn -c <configuration folder route>```


## Detailed documentation

* [Gitbook site](https://www.bitprim.org/)
* [Build manually from source](https://www.bitprim.org/installation.html)
* [Python API documentation](https://www.bitprim.org/python-interface/details.html)

## Changelog

* [0.8](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes.md#version-080)
* [0.7](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes.md#version-070)
* [0.6](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes.md#version-060)
* [0.5](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes-0.5.md)
* [0.4](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes-0.4.md)
* [Older](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes.md)


<!-- Links -->
[badge.Appveyor]: https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim-node-exe?svg=true&branch=dev
[badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg
[badge.Travis]: https://travis-ci.org/bitprim/bitprim-node-exe.svg?branch=master
[badge.version]: https://badge.fury.io/gh/bitprim%2Fbitprim-node-exe.svg


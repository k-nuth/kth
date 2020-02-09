<!-- <a target="_blank" href="http://semver.org">![Version][badge.version]</a> -->
<!-- <a target="_blank" href="https://cirrus-ci.com/github/k-nuth/node-exe">![Build Status][badge.Cirrus]</a> -->

# knuth <a target="_blank" href="https://github.com/k-nuth/node-exe/releases">![Github Releases][badge.release]</a> <a target="_blank" href="https://travis-ci.org/k-nuth/node-exe">![Build status][badge.Travis]</a> <a target="_blank" href="https://ci.appveyor.com/projects/k-nuth/node-exe">![Build Status][badge.Appveyor]</a> <a target="_blank" href="https://t.me/knuth_cash">![Telegram][badge.telegram]</a> <a target="_blank" href="https://k-nuth.slack.com/">![Slack][badge.slack]</a>

> High performance Bitcoin development platform.

Knuth is a high performance implementation of the Bitcoin protocol focused on users requiring extra performance and flexibility, what makes it the best platform for wallets, exchanges, block explorers and miners.

## Multiple cryptos

Knuth supports the following cryptocurrencies in the same code base:
- [BCH - Bitcoin Cash](https://www.bitcoincash.org/)
- [BTC - Bitcoin](https://bitcoin.org/)
- [LTC - Litecoin](https://litecoin.org/)

Choosing a cryptocurrency is just as simple as changing a switch in our build system, [take a look here](#getting-started).

Also, given its modular design and beautiful code, Knuth allows you to implement other cryptocurrencies with very few code changes.

## Not just a node

Knuth is a multi-crypto full node, but it is also a development platform.

Knuth's core is written in C++17, on top of that we provide a set of libraries and modules written in various programming languages that you can use as basis for building your application.

At the moment we have libraries in the following languages: [C++](https://github.com/k-nuth/node), [C](https://github.com/k-nuth/c-api), [C#](https://github.com/k-nuth/cs-api), [Python](https://github.com/k-nuth/py-api), [Javascript](https://github.com/k-nuth/js-api) and [Golang](https://github.com/k-nuth/go-api).
You can build your own library in the language of your choice on top of our [C library](https://github.com/k-nuth/c-api).

## Performance matters

We designed Knuth to be a high performance node, so our build system has the ability to automatically detect the microarchitecture of your processor and perform an optimized build for it.

For those who don't want to wait for compilation times, we provide pre-built binaries compatible with [Intel's Haswell microarchitecture](https://en.wikipedia.org/wiki/Haswell_(microarchitecture)). But you don't have to worry about that, our build system will do it for you.

## Modular architecture

Knuth is based on a modular architecture simple to modify, expand and learn.

Any protocol change can be introduced in Knuth much faster and more efficiently than in reference implementations.

## Cross-platform

Knuth can be used in any computer architecture and operating system, the only requirement is a 64-bit system.

Knuth has been well tested on x86-64 processors and on the following operating systems: FreeBSD, Linux, macOS and Windows. However, it is not limited to these, Knuth can be used in any computer architecture and any operating system, the only requirement is a 64-bit system.

If you find a problem in any other platform, please [let us know](https://github.com/k-nuth/kth/issues).

## Getting started

So, for any platform, an installation can be performed in 3 simple steps:

1. Install the Knuth build helper:
```
pip install kthbuild --user --upgrade
```

2. Configure the Conan remote:
```
conan remote add kth https://api.bintray.com/conan/k-nuth/kth
```

3. Install the appropriate executable:

```
# For Bitcoin Cash (default)
conan install kth/0.X@kth/stable -o currency=BCH

# For Bitcoin Core
conan install kth/0.X@kth/stable -o currency=BTC

# For Litecoin
conan install kth/0.X@kth/stable -o currency=LTC
```

## Building from source Requirements

In the case we don't have pre-built binaries for your plarform, it is necessary to build from the source code, so you need to add the following requirements to the previous ones:

- C++17 conforming compiler.
- [CMake](https://cmake.org/) building tool, version 3.8 or newer.

## Running the node

In order to run the full node you have to initialize the database and then run the node:

1. Run the following to initialize the database:

```./kth -i```

2. finally, run the node:

```./kth```


For more more detailed instructions, please refer to http://bitcoinverde.org/documentation/

## Roadmap

Our goal is to become a reliable implementation for use in mining and the one with the best APIs, so we need to:


- Implement the Bitcoin-Core and Bitcoin-ABC testing batteries, to ensure compatibility with them.

- Remove the limitation of 25 chained transactions.

- Implement a high performance SLP full indexer within the node.

- Improve libraries and APIs of languages.

- Create a high performance mining API (a high performance API should not be on top of HTTP).

- Improve our APIs documentation.

## Donation

## Documentation

We are working to improve the documentation, which is [located here](https://k-nuth.github.io/docs/).

## Issues

Each of our modules has its own Github repository. In case you want to create an issue, please do so in our [main repository](https://github.com/k-nuth/kth).

## Contact

You can contact us through our [Telegram](https://t.me/knuth_cash) and [Slack](https://k-nuth.slack.com/) groups or you can write to us at info@kth.cash.


<!-- Links -->
[badge.Travis]: https://travis-ci.org/k-nuth/node-exe.svg?branch=master
[badge.Appveyor]: https://ci.appveyor.com/api/projects/status/github/k-nuth/node-exe?svg=true&branch=master
[badge.Cirrus]: https://api.cirrus-ci.com/github/k-nuth/node-exe.svg?branch=master
[badge.version]: https://badge.fury.io/gh/k-nuth%2Fkth-node-exe.svg
[badge.release]: https://img.shields.io/github/release/k-nuth/node-exe.svg

[badge.telegram]: https://img.shields.io/badge/telegram-badge-blue.svg?logo=telegram
[badge.slack]: https://img.shields.io/badge/slack-badge-orange.svg?logo=slack

<!-- [badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg -->


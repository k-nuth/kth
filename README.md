<!-- <a target="_blank" href="http://semver.org">![Version][badge.version]</a> -->
<!-- <a target="_blank" href="https://cirrus-ci.com/github/k-nuth/node-exe">![Build Status][badge.Cirrus]</a> -->

# knuth <a target="_blank" href="https://github.com/k-nuth/node-exe/releases">![Github Releases][badge.release]</a> <a target="_blank" href="https://github.com/k-nuth/node-exe/actions">![Build status][badge.GithubActions]</a> <a target="_blank" href="https://ci.appveyor.com/projects/k-nuth/node-exe">![Build Status][badge.Appveyor]</a> <a href="#">![CPP][badge.cpp]</a> <a target="_blank" href="https://t.me/knuth_cash">![Telegram][badge.telegram]</a> <a target="_blank" href="https://k-nuth.slack.com/">![Slack][badge.slack]</a>

> High performance Bitcoin development platform.

Knuth is a high performance implementation of the Bitcoin protocol focused on users requiring extra performance and flexibility, what makes it the best platform for wallets, exchanges, block explorers and miners.

## Not just a node

Knuth is a multi-crypto full node, but it is also a development platform.

Knuth's core is written in C++17, on top of it we provide a set of libraries and modules written in various programming languages that you can use as basis for building your application.

At the moment we have libraries in the following languages: [C++](https://github.com/k-nuth/node), [C](https://github.com/k-nuth/c-api), [C#](https://github.com/k-nuth/cs-api), [Python](https://github.com/k-nuth/py-api), [Javascript](https://github.com/k-nuth/js-api) and [Golang](https://github.com/k-nuth/go-api).
You can build your own library in the language of your choice on top of our [C library](https://github.com/k-nuth/c-api).

## Performance matters

We designed Knuth to be a high performance node, so our build system has the ability to automatically detect the microarchitecture of your processor and perform an optimized build for it.

For those who don't want to wait for compilation times, we provide pre-built binaries compatible with [Intel's Haswell microarchitecture](https://en.wikipedia.org/wiki/Haswell_(microarchitecture)). But you don't have to worry about that, our build system will do everything for you.

## Modular architecture

Knuth is based on a modular architecture simple to modify, expand and learn.

Any protocol change can be introduced in Knuth much faster and more efficiently than in reference implementations.

## Cross-platform

Knuth can be used in any computer architecture and operating system, it only requires a 64-bit system.

Knuth has been well tested on x86-64 processors and on the following operating systems: FreeBSD, Linux, macOS and Windows. However, it is not limited to these, Knuth can be used in any computer architecture and any operating system, the only requirement is a 64-bit system.

If you find a problem in any other platform, please [let us know](https://github.com/k-nuth/kth/issues).

## Multiple cryptos

Knuth supports the following cryptocurrencies in the same code base:
- [Bitcoin Cash (BCH)](https://www.bitcoincash.org/)
- [Bitcoin (BTC)](https://bitcoin.org/)
- [Litecoin (LTC)](https://litecoin.org/)

Choosing a cryptocurrency is just as simple as changing a switch in our build system, [take a look here](#getting-started).

Also, given its modular design and beautiful code, Knuth allows you to implement other cryptocurrencies with very few code changes.

## Getting started

Install and run Knuth is very easy:

1. Install and configure the Knuth build helper:
```
$ pip install kthbuild --user --upgrade

$ conan config install https://github.com/k-nuth/ci-utils/raw/master/conan/config.zip

# Just for Linux
$ conan profile update settings.compiler.libcxx=libstdc++11 default
```

2. Install the appropriate node executable:

```
$ conan install kth/0.X@kth/stable --update
```

(`0.X` is an alias for our latest uploaded package)

3. Run the node:

```
$ ./kth
```
For more more detailed instructions, please refer to our [documentation](https://k-nuth.github.io/docs/).

## Donation

See [fund.kth.cash](https://fund.kth.cash/) for active Flipstarter campaigns.

Our general donation address is:
`bitcoincash:qrlgfg2qkj3na2x9k7frvcmv06ljx5xlnuuwx95zfn`

## License

Knuth node is released under the terms of the MIT license. See COPYING for more information or see https://opensource.org/licenses/MIT.

## Issues

Each of our modules has its own Github repository, but in case you want to create an issue, please do so in our [main repository](https://github.com/k-nuth/kth/issues).

## Contact

You can contact us through our [Telegram](https://t.me/knuth_cash) and [Slack](https://k-nuth.slack.com/) groups or write to us at info@kth.cash.

## Security Disclosures
To report security issues please contact:

Fernando Pelliccioni (fpelliccioni@gmail.com) - GPG Fingerprint: 8C1C 3163 AAE1 0EFA 704C 8A00 FE77 07B7 4C29 E389

<!-- Links -->
[badge.Travis]: https://travis-ci.org/k-nuth/node-exe.svg?branch=master
[badge.Appveyor]: https://ci.appveyor.com/api/projects/status/github/k-nuth/node-exe?svg=true&branch=master
[badge.Cirrus]: https://api.cirrus-ci.com/github/k-nuth/node-exe.svg?branch=master
[badge.GithubActions]: https://github.com/k-nuth/node-exe/workflows/Build%20and%20Test/badge.svg
[badge.version]: https://badge.fury.io/gh/k-nuth%2Fkth-node-exe.svg
[badge.release]: https://img.shields.io/github/release/k-nuth/node-exe.svg
[badge.cpp]: https://img.shields.io/badge/C++-17-blue.svg?style=flat&logo=c%2B%2B
[badge.telegram]: https://img.shields.io/badge/telegram-badge-blue.svg?logo=telegram
[badge.slack]: https://img.shields.io/badge/slack-badge-orange.svg?logo=slack

<!-- [badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg -->


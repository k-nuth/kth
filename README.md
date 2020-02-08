<!-- <a target="_blank" href="http://semver.org">![Version][badge.version]</a> -->
<!-- <a target="_blank" href="https://cirrus-ci.com/github/k-nuth/node-exe">![Build Status][badge.Cirrus]</a> -->

# knuth <a target="_blank" href="https://github.com/k-nuth/node-exe/releases">![Github Releases][badge.release]</a> <a target="_blank" href="https://travis-ci.org/k-nuth/node-exe">![Build status][badge.Travis]</a> <a target="_blank" href="https://ci.appveyor.com/projects/k-nuth/node-exe">![Build Status][badge.Appveyor]</a> <a target="_blank" href="https://t.me/knuth_cash">![Telegram][badge.telegram]</a> <a target="_blank" href="https://k-nuth.slack.com/">![Slack][badge.slack]</a>

> High performance Bitcoin development platform.

Knuth is a high performance implementation of the Bitcoin protocol focused on users requiring extra performance and flexibility, what makes it the best platform for wallets, exchanges, block explorers and miners.

## Multiple coins

Knuth supports the following cryptocurrencies in the same code base:
- [Bitcoin Cash](https://www.bitcoincash.org/)
- [Bitcoin](https://bitcoin.org/)
- [Litecoin](https://litecoin.org/)

Choosing a cryptocurrency is just as simple as changing a switch in our build system, [take a look here](TODO GETTING STARTING).

Also, given its modular design and beautiful code, Knuth allows you to implement other cryptocurrencies with very few code changes.


## Not just a node

Knuth is a multi-crypto full node, but it is also a development platform.

Knuth's core is written in C++17, but on top of that, we offer a set of libraries and modules written in various programming languages that serve as the basis for building your application.

At the moment we have libraries in the following languages: [C++](https://github.com/k-nuth/c-api), [C](https://github.com/k-nuth/node), [C#](https://github.com/k-nuth/cs-api), [Python](https://github.com/k-nuth/py-api), [Javascript](https://github.com/k-nuth/js-api), [Golang](https://github.com/k-nuth/go-api) and [Eiffel](https://github.com/k-nuth/c-api).
You can build your own library in the language of your choice on top of our [C library](https://github.com/k-nuth/c-api).







## Design principles
  c++ design principles zero overhead

  n to “not pay for what you don't use”?

## Cross-platform

Knuth can be used in any computer architecture and operating system, the only requirement is a 64-bit system.

Knuth has been well tested on x86-64 processors and on the following operating systems: FreeBSD, Linux, macOS and Windows; however, this is not limited to these, Knuth can be used in any computer architecture and operating system, the only requirement is a 64-bit system.

If you find a problem in any other platform, please [let us know](https://github.com/k-nuth/kth/issues).





## High performance node

We designed Knuth to be a high performance node, so our build system has the ability to automatically detect the microarchitecture of your processor and perform an optimized build for it.

For those who don't want to wait for compilation times, we provide pre-built binaries for the instruction set and extensions corresponding to [Intel Haswell](https://en.wikipedia.org/wiki/Haswell_(microarchitecture)).



## Build system

## Modular

Not based on Bitcoin Core, vs. ABC, vs. BU
codebase more modular and easy to maintain.
Our platform is based on a modular architecture simple to modify, expand and learn.






## Roadmap





## Documentation

In you want to tune the installation for better performance, please refer to the [documentation](https://k-nuth.github.io/docs/content/user_guide/advanced_installation.html).

## Changelog

* [0.1.0](https://github.com/k-nuth/kth/blob/master/doc/release-notes/release-notes.md#version-010)










*Knuth* allows you to run a full [Bitcoin Cash](https://www.bitcoincash.org/)/[Bitcoin](https://bitcoin.org/)/[Litecoin](https://litecoin.org/) node,
with all four main features:
  * Wallet
  * Mining
  * Full blockchain
  * Routing


*Knuth* allows you to run a full [Bitcoin Cash](https://www.bitcoincash.org/)/[Bitcoin](https://bitcoin.org/)/[Litecoin](https://litecoin.org/) node,
with all four main features:
  * Wallet
  * Mining
  * Full blockchain
  * Routing

*Knuth* also works as a cryptocurrency development platform with several programmable APIs:
  * C++
  * C
  * C#
  * Python
  * Javascript
  * Rust
  * Golang
  * Eiffel

... and networking APIs: 
  * insight: Our implementation of the [Bitpay insight-api](https://github.com/bitpay/insight-api)
  * JSON-RPC
  
# Documentation

For more detailed documentation, please refer to [Docs](https://k-nuth.github.io/docfx/index.html)

<!-- Links -->
[badge.Travis]: https://travis-ci.org/k-nuth/node-exe.svg?branch=master
[badge.Appveyor]: https://ci.appveyor.com/api/projects/status/github/k-nuth/node-exe?svg=true&branch=master
[badge.Cirrus]: https://api.cirrus-ci.com/github/k-nuth/node-exe.svg?branch=master
[badge.version]: https://badge.fury.io/gh/k-nuth%2Fkth-node-exe.svg
[badge.release]: https://img.shields.io/github/release/k-nuth/node-exe.svg

[badge.telegram]: https://img.shields.io/badge/telegram-badge-blue.svg?logo=telegram
[badge.slack]: https://img.shields.io/badge/slack-badge-orange.svg?logo=slack

<!-- [badge.Gitter]: https://img.shields.io/badge/gitter-join%20chat-blue.svg -->


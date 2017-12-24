[![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim) [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim?branch=master&svg=true)](https://ci.appveyor.com/project/bitprim/bitprim?branch=master) 

# Bitprim <a target="_blank" href="http://semver.org">![Version][badge.version]</a> <a target="_blank" href="https://travis-ci.org/bitprim/bitprim">![Travis status][badge.Travis]</a> <a target="_blank" href="https://ci.appveyor.com/project/bitprim/bitprim">![Appveyor status][badge.Appveyor]</a> <a target="_blank" href="https://gitter.im/bitprim/Lobby">![Gitter Chat][badge.Gitter]</a>

> Multi-Cryptocurrency full-node and development platform

Bitcoin, Bitcoin Cash and Litecoin development platform.
Bitprim allows you to run a full Bitcoin/Bitcoin Cash/Litecoin node,
with all four main features:
  * Wallet
  * Mining
  * Full blockchain
  * Routing

bitprim also works as a Bitcoin development platform: with its C interface,
bindings for many popular and friendlier languages can be built (some of which are available in this
site).

## Requirements

- 64-bit machine
- C++11 Compiler.
- [Conan](https://www.conan.io/) package manager. [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended). (This, in turn, requires Python and PIP)
- [CMake](https://cmake.org/) building tool, version 3.4 or newer.

## Installation

The Bitprim executables can be installed on Linux, macOS, FreeBSD, Windows and others. These binaries are pre-built for the most usual operating system/compiler combinations and hosted in an online repository. If there are no prebuilt binaries for your system, a build from source will be attempted.

So, for any system, a binary install can be performed in a terminal in 3 simple steps (assuming all requirements are already met):

```
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
# download https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
conan install .
```
 The 2nd step, downloading the conan file, is the only which may vary from system to system:
 
 Linux, macOS, FreeBSD, ...:  
  Using _wget_:
 
 ```
 wget -O conanfile.txt https://github.com/bitprim/bitprim/blob/v0.3/install/conanfile.txt
 ```
 
  Using _curl_:
 ```
 curl https://github.com/bitprim/bitprim/blob/v0.3/install/conanfile.txt -o conanfile.txt
 ```
 
 Windows:
 ```
 powershell -command "& {&'iwr' -outf conanfile.txt https://github.com/bitprim/bitprim/blob/v0.3/install/conanfile.txt}"
 ```

## Detailed documentation

* [Gitbook site](https://www.bitprim.org/)
* [Build manually from source](https://www.bitprim.org/installation.html)
* [Python API documentation](https://www.bitprim.org/python-interface/details.html)

## Changelog

* [0.3](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes-0.3.md)
* [0.2](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes-0.2.md)
* [0.1](https://github.com/bitprim/bitprim/blob/master/doc/release-notes/release-notes-0.1.md)

[![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim) [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim?branch=master&svg=true)](https://ci.appveyor.com/project/bitprim/bitprim?branch=master) 

# bitprim
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
- [Conan](https://www.conan.io/) package manager. [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended). (This, in turn, requires Python and PIP)
- C++11 Compiler.
- [CMake](https://cmake.org/) building tool, version 3.4 or newer.
- (Linux only) [m4 library](http://www.gnu.org/software/m4/m4.html)

## Installation

The bitprim binaries can be installed on Linux, Windows and OSX. These binaries are pre-built for the most
usual operating system-compiler combinations and hosted in an online repository, so a network connection
is needed to install bitprim. If there are no prebuilt binaries for your system, a build from source will be
attempted.

So, for any system, a binary install can be performed in a terminal in 3 simple steps (assuming all requirements are already met):

```
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
# download https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
conan install .
```
 The 2nd step, downloading the conan file, is the only which may vary from system to system:
 
 Linux
 ```
 wget -O conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
 ```
 
 OSX
 ```
 curl https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt -o conanfile.txt
 ```
 
 Windows:
 ```
 powershell -command "& {&'iwr' -outf conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt}"
 ```

## Detailed documentation

* [Gitbook site](https://www.bitprim.org/)
* [Build manually from source](https://www.bitprim.org/installation.html)
* [Python API documentation](https://www.bitprim.org/python-interface/details.html)
[![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim) [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim?branch=master&svg=true)](https://ci.appveyor.com/project/bitprim/bitprim?branch=master) 

# bitprim
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
- [Conan](https://www.conan.io/) package manager. [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended). (This, in turn, requires Python and PIP)
- C++11 Compiler.
- [CMake](https://cmake.org/) building tool, version 3.4 or newer.
- (Linux only) [m4 library](http://www.gnu.org/software/m4/m4.html)

## Installation

The bitprim binaries can be installed on Linux, Windows and OSX. These binaries are pre-built for the most
usual operating system-compiler combinations and hosted in an online repository, so a network connection
is needed to install bitprim. If there are no prebuilt binaries for your system, a build from source will be
attempted.

So, for any system, a binary install can be performed in a terminal in 3 simple steps (assuming all requirements are already met):

```
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
# download https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
conan install .
```
 The 2nd step, downloading the conan file, is the only which may vary from system to system:
 
 Linux
 ```
 wget -O conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
 ```
 
 OSX
 ```
 curl https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt -o conanfile.txt
 ```
 
 Windows:
 ```
 powershell -command "& {&'iwr' -outf conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt}"
 ```

## Detailed documentation

* [Gitbook site](https://www.bitprim.org/)
* [Build manually from source](https://www.bitprim.org/installation.html)
* [Python API documentation](https://www.bitprim.org/python-interface/details.html)

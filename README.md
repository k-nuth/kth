[![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim) [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim?branch=master&svg=true)](https://ci.appveyor.com/project/bitprim/bitprim?branch=master) 

<!-- [![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim)  -->

# bitprim
Bitcoin, Bitcoin Cash and Litecoin development platform

Table of Contents
=================

   * [bitprim](#bitprim)
      * [Requirements:](#requirements)
      * [Installation:](#installation)
        * [Using conan:](#using-conan)
        * [Automatic script for linux:](#automatic-installation-using-script-for-linux)
        * [Build from source:](#build-from-source)
      * [Docker Image](#docker-image)
      * [Reference documentation](#reference-documentation)

## Requirements:

- 64-bit machine
- [Conan](https://www.conan.io/) package manager. [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended). (This, in turn, requires Python and PIP)
- C++11 Compiler.
- [CMake](https://cmake.org/) building tool.

## Installation:

### Using conan:
Using the conan method bitprim can be installed in Linux, OSX and Windows.

Bitprim can be installed following these three steps:
  * Add the `bitprim` remote to conan: ```conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim```
  * Download the conanfile.txt located [here](https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt)
  * Run `conan install .`

**For example (linux):**
```sh
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
wget -O conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
conan install .
```

**Now you have Bitprim in its two variants:**
  * As a development platform:  
    * *_Headers files_:* `./bitprim/include`  
    * *_Compiled libraries_:* `./bitprim/lib`
  * As a ready-to-use Bitcoin (/Cash/Litecoin) full node:
    ```sh
    $ ./bitprim/bin/bn -i # for directories initialization 
    $ ./bitprim/bin/bn    # for starting the node
    ```

### Automatic Installation using script for Linux:
*Tested on Ubuntu 16.04/17.04 and Fedora 26*

This script will automatically try to install `cmake`, `pip` and `conan`. Then it will use conan to install Bitprim (files will be installed in a bitprim directory created under the current working directory)

```sh
wget -qO- https://raw.githubusercontent.com/bitprim/bitprim/master/install/install_bitprim.sh | bash
```

### Build from source:
*Conan is used to download `boost`, `gmp`, `zlib` and `bzip2` to avoid conflict between versions*

After installing all the [requirements](#requirements), clone the repository, use `conan` to install external dependencies such as `libboost` and compile using `cmake`.

**For example (linux):**
```sh
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
git clone --recursive https://github.com/bitprim/bitprim
cd bitprim
mkdir build
cd build
conan install ..
cmake ..
make -j4
```

## Docker Image
For your convenience a Docker image is provided use ```docker pull bitprim/bitprim``` to pull it, the DockerFile used to build the image can be found [here](https://github.com/bitprim/bitprim/blob/master/install/Dockerfile)

## Reference documentation ##
For more detailed documentation, please refer to https://www.bitprim.org/

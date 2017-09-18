[![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim) [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim?branch=master&svg=true)](https://ci.appveyor.com/project/bitprim/bitprim?branch=master) 

<!-- [![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim)  -->

# bitprim
Bitcoin, Bitcoin Cash and Litecoin development platform


Table of Contents
=================

   * [bitprim](#bitprim)
      * [Requirements:](#requirements)
         * [CMake Installation](#cmake-installation)
      * [Automatic Installation using script for Linux (tested on Ubuntu 16.04/17.04 and Fedora 26)](#automatic-installation-using-script-for-linux-tested-on-ubuntu-16041704-and-fedora-26)
         * [Manual Installation with Conan:](#manual-installation-with-conan)
      * [Build from source](#build-from-source)
         * [Dependencies](#dependencies)
         * [Simplified Bitprim Build using script](#simplified-bitprim-build-using-script)
         * [Build bitprim manually](#build-bitprim-manually)
      * [Docker Image](#docker-image)
      * [Reference documentation](#reference-documentation)


## Requirements:

- 64-bit machine Linux/Windows Machine/VM/instance which is able tu run the rest of the requirements
- [Conan](https://www.conan.io/) package manager. [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended). (This, in turn, requires Python and PIP)
- C++11 Compiler.
- [CMake](https://cmake.org/) building tool. [Cmake Installation](#cmake).

### [CMake](https://cmake.org/) Installation
Cmake 3.8+ is required for all builds/installs, not all OS versions include this version, so check your version with ```cmake -version``` and install from source if required using the following instructions: 
```sh
wget https://cmake.org/files/v3.9/cmake-3.9.0.tar.gz
tar -xvzf cmake-3.9.0.tar.gz
cd cmake-3.9.0
./bootstrap
make -j 4
sudo make install
sudo ln -s /usr/local/bin/cmake /usr/bin/cmake
```


## Automatic Installation using script for Linux (tested on Ubuntu 16.04/17.04 and Fedora 26)

This script will automatically try to install cmake/pip/conan and then use conan to download/compile/install Bitprim (files will be installed in a bitprim directory created under the current working directory)

```wget -qO- https://raw.githubusercontent.com/bitprim/bitprim/master/install/install_bitprim.sh | bash```


### Manual Installation with Conan:
```sh
conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
wget -O conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
conan install .
```
<b>Now you have Bitprim in its two variants:</b>
- As a development platform:  
    _Headers files_: `./bitprim/include`  
    _Compiled libraries_: `./bitprim/lib`
- As a ready-to-use Bitcoin (/Cash/Litecoin) full node:
    ```sh
    $ ./bitprim/bin/bn -i # for directories initialization 
    $ ./bitprim/bin/bn    # for starting the node
    ```


## Build from source

### Dependencies

Bitprim requires a C++11 compiler, currently minimum [GCC 4.8.0](https://gcc.gnu.org/projects/cxx0x.html)

To check your GCC version:
```sh
 g++ --version
```
```
g++ (Ubuntu 4.8.2-19ubuntu1) 4.8.2
Copyright (C) 2013 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```
If necessary, upgrade your compiler as follows (these instructions are for Ubuntu):
```sh
sudo apt-get install g++-4.8
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 50
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 50
sudo update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-4.8 50
```

Next, install the [build system](http://wikipedia.org/wiki/GNU_build_system) (Automake minimum 1.14) and tools needed to continue:
```sh
sudo apt-get install build-essential autoconf automake libtool pkg-config git screen curl make g++ unzip
```

### Simplified Bitprim Build using script
1) Download the script 
```sh
wget https://raw.githubusercontent.com/bitprim/bitprim/master/install.sh
chmod +x install.sh
```
2) Run the script install.sh, to get the dependencies (minimun: `Cmake 3.9`  and `Boost 1.64` built with `fPIC` flag) and build bitprim.

The destination folder MUST be set using the option `prefix` (--prefix=/path/to/dest).
The amount of CPU cores to be used by the `make -j cores` command can be set using the option `cores` (--cores=4). The cores option is NOT MANDATORY, and its default value is 4.

Cmake and boost will not be installed in your system directories to avoid conflict with other versions that may be installed there.

```sh
./install.sh --prefix=/home/dev/bitprim --cores=8
```
If the user running the script does not have full access to the folder, the script can be run as administrator using `sudo ./install.sh --prefix=/path/to/dest`

The script creates a deps folder in the location `/path/to/dest/deps` where boost and cmake will be located. It also creates the folder `/path/to/dest/bitprim` where the project will be cloned and built.

3) To upgrade to the latest `bitprim` version, just re-run the `./install.sh --prefix=/path/to/dest` script using the same options; the script won't re-download and build the dependencies, it will just pull the changes and rebuild bitprim.


### Build bitprim manually

1) Install Cmake 3.9 and Boost 1.64 manually

Install [CMake](https://cmake.org/) from sources:
```sh
wget https://cmake.org/files/v3.9/cmake-3.9.0.tar.gz
tar -xvzf cmake-3.9.0.tar.gz
cd cmake-3.9.0
./bootstrap
make -j 4
sudo make install
sudo ln -s /usr/local/bin/cmake /usr/bin/cmake
cd ..
```

Next, install the [Boost](http://www.boost.org) (minimum 1.64.0 with fPIC flag) development package:
```sh
wget 'https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.gz'
tar -xvzf boost_1_64_0.tar.gz
cd boost_1_64_0
./bootstrap.sh
sudo ./b2  cxxflags=-fPIC cflags=-fPIC -j4 install
```

2) Clone the bitprim repository with the `--recursive` option, and build it
```sh
git clone --recursive https://github.com/bitprim/bitprim/
cd bitprim
mkdir build 
cd build
cmake ..
make -j4
```

## Docker Image
For your convenience a Docker image is provided use ```docker pull bitprim/bitprim``` to pull it, the DockerFile used to build the image can be found [here](https://github.com/bitprim/bitprim/blob/master/install/Dockerfile)

## Reference documentation ##

For more detailed documentation, please refer to https://www.bitprim.org/

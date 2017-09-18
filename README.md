[![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim) [![Appveyor Status](https://ci.appveyor.com/api/projects/status/github/bitprim/bitprim?branch=master&svg=true)](https://ci.appveyor.com/project/bitprim/bitprim?branch=master) 

<!-- [![Build Status](https://travis-ci.org/bitprim/bitprim.svg?branch=master)](https://travis-ci.org/bitprim/bitprim)  -->

# bitprim
Bitcoin, Bitcoin Cash and Litecoin development platform

## Install using Conan (recommended way)

#### Requirements:

- 64-bit machine.
- [Conan](https://www.conan.io/) package manager. [Conan Installation](http://docs.conan.io/en/latest/installation.html#install-with-pip-recommended).
- C++11 Compiler.
- [CMake](https://cmake.org/) building tool. [Cmake Installation](#cmake).

#### Installation:

```sh
$ conan remote add bitprim https://api.bintray.com/conan/bitprim/bitprim
$ wget -O conanfile.txt https://raw.githubusercontent.com/bitprim/bitprim/master/install/conanfile.txt
$ conan install .
```


<a name="cmake"></a>
## [CMake](https://cmake.org/) Installation

Install [CMake](https://cmake.org/) from sources:
```sh
$ wget https://cmake.org/files/v3.9/cmake-3.9.0.tar.gz
$ tar -xvzf cmake-3.9.0.tar.gz
$ cd cmake-3.9.0
$ ./bootstrap
$ make -j 4
$ sudo make install
$ sudo ln -s /usr/local/bin/cmake /usr/bin/cmake
```


## Build from source

### Debian/Ubuntu
#### Dependencies

Bitprim requires a C++11 compiler, currently minimum [GCC 4.8.0](https://gcc.gnu.org/projects/cxx0x.html)

To see your GCC version:
```sh
$ g++ --version
```
```
g++ (Ubuntu 4.8.2-19ubuntu1) 4.8.2
Copyright (C) 2013 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```
If necessary, upgrade your compiler as follows:
```sh
$ sudo apt-get install g++-4.8
$ sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 50
$ sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 50
$ sudo update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-4.8 50
```

Next install the [build system](http://wikipedia.org/wiki/GNU_build_system) (Automake minimum 1.14) and tools needed to continue:
```sh
$ sudo apt-get install build-essential autoconf automake libtool pkg-config git screen curl make g++ unzip
```

#### Build bitprim using install.sh script
1) Download the script 
```sh
$ wget https://raw.githubusercontent.com/bitprim/bitprim/master/install.sh
$ chmod +x install.sh
```
2) Run the script install.sh, to get the dependencies (minimun: `Cmake 3.9`  and `Boost 1.64` built with `fPIC` flag) and build bitprim.

The destination folder MUST be set using the option `prefix` (--prefix=/path/to/dest).
The cores amount to be used by the `make -j cores` can be set using the option `cores` (--cores=4). Cores option is OPTIONAL and the default value is 4.

Cmake and boost will not be installed in your system to avoid conflict with other versions that may be installed in the system.

```sh
$ ./install.sh --prefix=/home/dev/bitprim --cores=8
```
If the user running the script does not have full access to the folder, the script can be run as administrator using `sudo ./install.sh --prefix=/path/to/dest`

The script creates a deps folder in the location `/path/to/dest/deps` where boost and cmake will be located. It also creates the folder `/path/to/dest/bitprim` where the project will be cloned and built.

3) To upgrade to the `bitprim` version, just re-run the `./install.sh --prefix=/path/to/dest` script using the same options, the script won't re-download and build the dependencies, it will just pull the changes and rebuild bitprim.


#### Build bitprim manually

1) Install Cmake 3.9 and Boost 1.64 manually

Install [CMake](https://cmake.org/) from sources:
```sh
$ wget https://cmake.org/files/v3.9/cmake-3.9.0.tar.gz
$ tar -xvzf cmake-3.9.0.tar.gz
$ cd cmake-3.9.0
$ ./bootstrap
$ make -j 4
$ sudo make install
$ sudo ln -s /usr/local/bin/cmake /usr/bin/cmake
$ cd ..
```

Next, install the [Boost](http://www.boost.org) (minimum 1.64.0 with fPIC flag) development package:
```sh
$ wget 'https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.gz'
$ tar -xvzf boost_1_64_0.tar.gz
$ cd boost_1_64_0
$ ./bootstrap.sh
$ sudo ./b2  cxxflags=-fPIC cflags=-fPIC -j4 install
```

2) Clone recursive the bitprim repository and build it
```sh
$ git clone --recursive https://github.com/bitprim/bitprim/
$ cd bitprim
$ mkdir build 
$ cd build
$ cmake ..
$ make -j4
```

## Reference documentation ##

For more detailed documentation, please refer to https://www.bitprim.org/

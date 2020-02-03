#!/bin/bash
root_folder=""
cores=4
##GETOPTS
while test $# -gt 0; do
  case "$1" in
    --prefix*)
      root_folder=`echo $1 | sed -e 's/^[^=]*=//g'`
      shift
      ;;
  --cores*)
    cores=`echo $1 | sed -e 's/^[^=]*=//g'`
    shift
    ;;
  *)
    break
    ;;
  esac
done
# Validate that the root folder was set
if [ "$root_folder" == "" ]; then
  echo "no prefix specified"
  exit 1
fi

deps_folder="$root_folder/deps"
if [ ! -d "$root_folder" ]; then
  mkdir $root_folder
fi
if [ ! -d "$deps_folder" ]; then
  mkdir $deps_folder
fi

boost_root="$deps_folder/boost_1_64_0"
cmake_bin="$deps_folder/cmake-3.9.0-rc6/bin"

function build_boost {
  cd $deps_folder
  if [ -d "$deps_folder/boost_1_64_0" ]; then
    echo "--->Boost already installed."
  else
  	echo "--->Downloading and building boost..."
	  wget 'https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.gz'
	  tar -xvzf boost_1_64_0.tar.gz
	  cd boost_1_64_0
	  ./bootstrap.sh
	  ./b2  cxxflags=-fPIC cflags=-fPIC -j$cores
	  cd ..
	  rm -f boost_1_64_0.tar.gz
  fi
}

function build_cmake {
  cd $deps_folder
  if [ -d "$deps_folder/cmake-3.9.0-rc6" ]; then
    echo "--->Cmake already installed."
  else
    echo "--->Cmake not found, installing"
    wget https://cmake.org/files/v3.9/cmake-3.9.0-rc6.tar.gz
    tar -xvzf cmake-3.9.0-rc6.tar.gz
    cd cmake-3.9.0-rc6
    ./bootstrap
    make -j$cores
    cd ..
    rm -r cmake-3.9.0-rc6.tar.gz
  fi
}

function build_bitprim {
	cd $root_folder
  if [ -d "$root_folder/bitprim" ]; then
    echo "--->Upgrading bitprim"
    cd bitprim
    git pull
    cd build
    $cmake_bin/cmake .. -DBOOST_ROOT="$boost_root"
    make -j$cores
  else
    git clone --recursive https://github.com/k-nuth/kth.git
    cd bitprim
	  mkdir build
	  cd build
  	$cmake_bin/cmake .. -DBOOST_ROOT="$boost_root"
	  make -j$cores
  fi
}

build_boost
build_cmake
build_bitprim
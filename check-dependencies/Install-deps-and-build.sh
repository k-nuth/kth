#!/bin/bash
#to use ./clone.sh ~/112bit/nuevo/
#destination folder must exist

declare -a BOOST_CHECK_RESULTS=("Success" "Headers not found" "Runtime error in header test" "Libraries not found" "Runtime error in library test")

function install_boost
{
	echo "--->Downloading and installing boost..."
	#Download tarball
	wget 'https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.gz'
	#Unzip
	tar -xvzf boost_1_64_0.tar.gz
	#Build with -fPIC flag
	cd boost_1_64_0
	./bootstrap.sh
	echo "Need admin permission to install Boost"
	sudo ./b2  cxxflags=-fPIC cflags=-fPIC install
	cd ..
	rm -rf boost_1_64_0.tar.gz
	rm -rf boost_1_64_0
	sudo ldconfig
}

function install_cmake
{
	echo "--->Cmake not found, installing"
	wget https://cmake.org/files/v3.7/cmake-3.7.0-rc1.tar.gz
	tar -xvzf cmake-3.7.0-rc1.tar.gz
	cd cmake-3.7.0-rc1
	./bootstrap
	make
	sudo make install
	sudo ln -s /usr/local/bin/cmake /usr/bin/cmake
	cd ..
	rm -rf cmake-3.7.0-rc1.tar.gz
	rm -rf cmake-3.7.0-rc1
}

function test_boost_headers
{
	tmpfile=$(mktemp "/tmp/boost-htest.XXXXXX.cpp")

	echo '#include<iostream>
#include<boost/any.hpp>
#include <boost/version.hpp>
#include <stdexcept>

int main()
{
	try
	{
    	boost::any a(5);
    	a = 7.67;
    	double d = boost::any_cast<double>(a);
    	std::cout << "Boost headers found! Boost version: " << BOOST_LIB_VERSION << std::endl;
    	return 0;
	}catch(std::exception e){
		return -1;
	}
}' >> "$tmpfile"
	test_exec=/tmp/boost_header_only_test
	#if ! g++ "$tmpfile" -o "$test_exec" >&/dev/null; then
	if ! g++ "$tmpfile" -o "$test_exec"; then
		return 1
	fi

	if ! "$test_exec"; then
		rm -rf "$test_exec"
		return 2
	fi
	rm -rf "$test_exec"
	rm "$tmpfile"
	return 0
}

function test_boost_libraries
{
	tmpfile=$(mktemp "/tmp/boost-ltest.XXXXXX.cpp")

	echo '#include<iostream>
#include<boost/filesystem/operations.hpp>
#include <stdexcept>

namespace bfs=boost::filesystem;

int main()
{
	try
	{
	    bfs::path p("clone_build_cint.sh");
	    if(bfs::exists(p))
	    {
	    	std::cout<< "Boost libraries found!" <<std::endl;
	        return 0;
	    }
	    return -1;
	}catch(std::exception)
	{
		return -2;
	}
}' >> "$tmpfile"
	#Link boost libraries against an executable, and run it
	test_exec=/tmp/boost_library_test
	if ! g++ "$tmpfile" -o "$test_exec" -lboost_filesystem -lboost_system >&/dev/null; then
		return 1
	fi

	if ! "$test_exec"; then
		rm -rf "$test_exec"
		return 2
	fi
	rm -rf "$test_exec"
	rm "$tmpfile"
	return 0
}

function test_boost_fpic
{
	tmpfile=$(mktemp "/tmp/boost-ftest.XXXXXX.cpp")

	echo '#include<iostream>
#include<boost/filesystem/operations.hpp>
#include <stdexcept>

namespace bfs=boost::filesystem;

class lib_class{
public:
	void foo(){
	    bfs::path p("clone_build_cint.sh");
	    if(!bfs::exists(p))
	    {
	    	throw std::runtime_error("Error in boost filesystem lib");
	    }
	}
};' >> "$tmpfile"

	g++ -g -ggdb -c "$tmpfile" -o /tmp/boost_ftest.o
}

function check_if_boost_installed
{
	if ! test_boost_headers; then
		echo ${BOOST_CHECK_RESULTS[1]}
		return 1
	fi

	if ! test_boost_libraries; then
		echo ${BOOST_CHECK_RESULTS[2]}
		return 2
	fi

	if ! test_boost_fpic; then
		echo ${BOOST_CHECK_RESULTS[3]}
		return 3
	fi

	return 0
}

function check_dependencies
{
	if ! check_if_boost_installed; then
		install_boost
	fi

	if ! git --help >&/dev/null; then
		echo "--->Git not found, installing..."
		sudo apt-get install git
	fi

	if ! cmake --help&>/dev/null; then
		install_cmake
	fi
}


function build_project
{
	cd ..
	mkdir build
	cd build
  	cmake .. 
	make -j8
}

function build_bitprim
{
    check_dependencies
    build_project
}

build_bitprim


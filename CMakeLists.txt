# Copyright (c) 2016-2022 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.8)

# kth
#==============================================================================
project(kth
  VERSION 0.0.0
  LANGUAGES CXX C)

set(CMAKE_VERBOSE_MAKEFILE ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CURRENCY "BCH" CACHE STRING "Specify the Cryptocurrency (BCH|BTC|LTC).")
if (${CURRENCY} STREQUAL "BCH")
    add_definitions(-DKTH_CURRENCY_BCH)
elseif (${CURRENCY} STREQUAL "BTC")
    add_definitions(-DKTH_CURRENCY_BTC)
elseif (${CURRENCY} STREQUAL "LTC")
    add_definitions(-DKTH_CURRENCY_LTC)
else()
    message(FATAL_ERROR "Invalid Cryptocurrency: ${CURRENCY}")
endif()

message(STATUS "Knuth: Cryptocurrency: ${CURRENCY}")
# Check for baseline language coverage in the compiler for the C++17 standard.
#------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
# ---------------------------------------------------------------

option(BUILD_NODE_EXE "Build the node executable." ON)
option(BUILD_C_API "Build the C-API." OFF)

option(USE_CONAN "Use Conan Build Tool." OFF)
option(NO_CONAN_AT_ALL "Conan totally disabled." OFF)

option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_CONSOLE "Compile console application." OFF)
set(WITH_BIGNUM "no" CACHE STRING "Specify Bignum Implementation (gmp|no|auto).")

set(USE_CPP11_ABI ON)
set(ENABLE_POSITION_INDEPENDENT_CODE ON)
set(USE_CONAN OFF)
set(NO_CONAN_AT_ALL OFF)

set(WITH_CONSOLE OFF)

if (NO_CONAN_AT_ALL)
  set(USE_CONAN OFF)
endif()

# ---------------------------------------------------------------

option(WITH_RPC "Compile with RPC-ZMQ support." OFF)
option(WITH_LOCAL_MINING "Compile with local mining." ON)
option(ENABLE_TEST "Compile with enable test." OFF)
option(ENABLE_TESTS "Compile with enable test." OFF)
option(ENABLE_MODULE_RECOVERY "Compile with enable module recovery." ON)
option(WITH_TOOLS "Compile with tools." OFF)
option(WITH_CONSOLE_NODE_CINT "" OFF)

# Implement --with-tests and declare WITH_TESTS.
#------------------------------------------------------------------------------
option(WITH_TESTS "Compile with unit tests." OFF)

# Implement --with-examples and declare WITH_EXAMPLES.
#------------------------------------------------------------------------------
option(WITH_EXAMPLES "Compile with examples." OFF)

# Inherit --enable-shared and define BOOST_TEST_DYN_LINK.
#------------------------------------------------------------------------------
option(ENABLE_SHARED "" OFF)
option(ENABLE_SHARED_NODE_CINT "" ON)

set(WITH_LOCAL_MINING OFF)
set(ENABLE_TEST OFF)
set(ENABLE_TESTS OFF)
set(ENABLE_MODULE_RECOVERY ON)
set(WITH_TOOLS OFF)
set(WITH_TESTS OFF)
set(WITH_EXAMPLES OFF)

set(ENABLE_SHARED OFF)
set(ENABLE_SHARED_NODE_CINT ON)

if (WITH_RPC)
    add_definitions(-DKTH_WITH_RPC)
endif()

if (WITH_LOCAL_MINING)
	add_definitions(-DWITH_LOCAL_MINING)
endif()

# Knuth tools
#------------------------------------------------------------------------------
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(KnuthTools)

# Build
#==============================================================================
if (NOT DEFINED ENABLE_MODULE_RECOVERY)
  set(ENABLE_MODULE_RECOVERY ON CACHE BOOL "Enable ECDSA pubkey recovery module.")
endif()

add_subdirectory(secp256k1)
add_subdirectory(infrastructure)
add_subdirectory(domain)
add_subdirectory(consensus)
add_subdirectory(network)
add_subdirectory(database)
add_subdirectory(blockchain)
add_subdirectory(node)
# add_subdirectory(rpc)

if (BUILD_NODE_EXE)
    add_subdirectory(node-exe)
endif()

if (BUILD_C_API)
    add_subdirectory(c-api)
endif()


# Tests
#==============================================================================
if (WITH_TESTS)
  enable_testing()
endif()

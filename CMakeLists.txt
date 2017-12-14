#
# Copyright (c) 2017 Bitprim developers (see AUTHORS)
#
# This file is part of Bitprim.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.4)

# bitprim
#==============================================================================
project(bitprim
  VERSION 3.0.0
  LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Check for baseline language coverage in the compiler for the C++11 standard.
#------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ---------------------------------------------------------------
#TODO!
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)



option(USE_CPP11_ABI "" ON)

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

option(WITH_LITECOIN "Compile with Litecoin support." OFF)
option(WITH_REMOTE_BLOCKCHAIN "Compile with remote bitprim-blockchain (aka. bitprim-blockchain-requester)." OFF)
option(WITH_REMOTE_DATABASE "Compile with remote bitprim-database (aka. bitprim-database-requester)." OFF)
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

set(WITH_REMOTE_DATABASE OFF)
set(WITH_LOCAL_MINING OFF)
set(ENABLE_TEST OFF)
set(ENABLE_TESTS OFF)
set(ENABLE_MODULE_RECOVERY ON)
set(WITH_TOOLS OFF)
set(WITH_REMOTE_BLOCKCHAIN OFF)
set(WITH_TESTS OFF)
set(WITH_EXAMPLES OFF)

set(ENABLE_SHARED OFF)
set(ENABLE_SHARED_NODE_CINT ON)

set(WITH_RPC ON)
if (WITH_RPC)
    add_definitions(-DBITPRIM_WITH_RPC)
endif()

if (WITH_LOCAL_MINING)
	add_definitions(-DWITH_LOCAL_MINING)
endif()

if (WITH_LITECOIN)
	add_definitions(-DLITECOIN)
endif()

# # Check dependencies.
# #==============================================================================
# ## Require Boost of at least version 1.56.0 and output ${boost_CPPFLAGS/LDFLAGS}.
# # Require Boost of at least version 1.46.0 and output ${boost_CPPFLAGS/LDFLAGS}.
# #------------------------------------------------------------------------------
# if (NOT ENABLE_SHARED)
#   set(Boost_USE_STATIC_LIBS ON)
# endif()

# find_package(
#   Boost 1.56.0 REQUIRED
#   COMPONENTS chrono date_time filesystem iostreams locale log program_options regex system thread unit_test_framework)

# if (NOT Boost_FOUND)
#   message(FATAL_ERROR "Could not find Boost 1.56.0 or newer" )
# endif()


# bla bla bla
#------------------------------------------------------------------------------
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(BitprimTools)


# Build
#==============================================================================
if (NOT DEFINED ENABLE_MODULE_RECOVERY)
  set(ENABLE_MODULE_RECOVERY ON CACHE BOOL "Enable ECDSA pubkey recovery module.")
endif()

add_subdirectory(secp256k1)
add_subdirectory(bitprim-core)
add_subdirectory(bitprim-consensus)
add_subdirectory(bitprim-network)
add_subdirectory(bitprim-database)
add_subdirectory(bitprim-blockchain)
add_subdirectory(bitprim-rpc)
add_subdirectory(bitprim-node)
add_subdirectory(bitprim-node-exe)
add_subdirectory(bitprim-node-cint)

# Tests
#==============================================================================
if (WITH_TESTS)
  enable_testing()
endif()

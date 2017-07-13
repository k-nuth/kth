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
set(WITH_LOCAL_MINING ON)
set(ENABLE_TEST OFF)
set(ENABLE_TESTS OFF)
set(ENABLE_MODULE_RECOVERY ON)
set(WITH_TOOLS OFF)
set(WITH_REMOTE_BLOCKCHAIN OFF)
set(WITH_TESTS OFF)
set(WITH_EXAMPLES OFF)

set(ENABLE_SHARED OFF)
set(ENABLE_SHARED_NODE_CINT ON)


if (WITH_LOCAL_MINING)
	add_definitions(-DWITH_LOCAL_MINING)
endif()

if (WITH_LITECOIN)
	add_definitions(-DLITECOIN)
endif()

# Check dependencies.
#==============================================================================
## Require Boost of at least version 1.56.0 and output ${boost_CPPFLAGS/LDFLAGS}.
# Require Boost of at least version 1.46.0 and output ${boost_CPPFLAGS/LDFLAGS}.
#------------------------------------------------------------------------------
if (NOT ENABLE_SHARED)
  set(Boost_USE_STATIC_LIBS ON)
endif()

find_package(
  Boost 1.56.0 REQUIRED
  COMPONENTS chrono date_time filesystem iostreams locale log program_options regex system thread unit_test_framework)

if (NOT Boost_FOUND)
  message(FATAL_ERROR "Could not find Boost 1.56.0 or newer" )
endif()

# Require zmq of at least version 4.2.0 and output ${zmq_CPPFLAGS/LIBS/PKG}.
#------------------------------------------------------------------------------
#set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/bitprim-protocol/cmake)

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
add_subdirectory(bitprim-node)
add_subdirectory(bitprim-node-cint)

# Tests
#==============================================================================
if (WITH_TESTS)
  enable_testing()
endif()

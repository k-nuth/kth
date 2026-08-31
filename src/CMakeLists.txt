# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.15)

# secp256k1 (modernized upstream libsecp256k1 + the BCH Schnorr module). Configure
# its options before adding it, since the vendored tree is kept pristine. Static
# only, no upstream tests/benches, and the modules Knuth needs: ECDSA recovery and
# the BCH Schnorr module for consensus, plus ECDH / schnorrsig / ellswift for the
# Stratum V2 Noise transport.
set(SECP256K1_DISABLE_SHARED ON CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORR_BCH ON CACHE BOOL "" FORCE)

add_subdirectory(secp256k1)

# Knuth links this alias; upstream exports the bare `secp256k1` target.
if(NOT TARGET secp256k1::secp256k1)
    add_library(secp256k1::secp256k1 ALIAS secp256k1)
endif()

# Put secp256k1 in an installed export set (as the previous vendored build did),
# so the modules that link it (infrastructure, ...) can export their own targets.
# Upstream's own install is disabled (SECP256K1_INSTALL OFF); this is Knuth-side.
install(TARGETS secp256k1
    EXPORT secp256k1-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(EXPORT secp256k1-targets
    FILE secp256k1-targets.cmake
    NAMESPACE secp256k1::
    DESTINATION lib/cmake/secp256k1
)
add_subdirectory(infrastructure)
add_subdirectory(domain)

if (WITH_CONSENSUS)
    add_subdirectory(consensus)
endif()

# Network module is not compatible with Emscripten
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_subdirectory(network)
endif()

add_subdirectory(database)
add_subdirectory(blockchain)
add_subdirectory(node)

# Node executable is not compatible with Emscripten
if (BUILD_NODE_EXE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_subdirectory(node-exe)
endif()

if (BUILD_C_API)
    add_subdirectory(c-api)
endif()
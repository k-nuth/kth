# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.22)

# secp256k1 (upstream libsecp256k1 plus the local BCH Schnorr module). Configure
# its options here, before adding it, rather than in the vendored build files.
# Static only, no benches, and the modules Knuth needs: ECDSA recovery and the BCH
# Schnorr module for consensus, plus ECDH / schnorrsig / ellswift for the Stratum
# V2 Noise transport. The upstream unit and exhaustive tests follow ENABLE_TEST
# and are registered individually with CTest; their binaries are
# src/secp256k1/bin/*.
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS ${ENABLE_TEST} CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS ${ENABLE_TEST} CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SILENTPAYMENTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORR_BCH ON CACHE BOOL "" FORCE)

# Keep the vendored library static without changing a parent's shared-library
# policy. Upstream no longer provides SECP256K1_DISABLE_SHARED.
function(kth_add_secp256k1)
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(secp256k1)
endfunction()
kth_add_secp256k1()

# Knuth links this alias; upstream exports the bare `secp256k1` target.
if(NOT TARGET secp256k1::secp256k1)
    add_library(secp256k1::secp256k1 ALIAS secp256k1)
endif()

# Upstream installs this usage requirement only when its own install is enabled.
target_include_directories(secp256k1 INTERFACE $<INSTALL_INTERFACE:include>)

# Put secp256k1 in an installed export set (as the previous vendored build did),
# so the modules that link it (infrastructure, ...) can export their own targets.
# Upstream's own install is disabled (SECP256K1_INSTALL OFF); this is Knuth-side,
# including the public headers of the enabled modules, which package consumers
# include directly (the kth::secp256k1 component).
install(TARGETS secp256k1
    EXPORT secp256k1-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES
    secp256k1/include/secp256k1.h
    secp256k1/include/secp256k1_preallocated.h
    secp256k1/include/secp256k1_ecdh.h
    secp256k1/include/secp256k1_recovery.h
    secp256k1/include/secp256k1_extrakeys.h
    secp256k1/include/secp256k1_schnorrsig.h
    secp256k1/include/secp256k1_ellswift.h
    secp256k1/include/secp256k1_schnorr.h
    DESTINATION include
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

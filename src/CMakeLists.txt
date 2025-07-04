# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.15)

add_subdirectory(secp256k1)
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
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

include_guard(GLOBAL)

# Match Conan's default. Honor the old boolean only when initially configuring
# without the new selector; an explicit backend wins over a cached legacy flag.
if(NOT DEFINED KTH_RPC_BACKEND)
    if(DEFINED KTH_WITH_RPC AND NOT KTH_WITH_RPC)
        set(KTH_RPC_BACKEND "none" CACHE STRING "RPC backend (jsonrpc|grpc|capnproto|none).")
    else()
        set(KTH_RPC_BACKEND "jsonrpc" CACHE STRING "RPC backend (jsonrpc|grpc|capnproto|none).")
    endif()
endif()
set(KTH_RPC_BACKEND "${KTH_RPC_BACKEND}" CACHE STRING "RPC backend (jsonrpc|grpc|capnproto|none).")
set_property(CACHE KTH_RPC_BACKEND PROPERTY STRINGS jsonrpc grpc capnproto none)

if(NOT KTH_RPC_BACKEND MATCHES "^(jsonrpc|grpc|capnproto|none)$")
    message(FATAL_ERROR "Invalid RPC backend: '${KTH_RPC_BACKEND}' (expected jsonrpc|grpc|capnproto|none)")
endif()

# Native listeners are unavailable under WebAssembly, as in the Conan recipe.
# Keep the requested cache value so a later native configure can restore it.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(KTH_RPC_BACKEND "none")
endif()

if(KTH_RPC_BACKEND STREQUAL "grpc" OR KTH_RPC_BACKEND STREQUAL "capnproto")
    message(FATAL_ERROR "RPC backend '${KTH_RPC_BACKEND}' is not implemented; use jsonrpc or none.")
endif()

# This is a derived, non-cache variable. Existing source/consumer guards still
# mean JSON-RPC only; future transports must have their own sources and guards.
set(KTH_WITH_RPC OFF)
if(KTH_RPC_BACKEND STREQUAL "jsonrpc")
    set(KTH_WITH_RPC ON)
endif()
message(STATUS "Knuth: RPC backend: ${KTH_RPC_BACKEND}")

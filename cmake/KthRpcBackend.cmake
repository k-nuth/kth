# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

include_guard(GLOBAL)

# One backend per binary. The Conan recipe always passes KTH_RPC_BACKEND
# (default jsonrpc). Without it, the old boolean decides as it did: a configure
# that never set KTH_WITH_RPC built no RPC server, and still builds none.
if(NOT DEFINED KTH_RPC_BACKEND)
    if(KTH_WITH_RPC)
        set(KTH_RPC_BACKEND "jsonrpc" CACHE STRING "RPC backend (jsonrpc|grpc|capnproto|none).")
    else()
        set(KTH_RPC_BACKEND "none" CACHE STRING "RPC backend (jsonrpc|grpc|capnproto|none).")
    endif()
endif()
set(KTH_RPC_BACKEND "${KTH_RPC_BACKEND}" CACHE STRING "RPC backend (jsonrpc|grpc|capnproto|none).")

# The old boolean, given as a cache entry (-DKTH_WITH_RPC=...), is taken once and
# dropped, so a later one is seen as given. One that contradicts the backend is an
# error rather than silently ignored.
if(DEFINED CACHE{KTH_WITH_RPC})
    if(KTH_WITH_RPC AND NOT KTH_RPC_BACKEND STREQUAL "jsonrpc")
        message(FATAL_ERROR "KTH_WITH_RPC=ON contradicts KTH_RPC_BACKEND=${KTH_RPC_BACKEND}; "
            "KTH_WITH_RPC is replaced by KTH_RPC_BACKEND (jsonrpc|grpc|capnproto|none).")
    endif()
    if(NOT KTH_WITH_RPC AND KTH_RPC_BACKEND STREQUAL "jsonrpc")
        message(FATAL_ERROR "KTH_WITH_RPC=OFF contradicts KTH_RPC_BACKEND=jsonrpc; "
            "KTH_WITH_RPC is replaced by KTH_RPC_BACKEND (jsonrpc|grpc|capnproto|none).")
    endif()
    unset(KTH_WITH_RPC CACHE)
endif()
set_property(CACHE KTH_RPC_BACKEND PROPERTY STRINGS jsonrpc grpc capnproto none)

if(NOT KTH_RPC_BACKEND MATCHES "^(jsonrpc|grpc|capnproto|none)$")
    message(FATAL_ERROR "Invalid RPC backend: '${KTH_RPC_BACKEND}' (expected jsonrpc|grpc|capnproto|none)")
endif()

# Native listeners are unavailable under WebAssembly, as in the Conan recipe.
# Keep the requested cache value so a later native configure can restore it.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(KTH_RPC_BACKEND "none")
endif()

# Derived, non-cache variables. KTH_WITH_RPC keeps meaning JSON-RPC only; every
# backend has its own flag, sources and guards, and exactly one is on.
set(KTH_WITH_RPC OFF)
set(KTH_WITH_GRPC OFF)
set(KTH_WITH_CAPNPROTO_RPC OFF)
if(KTH_RPC_BACKEND STREQUAL "jsonrpc")
    set(KTH_WITH_RPC ON)
elseif(KTH_RPC_BACKEND STREQUAL "grpc")
    set(KTH_WITH_GRPC ON)
elseif(KTH_RPC_BACKEND STREQUAL "capnproto")
    set(KTH_WITH_CAPNPROTO_RPC ON)
endif()
message(STATUS "Knuth: RPC backend: ${KTH_RPC_BACKEND}")

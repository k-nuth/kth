# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.22)

project(kth VERSION 0.0.0 LANGUAGES CXX C)

include(cmake/KthRpcBackend.cmake)

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

# Mempool backing map: cfm (boost::concurrent_flat_map) | parlay (ParlayHash).
# Set at the root so the define (and, for parlay, the vendored include) reach
# every module that includes block_chain.hpp — not only blockchain.
set(KTH_MEMPOOL_BACKEND "cfm" CACHE STRING "Mempool backing map (cfm|parlay).")
if (${KTH_MEMPOOL_BACKEND} STREQUAL "cfm")
    add_definitions(-DKTH_MEMPOOL_BACKEND_CFM)
elseif (${KTH_MEMPOOL_BACKEND} STREQUAL "parlay")
    add_definitions(-DKTH_MEMPOOL_BACKEND_PARLAY)
    include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/third_party/parlayhash/include)
else()
    message(FATAL_ERROR "Invalid mempool backend: ${KTH_MEMPOOL_BACKEND} (expected cfm|parlay)")
endif()
message(STATUS "Knuth: Mempool backend: ${KTH_MEMPOOL_BACKEND}")

# Signature verification: secp256k1 (libsecp256k1, one signature at a time,
# inline in the script interpreter) | secp256k1_turbo (the interpreters defer
# the checks whose failure is final and the block validator verifies them in
# lane-parallel batches with src/secp256k1-turbo). Set at the root so the
# define reaches every module that compiles the interpreters.
set(KTH_SIGNATURE_VERIFICATION "secp256k1" CACHE STRING "Signature verification: secp256k1 | secp256k1_turbo")
if (KTH_SIGNATURE_VERIFICATION STREQUAL "secp256k1_turbo")
    add_definitions(-DKTH_SIGVERIFY_TURBO_BATCH)
elseif (NOT KTH_SIGNATURE_VERIFICATION STREQUAL "secp256k1")
    message(FATAL_ERROR "Invalid signature verification: ${KTH_SIGNATURE_VERIFICATION} (expected secp256k1|secp256k1_turbo)")
endif()
message(STATUS "Knuth: Signature verification: ${KTH_SIGNATURE_VERIFICATION}")
# GPU backends of src/secp256k1-turbo: none | auto | cuda | metal. Off by
# default in the node build; src/secp256k1-turbo/CMakeLists.txt documents the
# toolchains each one needs.
set(KTH_SECP256K1_TURBO_GPU "none" CACHE STRING "secp256k1-turbo GPU backends: none | auto | cuda | metal")
message(STATUS "Knuth: secp256k1-turbo GPU backends: ${KTH_SECP256K1_TURBO_GPU}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/KthCompilerChecks.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/KthTests.cmake)

option(BUILD_NODE_EXE "Build the node executable." ON)
option(BUILD_C_API "Build the C-API." ON)
option(GLOBAL_BUILD "Global build flag." ON)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_CONSENSUS "Link consensus and use for consensus checks." ON)
option(WITH_CONSOLE "Compile console application." OFF)
option(WITH_LOCAL_MINING "Compile with local mining." ON)
option(ENABLE_TEST "Compile with enable test." ON)
option(ENABLE_MODULE_RECOVERY "Compile with enable module recovery." ON)
option(WITH_TOOLS "Compile with tools." OFF)
option(WITH_CONSOLE_NODE_CINT "" OFF)
option(WITH_EXAMPLES "Compile with examples." OFF)
option(ENABLE_SHARED "" OFF)
option(ENABLE_SHARED_NODE_CINT "" ON)
option(KTH_ASIO_STANDALONE "Use standalone Asio instead of Boost.Asio" OFF)
option(KTH_BROWSER_NODE "Build the experimental Worker-owned WebSocket node" OFF)
if(KTH_BROWSER_NODE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR NOT KTH_ASIO_STANDALONE)
        message(FATAL_ERROR "KTH_BROWSER_NODE requires Emscripten and standalone Asio")
    endif()
    add_compile_definitions(KTH_BROWSER_NODE ASIO_DISABLE_THREADS)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>)
    add_link_options(-fwasm-exceptions -lwebsocket.js)
    find_package(asio REQUIRED)
    # Selections wait on the Worker runtime's timer, the one timer it wakes for
    # (asio 1.38.3-kth.3, docs/asio-select-sync.md).
    add_compile_definitions(
        ASIO_CHANNEL_SELECT_TIMER_HEADER="kth/infrastructure/utility/browser_runtime.hpp"
        ASIO_CHANNEL_SELECT_TIMER_TYPE=::kth::browser::timer)
endif()
option(KTH_WITH_WEBSOCKET "Build native P2P WebSocket server transport" OFF)

# set(ENABLE_POSITION_INDEPENDENT_CODE ON)
# set(WITH_CONSOLE OFF)
# set(WITH_LOCAL_MINING OFF)
# set(ENABLE_TEST ON)
# set(ENABLE_MODULE_RECOVERY ON)
# set(WITH_TOOLS OFF)
# set(WITH_EXAMPLES OFF)
# set(ENABLE_SHARED OFF)
# set(ENABLE_SHARED_NODE_CINT ON)

if (WITH_LOCAL_MINING)
	add_definitions(-DWITH_LOCAL_MINING)
endif()

# Asio standalone mode - define both our macro and Asio's expected macro
if (KTH_ASIO_STANDALONE)
    add_definitions(-DKTH_ASIO_STANDALONE)
    add_definitions(-DASIO_STANDALONE)
endif()

# jemalloc support
option(KTH_WITH_JEMALLOC "Use jemalloc for memory allocation" OFF)
if (KTH_WITH_JEMALLOC)
    find_package(jemalloc REQUIRED)
    add_definitions(-DKTH_WITH_JEMALLOC)
    message(STATUS "Knuth: jemalloc enabled")
endif()

# Stats/profiling support
option(KTH_WITH_STATS "Enable performance statistics collection" OFF)
if (KTH_WITH_STATS)
    add_definitions(-DKTH_WITH_STATS)
    message(STATUS "Knuth: stats/profiling enabled")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)
include(KnuthTools)

if (NOT DEFINED ENABLE_MODULE_RECOVERY)
  set(ENABLE_MODULE_RECOVERY ON CACHE BOOL "Enable ECDSA pubkey recovery module.")
endif()

find_package(gmp REQUIRED)

# Enable testing if tests are enabled
if(ENABLE_TEST)
    message(STATUS "Knuth: Enabling CTest (ENABLE_TEST=${ENABLE_TEST})")
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME kth_header_capacity
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_header_capacity.py)
    endif()
endif()

add_subdirectory(src)
option(KTH_BROWSER_CONTROLS "Build browser runtime and transport controls" OFF)
if(KTH_BROWSER_NODE AND KTH_BROWSER_CONTROLS)
    add_subdirectory(experiments/browser-worker)
endif()

# Exercise the real parser translation unit's currency selection with the
# generated target flags. flags.make is provided by the Makefiles generator.
if(ENABLE_TEST AND Python3_Interpreter_FOUND AND TARGET node
   AND CMAKE_GENERATOR STREQUAL "Unix Makefiles"
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_test(NAME kth_parser_currency_options
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_parser_currencies.py ${CMAKE_BINARY_DIR})
    set_tests_properties(kth_parser_currency_options PROPERTIES TIMEOUT 120)
endif()



# Scoped executable linkage control for the actual public tick definitions.
# Full non-BCH builds currently have unrelated baseline errors; this Linux
# static-library fixture isolates the currency guard using configured binutils.
if(ENABLE_TEST AND Python3_Interpreter_FOUND AND TARGET kth_domain_test
   AND CURRENCY STREQUAL "BCH" AND NOT ENABLE_SHARED
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_GENERATOR STREQUAL "Unix Makefiles"
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_test(NAME kth_tick_accessor_currencies
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_tick_accessor_currencies.py ${CMAKE_BINARY_DIR})
    set_tests_properties(kth_tick_accessor_currencies PROPERTIES TIMEOUT 120)
endif()

# Install the main kth CMake configuration file
# ------------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Configure the main kth config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/kth-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/kth-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kth"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

# Install the config file
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/kth-config.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kth"
)

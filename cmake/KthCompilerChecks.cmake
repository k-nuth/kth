# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Compilers kth refuses to build with.
#
# GCC 15 and 16.1, at -O1 and above, never destroy the object behind a
# by-value structured binding that is still in scope at a later suspension
# point. kth binds co_await results everywhere (`auto [ec, msg] = co_await
# channel.async_receive(...)`), so each turn of a receive loop would keep the
# previous message, and whatever it owns, alive in the coroutine frame. GCC 14,
# GCC 16.2 and Clang destroy it, as the language requires.
#
# Two checks: the GCC version, which also covers cross builds, and a probe that
# runs the pattern with optimization and fails when the bound object is not
# destroyed, whichever compiler it is. The probe is skipped when the target
# cannot run on the build machine.

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.2)
    message(FATAL_ERROR
        "Knuth: GCC ${CMAKE_CXX_COMPILER_VERSION} is not supported; GCC 16.2 or later is required. "
        "GCC 15 and 16.1 do not destroy a structured binding that lives across a coroutine "
        "suspension, which kth relies on.")
endif()

if(CMAKE_CROSSCOMPILING)
    message(STATUS "Knuth: cross build, coroutine binding lifetime probe skipped")
    return()
endif()

if(MSVC)
    set(_kth_probe_optimization /O2)
else()
    set(_kth_probe_optimization -O2)
endif()

try_run(_kth_probe_run _kth_probe_compile
    ${CMAKE_BINARY_DIR}/kth_compiler_checks
    ${CMAKE_CURRENT_LIST_DIR}/checks/coroutine_binding_lifetime.cpp
    COMPILE_DEFINITIONS ${_kth_probe_optimization}
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    COMPILE_OUTPUT_VARIABLE _kth_probe_output)

if(NOT _kth_probe_compile)
    message(FATAL_ERROR "Knuth: the coroutine binding lifetime probe did not compile:\n${_kth_probe_output}")
endif()
if(NOT _kth_probe_run EQUAL 0)
    message(FATAL_ERROR
        "Knuth: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not destroy a structured "
        "binding that lives across a coroutine suspension (cmake/checks/coroutine_binding_lifetime.cpp). "
        "kth cannot be built with it.")
endif()
message(STATUS "Knuth: coroutine binding lifetime probe passed")

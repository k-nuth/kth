# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(network VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)

option(GLOBAL_BUILD "" ON)


set(MARCH_ID "" CACHE STRING "Specify the Microarchitecture ID (x86_64|...).")
message( STATUS "Knuth: Compiling for Microarchitecture ID ${MARCH_ID}")

message( STATUS "Knuth: CONAN_CXX_FLAGS ${CONAN_CXX_FLAGS}")
message( STATUS "Knuth: CONAN_C_FLAGS ${CONAN_C_FLAGS}")

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

if (NOT GLOBAL_BUILD)
  find_package(domain REQUIRED)
endif()


include(CheckCXXCompilerFlag)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)
include(KnuthTools)


if (MINGW)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wa,-mbig-obj")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj")
endif()

if (NOT MSVC)
  _add_c_compile_flag(-Wall _has_all_warning_flag)
else()
  _add_c_compile_flag(-W4 _has_all_warning_flag)
  add_definitions(-D_SCL_SECURE_NO_WARNINGS)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

if (NOT MSVC)
  _add_c_compile_flag(-Wextra _has_extra_warning_flag)
endif()

_add_c_compile_flag(-Wpedantic _has_pedantic_warning_flag)
if (_has_pedantic_warning_flag)
  _add_c_compile_flag(-pedantic _has_pedantic_flag)
endif()

_add_cxx_compile_flag(-Wno-missing-braces _has_no_missing_braces_warning_flag)
_add_cxx_compile_flag(-Wno-mismatched-tags _has_no_mismatched_tags_warning_flag)
_add_c_compile_flag(-Wno-deprecated-declarations _has_no_deprecated_declarations_warning_flag)
_add_link_flag(-fstack-protector _has_stack_protector_flag)
_add_link_flag(-fstack-protector-all _has_stack_protector_all_flag)
_add_cxx_compile_flag(-fvisibility-hidden _has_visibility_hidden_flag)
_add_cxx_compile_flag(-fvisibility-inlines-hidden _has_visibility_inlines_hidden_flag)

if (MSVC)
  add_definitions(-D_WIN32_WINNT=0x0600)
endif()

# Build
# ------------------------------------------------------------------------------

set(MODE STATIC)
if (ENABLE_SHARED)
  set(MODE SHARED)
endif()


set(kth_headers
  include/kth/network/acceptor.hpp
  include/kth/network/define.hpp
  include/kth/network/proxy.hpp
  include/kth/network/channel.hpp
  include/kth/network/hosts.hpp
  include/kth/network/p2p.hpp
  include/kth/network/sessions/session_outbound.hpp
  include/kth/network/sessions/session_seed.hpp
  include/kth/network/sessions/session_inbound.hpp
  include/kth/network/sessions/session_manual.hpp
  include/kth/network/sessions/session_batch.hpp
  include/kth/network/sessions/session.hpp
  include/kth/network/connector.hpp
  include/kth/network/message_subscriber.hpp
  include/kth/network/protocols/protocol_version_70002.hpp
  include/kth/network/protocols/protocol_seed_31402.hpp
  include/kth/network/protocols/protocol_timer.hpp
  include/kth/network/protocols/protocol_version_31402.hpp
  include/kth/network/protocols/protocol_address_31402.hpp
  include/kth/network/protocols/protocol_ping_31402.hpp
  include/kth/network/protocols/protocol_events.hpp
  include/kth/network/protocols/protocol.hpp
  include/kth/network/protocols/protocol_ping_60001.hpp
  include/kth/network/protocols/protocol_reject_70002.hpp
  include/kth/network/settings.hpp
  include/kth/network/version.hpp
  include/kth/network.hpp
)

set(kth_sources
  src/protocols/protocol.cpp
  src/protocols/protocol_address_31402.cpp
  src/protocols/protocol_events.cpp
  src/protocols/protocol_ping_31402.cpp
  src/protocols/protocol_ping_60001.cpp
  src/protocols/protocol_reject_70002.cpp
  src/protocols/protocol_seed_31402.cpp
  src/protocols/protocol_timer.cpp
  src/protocols/protocol_version_31402.cpp
  src/protocols/protocol_version_70002.cpp
  src/sessions/session.cpp
  src/sessions/session_batch.cpp
  src/sessions/session_inbound.cpp
  src/sessions/session_manual.cpp
  src/sessions/session_outbound.cpp
  src/sessions/session_seed.cpp
  src/acceptor.cpp
  src/channel.cpp
  src/connector.cpp
  src/hosts.cpp
  src/message_subscriber.cpp
  src/p2p.cpp
  src/proxy.cpp
  src/settings.cpp
  src/version.cpp
)

add_library(${PROJECT_NAME} ${MODE} ${kth_sources} ${kth_headers})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX CXX_STANDARD 23 CXX_STANDARD_REQUIRED TRUE)


if (ENABLE_POSITION_INDEPENDENT_CODE)
  set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
endif(ENABLE_POSITION_INDEPENDENT_CODE)

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if (NOT ENABLE_SHARED)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DKN_STATIC)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC domain::domain)

if (MINGW)
  target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 wsock32)
endif()

_group_sources(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}")

# Tests
# ------------------------------------------------------------------------------
# Skip tests for WebAssembly (Catch2 incompatible with shared-memory/threads)
if (ENABLE_TEST AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  enable_testing()
  find_package(Catch2 3 REQUIRED)

  add_executable(kth_network_test
    test/p2p.cpp
  )

  target_include_directories(kth_network_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)

  target_link_libraries(kth_network_test PUBLIC ${PROJECT_NAME})
  target_link_libraries(kth_network_test PRIVATE Catch2::Catch2WithMain)

  _group_sources(kth_network_test "${CMAKE_CURRENT_LIST_DIR}/test")

  include(CTest)
  # Try to use Catch2 automatic test discovery
  find_package(Catch2 QUIET)
  if(Catch2_FOUND)
    include(Catch)
    catch_discover_tests(kth_network_test)
  else()
    # Fallback to manual test registration
    add_test(NAME kth_network_test COMMAND kth_network_test)
  endif()
endif()


# Install
# ------------------------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Generate and install a CMake package configuration file
install(EXPORT ${PROJECT_NAME}-targets
    FILE ${PROJECT_NAME}-config.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)


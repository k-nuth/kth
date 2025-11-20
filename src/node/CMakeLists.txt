# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(node VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_MEMPOOL "Mempool enabled." OFF)
option(DB_READONLY_MODE "Readonly DB mode enabled." OFF)
option(JUST_KTH_SOURCES "Just Knuth source code to be linted." OFF)
option(STATISTICS "Collect statistics." OFF)

option(GLOBAL_BUILD "" ON)

if (WITH_MEMPOOL)
  message(STATUS "Knuth: Mempool enabled")
  add_definitions(-DKTH_WITH_MEMPOOL)
endif()

if (DB_READONLY_MODE)
  message(STATUS "Knuth: Readonly DB mode enabled")
  add_definitions(-DKTH_DB_READONLY)
endif()

if (JUST_KTH_SOURCES)
  message(STATUS "Knuth: Just Knuth source code to be linted: enabled")
endif()

if (STATISTICS)
  add_definitions(-DKTH_STATISTICS_ENABLED)
endif()

add_definitions(-DKTH_MICROARCHITECTURE_STR="${MARCH_ID}")
set(MARCH_ID "" CACHE STRING "Specify the Microarchitecture ID (x86_64|...).")
message( STATUS "Knuth: Compiling for Microarchitecture ID ${MARCH_ID}")

add_definitions(-DKTH_MARCH_NAMES_FULL_STR="${MARCH_NAMES_FULL_STR}")
set(MARCH_NAMES_FULL_STR "" CACHE STRING "String with full list of CPU instructions/extensions.")
message( STATUS "Knuth: Built for CPU instructions/extensions: ${MARCH_NAMES_FULL_STR}")

message( STATUS "Knuth: CONAN_CXX_FLAGS ${CONAN_CXX_FLAGS}")
message( STATUS "Knuth: CONAN_C_FLAGS ${CONAN_C_FLAGS}")

set(LOG_LIBRARY "boost" CACHE STRING "Setting for the logging library (boost|spdlog|binlog).")

if (${LOG_LIBRARY} STREQUAL "boost")
  add_definitions(-DKTH_LOG_LIBRARY_BOOST)
elseif (${LOG_LIBRARY} STREQUAL "spdlog")
  add_definitions(-DKTH_LOG_LIBRARY_SPDLOG)
elseif (${LOG_LIBRARY} STREQUAL "binlog")
  add_definitions(-DKTH_LOG_LIBRARY_BINLOG)
else()
  message(FATAL_ERROR "Invalid Logging Library: ${LOG_LIBRARY}")
endif()

message(STATUS "Knuth: Logging Library: ${LOG_LIBRARY}")

set(CURRENCY "BCH" CACHE STRING "Specify the Cryptocurrency (BCH|BTC|LTC).")

if (${CURRENCY} STREQUAL "BCH")
  add_definitions(-DKTH_CURRENCY_BCH)
  add_definitions(-DKTH_CURRENCY_SYMBOL_STR="BCH")
  add_definitions(-DKTH_CURRENCY_STR="Bitcoin Cash")
elseif (${CURRENCY} STREQUAL "BTC")
  add_definitions(-DKTH_CURRENCY_BTC)
  add_definitions(-DKTH_CURRENCY_SYMBOL_STR="BTC")
  add_definitions(-DKTH_CURRENCY_STR="Bitcoin Legacy")
elseif (${CURRENCY} STREQUAL "LTC")
  add_definitions(-DKTH_CURRENCY_LTC)
  add_definitions(-DKTH_CURRENCY_SYMBOL_STR="LTC")
  add_definitions(-DKTH_CURRENCY_STR="Litecoin")
else()
  message(FATAL_ERROR "Invalid Cryptocurrency: ${CURRENCY}")
endif()

message(STATUS "Knuth: Cryptocurrency: ${CURRENCY}")

# --------------------------------------------------------------------------------
string(TIMESTAMP KTH_NODE_BUILD_TIMESTAMP "%s" UTC)
message(STATUS "KTH_NODE_BUILD_TIMESTAMP: ${KTH_NODE_BUILD_TIMESTAMP}")
add_definitions(-DKTH_NODE_BUILD_TIMESTAMP=${KTH_NODE_BUILD_TIMESTAMP})
# --------------------------------------------------------------------------------



if (NOT GLOBAL_BUILD)
  find_package(blockchain REQUIRED)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    find_package(network REQUIRED)
  endif()
endif()

include(CheckCXXCompilerFlag)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)
include(KnuthTools)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -matomics -mbulk-memory")
  _add_cxx_compile_flag(-matomics _has_matomics_flag)
  _add_cxx_compile_flag(-mbulk-memory _has_mbulk_memory_flag)
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

set(kth_sources_just_legacy
  src/configuration.cpp
  src/executor/executor_info.cpp
  src/parser.cpp
  src/settings.cpp
  src/version.cpp
  src/user_agent.cpp

  src/utility/check_list.cpp
  src/utility/header_list.cpp
  src/utility/performance.cpp
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(kth_sources_just_legacy
    ${kth_sources_just_legacy}
    src/full_node.cpp
    src/executor/executor.cpp

    src/protocols/protocol_block_in.cpp
    src/protocols/protocol_block_out.cpp
    src/protocols/protocol_block_sync.cpp
    src/protocols/protocol_double_spend_proof_in.cpp
    src/protocols/protocol_double_spend_proof_out.cpp
    src/protocols/protocol_header_sync.cpp
    src/protocols/protocol_transaction_in.cpp
    src/protocols/protocol_transaction_out.cpp

    src/sessions/session_block_sync.cpp
    src/sessions/session_header_sync.cpp
    src/sessions/session_inbound.cpp
    src/sessions/session_manual.cpp
    src/sessions/session_outbound.cpp

    src/utility/reservation.cpp
    src/utility/reservations.cpp

  )
endif()

if (NOT JUST_KTH_SOURCES)
  set(kth_sources
    ${kth_sources_just_legacy}
  )
endif()

set(kth_sources
  ${kth_sources}
  ${kth_sources_just_kth}
)

set(kth_headers
  include/kth/node.hpp
  include/kth/node/configuration.hpp
  include/kth/node/user_agent.hpp
  include/kth/node/executor/executor.hpp
  include/kth/node/executor/executor_info.hpp
  include/kth/node/define.hpp

  include/kth/node/utility/reservation.hpp
  include/kth/node/utility/check_list.hpp
  include/kth/node/utility/header_list.hpp
  include/kth/node/utility/performance.hpp
  include/kth/node/utility/reservations.hpp
  include/kth/node/settings.hpp
  include/kth/node/full_node.hpp
  include/kth/node/parser.hpp
  include/kth/node/version.hpp
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(kth_headers
    ${kth_headers}
    include/kth/node/sessions/session_header_sync.hpp
    include/kth/node/sessions/session_outbound.hpp
    include/kth/node/sessions/session_inbound.hpp
    include/kth/node/sessions/session_block_sync.hpp
    include/kth/node/sessions/session_manual.hpp
    include/kth/node/sessions/session.hpp
    include/kth/node/protocols/protocol_block_sync.hpp
    include/kth/node/protocols/protocol_header_sync.hpp
    include/kth/node/protocols/protocol_block_in.hpp
    include/kth/node/protocols/protocol_transaction_out.hpp
    include/kth/node/protocols/protocol_double_spend_proof_out.hpp
    include/kth/node/protocols/protocol_double_spend_proof_in.hpp
    include/kth/node/protocols/protocol_transaction_in.hpp
    include/kth/node/protocols/protocol_block_out.hpp
  )
endif()


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
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DKND_STATIC)
  # Note: Conan should propagate KB_STATIC and KN_STATIC from blockchain and network components
  # target_compile_definitions(${PROJECT_NAME} PUBLIC -DKB_STATIC -DKN_STATIC)
endif()

if (NOT MSVC)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DSYSCONFDIR=\"${SYSCONFDIR}\")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC blockchain::blockchain)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_link_libraries(${PROJECT_NAME} PUBLIC network::network)
endif()

if (MINGW)
  target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 wsock32)
endif()

_group_sources(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}")

# Tests
# ------------------------------------------------------------------------------
if (ENABLE_TEST)
  enable_testing()
  find_package(Catch2 3 REQUIRED)

  add_executable(kth_node_test
          test/check_list.cpp
          test/configuration.cpp
          test/header_list.cpp
          test/main.cpp
          test/node.cpp
          test/performance.cpp
          test/reservation.cpp
          test/reservations.cpp
          test/settings.cpp
          test/utility.cpp
          test/utility.hpp)

  target_include_directories(kth_node_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)

  target_link_libraries(kth_node_test PUBLIC ${PROJECT_NAME})
  target_link_libraries(kth_node_test PRIVATE Catch2::Catch2WithMain)

  _group_sources(kth_node_test "${CMAKE_CURRENT_LIST_DIR}/test")

  include(CTest)
  # Try to use Catch2 automatic test discovery
  find_package(Catch2 QUIET)
  if(Catch2_FOUND)
    include(Catch)
    catch_discover_tests(kth_node_test)
  else()
    # Fallback to manual test registration
    add_test(NAME kth_node_test COMMAND kth_node_test)
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

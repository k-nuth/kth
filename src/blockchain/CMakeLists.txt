# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(blockchain VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_TOOLS "Compile with tools." OFF)
option(WITH_MEMPOOL "Mempool enabled." OFF)
option(DB_READONLY_MODE "Readonly DB mode enabled." OFF)
option(JUST_KTH_SOURCES "Just Knuth source code to be linted." OFF)

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
  find_package(database REQUIRED)
endif()


if (WITH_CONSENSUS)
  if (NOT GLOBAL_BUILD)
    find_package(consensus REQUIRED)
  endif()
endif()


include(CheckCXXCompilerFlag)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)
include(KnuthTools)


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


if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -matomics -mbulk-memory")
    _add_cxx_compile_flag(-matomics _has_matomics_flag)
    _add_cxx_compile_flag(-mbulk-memory _has_mbulk_memory_flag)
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
  src/interface/block_chain.cpp
  # src/interface/block_chain_old_db.cpp
  src/pools/block_entry.cpp
  src/pools/block_organizer.cpp
  src/pools/block_pool.cpp
  src/pools/branch.cpp
  src/pools/transaction_entry.cpp
  src/pools/transaction_organizer.cpp
  src/pools/transaction_pool.cpp
  src/pools/mempool_transaction_summary.cpp
  src/populate/populate_base.cpp
  src/populate/populate_block.cpp
  src/populate/populate_chain_state.cpp
  src/populate/populate_transaction.cpp
  src/validate/validate_block.cpp
  src/validate/validate_input.cpp
  src/validate/validate_transaction.cpp
  src/settings.cpp
  src/version.cpp
)

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
  include/kth/blockchain.hpp
  include/kth/blockchain/interface
  include/kth/blockchain/interface/fast_chain.hpp
  include/kth/blockchain/interface/block_chain.hpp
  include/kth/blockchain/interface/safe_chain.hpp
  include/kth/blockchain/define.hpp
  include/kth/blockchain/populate/populate_transaction.hpp
  include/kth/blockchain/populate/populate_chain_state.hpp
  include/kth/blockchain/populate/populate_block.hpp
  include/kth/blockchain/populate/populate_base.hpp
  include/kth/blockchain/validate/validate_input.hpp
  include/kth/blockchain/validate/validate_transaction.hpp
  include/kth/blockchain/validate/validate_block.hpp
  include/kth/blockchain/pools/block_entry.hpp
  include/kth/blockchain/pools/transaction_pool.hpp
  include/kth/blockchain/pools/transaction_entry.hpp
  include/kth/blockchain/pools/mempool_transaction_summary.hpp
  include/kth/blockchain/pools/block_organizer.hpp
  include/kth/blockchain/pools/branch.hpp
  include/kth/blockchain/pools/block_pool.hpp
  include/kth/blockchain/pools/transaction_organizer.hpp
  include/kth/blockchain/mining/mempool_v1_old.hpp
  include/kth/blockchain/mining/prioritizer.hpp
  include/kth/blockchain/mining/transaction_element.1.hpp
  include/kth/blockchain/mining/node_v2.hpp
  include/kth/blockchain/mining/transaction_element.hpp
  include/kth/blockchain/mining/mempool_v1.hpp
  include/kth/blockchain/mining/mempool.hpp
  include/kth/blockchain/mining/common.hpp
  include/kth/blockchain/mining/partially_indexed.hpp
  include/kth/blockchain/mining/node_v1.hpp
  include/kth/blockchain/mining/mempool_v2.hpp
  include/kth/blockchain/mining/partially_indexed_node.hpp
  include/kth/blockchain/settings.hpp
  include/kth/blockchain/version.hpp
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
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DBCB_STATIC -DBCD_STATIC -DBC_STATIC)
endif()

if (WITH_CONSENSUS)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DBCK_STATIC)
endif(WITH_CONSENSUS)

target_link_libraries(${PROJECT_NAME} PUBLIC database::database)

if (WITH_CONSENSUS)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DWITH_CONSENSUS)
  target_link_libraries(${PROJECT_NAME} PUBLIC consensus::consensus)
endif(WITH_CONSENSUS)

if (MINGW)
  target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 wsock32)
endif()

_group_sources(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}")

# Tests
# ------------------------------------------------------------------------------
# if (ENABLE_TEST)
#     enable_testing()
#     find_package(Catch2 3 REQUIRED)

#     set(kth_blockchain_test_sources
#         test/block_chain.cpp
#         test/block_entry.cpp
#         test/block_pool.cpp
#         test/branch.cpp
#         test/transaction_entry.cpp
#         test/transaction_pool.cpp
#         test/validate_block.cpp
#         test/validate_transaction.cpp
#         test/utxo.cpp
#         test/main.cpp
#     )

#     if (WITH_MEMPOOL)
#         set(kth_blockchain_test_sources
#             ${kth_blockchain_test_sources}
#             test/mempool_tests.cpp
#         )
#     endif()

#     add_executable(kth_blockchain_test
#         ${kth_blockchain_test_sources}
#     )

#     target_include_directories(kth_blockchain_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)
#     target_link_libraries(kth_blockchain_test PUBLIC ${PROJECT_NAME})
#     target_link_libraries(kth_blockchain_test PRIVATE Catch2::Catch2WithMain)

#     _group_sources(kth_blockchain_test "${CMAKE_CURRENT_LIST_DIR}/test")

#     include(CTest)
#     # Try to use Catch2 automatic test discovery
#     find_package(Catch2 QUIET)
#     if(Catch2_FOUND)
#       include(Catch)
#       catch_discover_tests(kth_blockchain_test)
#     else()
#       # Fallback to manual test registration
#       add_test(NAME kth_blockchain_test COMMAND kth_blockchain_test)
#     endif()
# endif()

# Tools
# ------------------------------------------------------------------------------
if (WITH_TOOLS)
  add_executable(tools.initchain tools/initchain/initchain.cpp)

  target_link_libraries(tools.initchain ${PROJECT_NAME})
  _group_sources(tools.initchain "${CMAKE_CURRENT_LIST_DIR}/tools/initchain")
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

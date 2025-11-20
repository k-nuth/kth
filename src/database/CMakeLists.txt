# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(database VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_TOOLS "Compile with tools." OFF)
option(WITH_MEASUREMENTS "Measurements enabled." OFF)
option(DB_READONLY_MODE "Readonly DB mode enabled." OFF)
option(JUST_KTH_SOURCES "Just Knuth source code to be linted." OFF)

option(GLOBAL_BUILD "" ON)

set(MARCH_ID "" CACHE STRING "Specify the Microarchitecture ID (x86_64|...).")
message( STATUS "Knuth: Compiling for Microarchitecture ID ${MARCH_ID}")

if (WITH_MEASUREMENTS)
  message(STATUS "Knuth: MEASUREMENTS enabled")
  add_definitions(-DWITH_MEASUREMENTS)
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

find_package(lmdb 0.9.29 REQUIRED)

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
#==============================================================================
set(MODE STATIC)
if (ENABLE_SHARED)
    set(MODE SHARED)
endif()




set(kth_sources_just_kth
    ${kth_sources_just_kth}
    src/data_base.cpp
    src/settings.cpp
    src/store.cpp
    src/version.cpp

    src/databases/header_abla_entry.cpp
    src/databases/utxo_entry.cpp
    src/databases/history_entry.cpp
    src/databases/transaction_entry.cpp
    src/databases/transaction_unconfirmed_entry.cpp
)


set(kth_sources
  ${kth_sources}
  ${kth_sources_just_kth}
)

set(kth_headers
  include/kth/database/store.hpp
  include/kth/database/currency_config.hpp
  include/kth/database/define.hpp
  include/kth/database/data_base.hpp
  include/kth/database/databases/block_database.ipp
  include/kth/database/databases/property_code.hpp
  include/kth/database/databases/internal_database.ipp
  include/kth/database/databases/reorg_database.ipp
  include/kth/database/databases/internal_database.hpp
  include/kth/database/databases/transaction_unconfirmed_database.ipp
  include/kth/database/databases/transaction_entry.hpp
  include/kth/database/databases/history_database.ipp
  include/kth/database/databases/history_entry.hpp
  include/kth/database/databases/transaction_database.ipp
  include/kth/database/databases/generic_db.hpp
  include/kth/database/databases/tools.hpp
  include/kth/database/databases/lmdb_helper.hpp
  include/kth/database/databases/result_code.hpp
  include/kth/database/databases/transaction_unconfirmed_entry.hpp
  include/kth/database/databases/header_abla_entry.hpp
  include/kth/database/databases/utxo_entry.hpp
  include/kth/database/databases/spend_database.ipp
  include/kth/database/databases/utxo_database.ipp
  include/kth/database/databases/header_database.ipp
  include/kth/database/settings.hpp
  # include/kth/database/unspent_outputs.hpp
  # include/kth/database/unspent_transaction.hpp
  include/kth/database/version.hpp
  include/kth/database.hpp
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
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DKDB_STATIC)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC domain::domain)
target_link_libraries(${PROJECT_NAME} PUBLIC lmdb::lmdb)

if (MINGW)
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 wsock32)
endif()

_group_sources(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}")

# Tests
#==============================================================================
# if (ENABLE_TEST)
#     enable_testing()
#     find_package(Catch2 3 REQUIRED)

#     add_executable(kth_database_test
#             test/main.cpp
#             test/internal_database.cpp
#             )

#     target_include_directories(kth_database_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)

#     target_link_libraries(kth_database_test PUBLIC ${PROJECT_NAME})
#     target_link_libraries(kth_database_test PRIVATE Catch2::Catch2WithMain)

#     _group_sources(kth_database_test "${CMAKE_CURRENT_LIST_DIR}/test")

#     include(CTest)
#   # Try to use Catch2 automatic test discovery
#   find_package(Catch2 QUIET)
#   if(Catch2_FOUND)
#     include(Catch)
#     catch_discover_tests(kth_database_test)
#   else()
#     # Fallback to manual test registration
#     add_test(NAME kth_database_test COMMAND kth_database_test)
#   endif()
# endif()

# Tools
#------------------------------------------------------------------------------
if (WITH_TOOLS)
    add_executable(tools.block_db
            tools/block_db/block_db.cpp)
    target_link_libraries(tools.block_db ${PROJECT_NAME})
    _group_sources(tools.block_db "${CMAKE_CURRENT_LIST_DIR}/tools/block_db")

    add_executable(tools.count_records
            tools/count_records/count_records.cpp)
    target_link_libraries(tools.count_records ${PROJECT_NAME})
    _group_sources(tools.count_records "${CMAKE_CURRENT_LIST_DIR}/tools/count_records")

    add_executable(tools.history_db
            tools/history_db/history_db.cpp)
    target_link_libraries(tools.history_db ${PROJECT_NAME})
    _group_sources(tools.history_db "${CMAKE_CURRENT_LIST_DIR}/tools/history_db")

    add_executable(tools.initchain_db
            tools/initchain/initchain.cpp)
    target_link_libraries(tools.initchain_db ${PROJECT_NAME})
    _group_sources(tools.initchain_db "${CMAKE_CURRENT_LIST_DIR}/tools/initchain")

    add_executable(tools.mmr_add_row
            tools/mmr_add_row/mmr_add_row.cpp)
    target_link_libraries(tools.mmr_add_row ${PROJECT_NAME})
    _group_sources(tools.mmr_add_row "${CMAKE_CURRENT_LIST_DIR}/tools/mmr_add_row")

    add_executable(tools.mmr_create
            tools/mmr_create/mmr_create.cpp)
    target_link_libraries(tools.mmr_create ${PROJECT_NAME})
    _group_sources(tools.mmr_create "${CMAKE_CURRENT_LIST_DIR}/tools/mmr_create")

    add_executable(tools.mmr_delete_last_row
            tools/mmr_delete_last_row/mmr_delete_last_row.cpp)
    target_link_libraries(tools.mmr_delete_last_row ${PROJECT_NAME})
    _group_sources(tools.mmr_delete_last_row "${CMAKE_CURRENT_LIST_DIR}/tools/mmr_delete_last_row")

    add_executable(tools.mmr_lookup
            tools/mmr_lookup/mmr_lookup.cpp)
    target_link_libraries(tools.mmr_lookup ${PROJECT_NAME})
    _group_sources(tools.mmr_lookup "${CMAKE_CURRENT_LIST_DIR}/tools/mmr_lookup")

    add_executable(tools.read_htdb_record_value
            tools/read_htdb_record_value/read_htdb_record_value.cpp)
    target_link_libraries(tools.read_htdb_record_value ${PROJECT_NAME})
    _group_sources(tools.read_htdb_record_value "${CMAKE_CURRENT_LIST_DIR}/tools/read_htdb_record_value")

    add_executable(tools.read_htdb_slab_value
            tools/read_htdb_slab_value/read_htdb_slab_value.cpp)
    target_link_libraries(tools.read_htdb_slab_value ${PROJECT_NAME})
    _group_sources(tools.read_htdb_slab_value "${CMAKE_CURRENT_LIST_DIR}/tools/read_htdb_slab_value")

    add_executable(tools.show_array
            tools/show_array/show_array.cpp)
    target_link_libraries(tools.show_array ${PROJECT_NAME})
    _group_sources(tools.show_array "${CMAKE_CURRENT_LIST_DIR}/tools/show_array")

    add_executable(tools.show_linked_records
            tools/show_linked_records/show_linked_records.cpp)
    target_link_libraries(tools.show_linked_records ${PROJECT_NAME})
    _group_sources(tools.show_linked_records "${CMAKE_CURRENT_LIST_DIR}/tools/initchain")

    add_executable(tools.show_records
            tools/show_records/show_records.cpp)
    target_link_libraries(tools.show_records ${PROJECT_NAME})
    _group_sources(tools.show_records "${CMAKE_CURRENT_LIST_DIR}/tools/show_records")

    add_executable(tools.spend_db
            tools/spend_db/spend_db.cpp)
    target_link_libraries(tools.spend_db ${PROJECT_NAME})
    _group_sources(tools.spend_db "${CMAKE_CURRENT_LIST_DIR}/tools/spend_db")

    add_executable(tools.stealth_db
            tools/stealth_db/stealth_db.cpp)
    target_link_libraries(tools.stealth_db ${PROJECT_NAME})
    _group_sources(tools.stealth_db "${CMAKE_CURRENT_LIST_DIR}/tools/stealth_db")

    add_executable(tools.transaction_db
            tools/transaction_db/transaction_db.cpp)
    target_link_libraries(tools.transaction_db ${PROJECT_NAME})
    _group_sources(tools.transaction_db "${CMAKE_CURRENT_LIST_DIR}/tools/transaction_db")

#   add_executable(tools.unspent_db
#     tools/unspent_db/unspent_db.cpp)
#   target_link_libraries(tools.unspent_db ${PROJECT_NAME})

#   add_executable(tools.build_utxo
#     tools/build_utxo/build_utxo.cpp)
#   target_link_libraries(tools.build_utxo ${PROJECT_NAME})
#   _group_sources(tools.transaction_db "${CMAKE_CURRENT_LIST_DIR}/tools/build_utxo")

#   add_executable(tools.check_scripts
#     tools/check_scripts/check_scripts.cpp)
#   target_link_libraries(tools.check_scripts ${PROJECT_NAME})
#   _group_sources(tools.transaction_db "${CMAKE_CURRENT_LIST_DIR}/tools/check_scripts")

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


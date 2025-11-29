# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(domain VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_EXAMPLES "Compile with examples." OFF)
option(WITH_ICU "Compile with International Components for Unicode." OFF)
option(WITH_PNG "Compile with Libpng support." OFF)
option(WITH_QRENCODE "Compile with QREncode." OFF)
option(JUST_KTH_SOURCES "Just Knuth source code to be linted." OFF)
option(WITH_CONSOLE "Compile console application." OFF)

option(GLOBAL_BUILD "" ON)

if (WITH_ICU)
  add_definitions(-DWITH_ICU -DBOOST_HAS_ICU)
endif()

if (WITH_PNG)
  add_definitions(-DWITH_PNG)
endif()

if (WITH_QRENCODE)
  add_definitions(-DWITH_QRENCODE)
endif()

add_definitions(-DJEMALLOC)


if (JUST_KTH_SOURCES)
  message(STATUS "Knuth: Just Knuth source code to be linted: enabled")
endif()


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

set(Boost_DEBUG ON)

find_package(Boost 1.81.0 COMPONENTS program_options REQUIRED)
find_package(Boost 1.81.0 COMPONENTS thread REQUIRED)

find_package(Threads REQUIRED)

if (NOT GLOBAL_BUILD)
  find_package(infrastructure REQUIRED)
endif()

if (${CURRENCY} STREQUAL "LTC")
  find_package(OpenSSL 1.0.1 REQUIRED)
endif()

if (WITH_ICU)
  find_package(ICU REQUIRED)
endif()

find_package(tiny-aes-c REQUIRED)
# # jemalloc
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(JEMALLOC jemalloc)

# message("-- Found jemalloc: JEMALLOC_INCLUDE_DIRS: ${JEMALLOC_INCLUDE_DIRS}")
# message("-- Found jemalloc: JEMALLOC_LIBRARY_DIRS: ${JEMALLOC_LIBRARY_DIRS}")
# message("-- Found jemalloc: JEMALLOC_CFLAGS_OTHER: ${JEMALLOC_CFLAGS_OTHER}")

# include_directories(${JEMALLOC_INCLUDE_DIRS})
# link_directories(${JEMALLOC_LIBRARY_DIRS})
# add_definitions(${JEMALLOC_CFLAGS_OTHER})




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

if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
endif()

if(ENABLE_USAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
endif()

if(ENABLE_TSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
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

set(MODE STATIC)
if (ENABLE_SHARED)
  set(MODE SHARED)
endif()

set(kth_sources_just_legacy
        src/chain/block_basis.cpp
        src/chain/block.cpp
        src/chain/chain_state.cpp
        src/chain/compact.cpp
        src/chain/header_basis.cpp
        src/chain/header.cpp
        src/chain/input_basis.cpp
        src/chain/input.cpp
        src/chain/output_basis.cpp
        src/chain/output.cpp
        src/chain/output_point.cpp
        src/chain/point.cpp
        src/chain/point_iterator.cpp
        src/chain/point_value.cpp
        src/chain/points_value.cpp
        src/chain/script_basis.cpp
        src/chain/script.cpp
        src/chain/transaction_basis.cpp
        src/chain/transaction.cpp
        src/chain/utxo.cpp

        src/machine/interpreter.cpp

        src/machine/opcode.cpp
        src/machine/operation.cpp
        src/machine/program.cpp
        src/machine/script_execution_context.cpp

        src/config/header.cpp
        src/config/input.cpp
        src/config/output.cpp
        src/config/point.cpp
        src/config/transaction.cpp
        src/config/script.cpp
        src/config/ec_private.cpp
        src/config/endorsement.cpp

        src/utility/property_tree.cpp

        src/multi_crypto_support.cpp
        src/version.cpp

        src/math/stealth.cpp
        src/math/external/scrypt.h

        src/message/address.cpp
        src/message/alert.cpp
        src/message/alert_payload.cpp
        src/message/block.cpp
        src/message/block_transactions.cpp
        src/message/compact_block.cpp
        src/message/double_spend_proof.cpp
        src/message/fee_filter.cpp
        src/message/filter_add.cpp
        src/message/filter_clear.cpp
        src/message/filter_load.cpp
        src/message/get_address.cpp
        src/message/get_block_transactions.cpp

        src/message/get_data.cpp
        src/message/get_headers.cpp
        src/message/header.cpp
        src/message/headers.cpp
        src/message/heading.cpp
        src/message/inventory.cpp
        src/message/inventory_vector.cpp
        src/message/memory_pool.cpp
        src/message/merkle_block.cpp
        src/message/not_found.cpp
        src/message/ping.cpp
        src/message/pong.cpp
        src/message/prefilled_transaction.cpp
        src/message/reject.cpp
        src/message/send_compact.cpp
        src/message/send_headers.cpp
        src/message/transaction.cpp
        src/message/verack.cpp
        src/message/version.cpp
        src/message/xverack.cpp
        src/message/xversion.cpp

        src/wallet/bitcoin_uri.cpp
        src/wallet/ec_private.cpp
        src/wallet/ec_public.cpp
        src/wallet/ek_private.cpp
        src/wallet/ek_public.cpp
        src/wallet/ek_token.cpp
        src/wallet/encrypted_keys.cpp
        src/wallet/message.cpp
        src/wallet/parse_encrypted_keys/parse_encrypted_key.hpp
        src/wallet/parse_encrypted_keys/parse_encrypted_key.ipp
        src/wallet/parse_encrypted_keys/parse_encrypted_prefix.hpp
        src/wallet/parse_encrypted_keys/parse_encrypted_prefix.ipp
        src/wallet/parse_encrypted_keys/parse_encrypted_private.cpp
        src/wallet/parse_encrypted_keys/parse_encrypted_private.hpp
        src/wallet/parse_encrypted_keys/parse_encrypted_public.cpp
        src/wallet/parse_encrypted_keys/parse_encrypted_public.hpp
        src/wallet/parse_encrypted_keys/parse_encrypted_token.cpp
        src/wallet/parse_encrypted_keys/parse_encrypted_token.hpp
        src/wallet/payment_address.cpp
        src/wallet/select_outputs.cpp
        src/wallet/select_outputs.cpp
        src/wallet/stealth_address.cpp
        src/wallet/stealth_receiver.cpp
        src/wallet/stealth_sender.cpp
        src/wallet/wallet_manager.cpp
)

set(kth_sources_just_legacy
  ${kth_sources_just_legacy}
  src/message/get_blocks.cpp
)

set(kth_sources_just_kth
    # src/wallet/transaction_functions.cpp
)

if (${CURRENCY} STREQUAL "LTC")
    set(kth_sources_just_kth
      ${kth_sources_just_kth}
      src/math/hash.cpp
      src/math/external/scrypt.cpp
      src/math/external/scrypt-sse2.cpp)
endif()

if (NOT JUST_KTH_SOURCES)
  set(kth_sources
    ${kth_sources_just_legacy}
  )
endif()

set(kth_sources ${kth_sources} ${kth_sources_just_kth})

set(kth_headers
    include/kth/domain/concepts_undef.hpp
    include/kth/domain/message/address.hpp
    include/kth/domain/message/xversion.hpp
    include/kth/domain/message/get_data.hpp
    include/kth/domain/message/headers.hpp
    include/kth/domain/message/messages.hpp
    include/kth/domain/message/heading.hpp
    include/kth/domain/message/get_headers.hpp
    include/kth/domain/message/block.hpp
    include/kth/domain/message/not_found.hpp
    include/kth/domain/message/merkle_block.hpp
    include/kth/domain/message/memory_pool.hpp
    include/kth/domain/message/filter_clear.hpp
    include/kth/domain/message/fee_filter.hpp
    include/kth/domain/message/filter_load.hpp
    include/kth/domain/message/alert_payload.hpp
    include/kth/domain/message/prefilled_transaction.hpp
    include/kth/domain/message/send_compact.hpp
    include/kth/domain/message/xverack.hpp
    include/kth/domain/message/get_blocks.hpp
    include/kth/domain/message/header.hpp
    include/kth/domain/message/inventory_vector.hpp
    include/kth/domain/message/verack.hpp
    include/kth/domain/message/ping.hpp
    include/kth/domain/message/pong.hpp
    include/kth/domain/message/double_spend_proof.hpp
    include/kth/domain/message/send_headers.hpp
    include/kth/domain/message/transaction.hpp
    include/kth/domain/message/reject.hpp
    include/kth/domain/message/compact_block.hpp
    include/kth/domain/message/get_block_transactions.hpp
    include/kth/domain/message/alert.hpp
    include/kth/domain/message/filter_add.hpp
    include/kth/domain/message/get_address.hpp
    include/kth/domain/message/inventory.hpp
    include/kth/domain/message/block_transactions.hpp
    include/kth/domain/message/version.hpp
    include/kth/domain/constants/bch.hpp
    include/kth/domain/constants/btc.hpp
    include/kth/domain/constants/functions.hpp
    include/kth/domain/constants/ltc.hpp
    include/kth/domain/constants/common.hpp
    include/kth/domain/constants/segwit.hpp
    include/kth/domain/constants/bch_btc.hpp
    include/kth/domain/concepts.hpp
    include/kth/domain/chain/points_value.hpp
    include/kth/domain/chain/chain_state.hpp
    include/kth/domain/chain/header_basis.hpp
    include/kth/domain/chain/block_basis.hpp
    include/kth/domain/chain/input_point.hpp
    include/kth/domain/chain/input_basis.hpp
    include/kth/domain/chain/block.hpp
    include/kth/domain/chain/output.hpp
    include/kth/domain/chain/daa/aserti3_2d.hpp
    include/kth/domain/chain/token_data.hpp
    include/kth/domain/chain/token_data_serialization.hpp
    include/kth/domain/chain/output_point.hpp
    include/kth/domain/chain/hash_memoizer.hpp
    include/kth/domain/chain/script_basis.hpp
    include/kth/domain/chain/transaction_basis.hpp
    include/kth/domain/chain/stealth.hpp
    include/kth/domain/chain/point_iterator.hpp
    include/kth/domain/chain/header.hpp
    include/kth/domain/chain/history.hpp
    include/kth/domain/chain/compact.hpp
    include/kth/domain/chain/input.hpp
    include/kth/domain/chain/script.hpp
    include/kth/domain/chain/transaction.hpp
    include/kth/domain/chain/point.hpp
    include/kth/domain/chain/output_basis.hpp
    include/kth/domain/chain/point_value.hpp
    include/kth/domain/chain/utxo.hpp
    include/kth/domain/define.hpp
    include/kth/domain/multi_crypto_support.hpp
    include/kth/domain/constants.hpp

    include/kth/domain/wallet/stealth_address.hpp
    # include/kth/domain/wallet/transaction_functions.hpp
    include/kth/domain/wallet/bitcoin_uri.hpp
    include/kth/domain/wallet/ec_private.hpp
    include/kth/domain/wallet/stealth_receiver.hpp
    include/kth/domain/wallet/stealth_sender.hpp
    include/kth/domain/wallet/message.hpp
    include/kth/domain/wallet/ek_private.hpp
    include/kth/domain/wallet/ek_public.hpp
    include/kth/domain/wallet/ec_public.hpp
    include/kth/domain/wallet/ek_token.hpp
    include/kth/domain/wallet/payment_address.hpp
    include/kth/domain/wallet/select_outputs.hpp
    include/kth/domain/wallet/uri_reader.hpp
    include/kth/domain/wallet/encrypted_keys.hpp
    include/kth/domain/wallet/wallet_manager.hpp

    include/kth/domain/machine/opcode.hpp
    include/kth/domain/machine/operation.hpp
    include/kth/domain/machine/interpreter.hpp
    include/kth/domain/machine/program.hpp
    include/kth/domain/machine/rule_fork.hpp
    include/kth/domain/math/limits.hpp
    include/kth/domain/math/stealth.hpp
    include/kth/domain/utility/property_tree.hpp
    include/kth/domain/impl/machine
    include/kth/domain/impl/machine/program.ipp
    include/kth/domain/impl/machine/interpreter.ipp
    include/kth/domain/impl/machine/operation.ipp
    include/kth/domain/impl/utility
    include/kth/domain/impl/utility/property_tree.ipp
    include/kth/domain/config/ec_private.hpp
    include/kth/domain/config/output.hpp
    include/kth/domain/config/network.hpp
    include/kth/domain/config/header.hpp
    include/kth/domain/config/input.hpp
    include/kth/domain/config/script.hpp
    include/kth/domain/config/transaction.hpp
    include/kth/domain/config/point.hpp
    include/kth/domain/config/parser.hpp
    include/kth/domain/config/endorsement.hpp
    include/kth/domain/version.hpp
    include/kth/domain.hpp
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
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DKD_STATIC)
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${Boost_INCLUDE_DIR})

if (WITH_ICU)
  target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${ICU_INCLUDE_DIR})
endif()

if (${CURRENCY} STREQUAL "LTC")
  target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${OPENSSL_INCLUDE_DIR})
endif()


target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

if (MINGW)
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 wsock32)
endif()

if (${CURRENCY} STREQUAL "LTC")
  target_link_libraries(${PROJECT_NAME} PUBLIC ${OPENSSL_CRYPTO_LIBRARY})
endif()

if (WITH_ICU)
  target_link_libraries(${PROJECT_NAME} PUBLIC ICU::icuuc)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC infrastructure::infrastructure)
target_link_libraries(${PROJECT_NAME} PUBLIC tiny-aes-c::tiny-aes-c)
target_link_libraries(${PROJECT_NAME} PUBLIC ${JEMALLOC_LIBRARIES})

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_link_libraries(${PROJECT_NAME} PUBLIC Boost::thread)
else()
  target_compile_options(${PROJECT_NAME} PUBLIC "-pthread")
  target_link_options(${PROJECT_NAME} PUBLIC "-pthread")
  set_target_properties(${PROJECT_NAME} PROPERTIES
                          COMPILE_FLAGS "-pthread"
                          LINK_FLAGS "-pthread")
endif()


_group_sources(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}")

# Console
# ------------------------------------------------------------------------------
message(STATUS "Knuth: WITH_CONSOLE ${WITH_CONSOLE}")
if (WITH_CONSOLE)
    set(_test_console_sources
      console/main.cpp
    )

  add_executable(kth_domain_console ${_test_console_sources})

  target_link_libraries(kth_domain_console ${PROJECT_NAME})

  set_target_properties(
          kth_domain_console PROPERTIES
          FOLDER "domain"
          OUTPUT_NAME kth_domain_console)


  # Enable sanitizers and other debug options only in debug mode
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
      message(STATUS "Activating sanitizers and debug options")
      message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
      message(STATUS "APPLE: ${APPLE}")

      if (APPLE)
          # In macOS, use only AddressSanitizer and UndefinedBehaviorSanitizer
          target_compile_options(kth_domain_console PRIVATE
              -fsanitize=address,undefined
              -fno-omit-frame-pointer
              -g
          )
          target_link_options(kth_domain_console PRIVATE
              -fsanitize=address,undefined
          )
      else()
          # In other operating systems (like Linux), add LeakSanitizer also
          target_compile_options(kth_domain_console PRIVATE
              -fsanitize=address,undefined,leak
              -fno-omit-frame-pointer
              -g
          )
          target_link_options(kth_domain_console PRIVATE
              -fsanitize=address,undefined,leak
          )
      endif()

  endif()
endif()

# Tests
# ------------------------------------------------------------------------------
# Skip tests for WebAssembly (Catch2 incompatible with shared-memory/threads)
if (ENABLE_TEST AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  enable_testing()
  find_package(Catch2 3 REQUIRED)
  add_executable(kth_domain_test
        test/chain/block.cpp
        test/chain/compact.cpp
        test/chain/header.cpp
        test/chain/input.cpp
        test/chain/output.cpp
        test/chain/output_point.cpp
        test/chain/point.cpp
        test/chain/point_iterator.cpp
        test/chain/point_value.cpp
        test/chain/points_value.cpp
        test/chain/satoshi_words.cpp
        test/chain/script.cpp
        test/chain/transaction.cpp

        test/main.cpp

        test/machine/opcode.cpp
        test/machine/operation.cpp

        test/math/limits.cpp
        test/math/stealth.cpp

        test/message/address.cpp
        test/message/alert.cpp
        test/message/alert_payload.cpp
        test/message/block.cpp
        test/message/block_transactions.cpp
        test/message/compact_block.cpp
        test/message/fee_filter.cpp
        test/message/filter_add.cpp
        test/message/filter_clear.cpp
        test/message/filter_load.cpp
        test/message/get_address.cpp
        test/message/get_block_transactions.cpp
        test/message/get_blocks.cpp
        test/message/get_data.cpp
        test/message/get_headers.cpp
        test/message/header.cpp
        test/message/headers.cpp
        test/message/heading.cpp
        test/message/inventory.cpp
        test/message/inventory_vector.cpp
        test/message/memory_pool.cpp
        test/message/merkle_block.cpp
        test/message/messages.cpp
        test/message/not_found.cpp
        test/message/ping.cpp
        test/message/pong.cpp
        test/message/prefilled_transaction.cpp
        test/message/reject.cpp
        test/message/send_compact.cpp
        test/message/send_headers.cpp
        test/message/transaction.cpp
        test/message/verack.cpp
        test/message/version.cpp

        test/wallet/bitcoin_uri.cpp
        test/wallet/ec_private.cpp
        test/wallet/ec_public.cpp
        test/wallet/encrypted_keys.cpp
        test/wallet/message.cpp
        test/wallet/payment_address.cpp
        test/wallet/select_outputs.cpp
        test/wallet/stealth_address.cpp
        test/wallet/stealth_receiver.cpp
        test/wallet/stealth_sender.cpp
        test/wallet/uri_reader.cpp
        # test/wallet/wallet_manager.cpp  # File not found

        #TODO(fernando): to include
        # test/wallet/transaction_functions.cpp
    )

  target_include_directories(kth_domain_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)

  target_link_libraries(kth_domain_test PUBLIC ${PROJECT_NAME})
  target_link_libraries(kth_domain_test PRIVATE Catch2::Catch2WithMain)

  include(CTest)
  # Try to use Catch2 automatic test discovery
  find_package(Catch2 QUIET)
  if(Catch2_FOUND)
    include(Catch)
    catch_discover_tests(kth_domain_test)
  else()
    # Fallback to manual test registration
    add_test(NAME kth_domain_test COMMAND kth_domain_test)
  endif()
endif()

# Examples
# ------------------------------------------------------------------------------
if (WITH_EXAMPLES)
  add_executable(kth_domain_examples
    examples/main.cpp)

  target_link_libraries(kth_domain_examples PUBLIC ${PROJECT_NAME})

  _group_sources(kth_domain_examples "${CMAKE_CURRENT_LIST_DIR}/examples")
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


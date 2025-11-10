# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(infrastructure VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(GLOBAL_BUILD "" ON)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_EXAMPLES "Compile with examples." OFF)
option(WITH_ICU "Compile with International Components for Unicode." OFF)
option(WITH_PNG "Compile with Libpng support." OFF)
option(WITH_QRENCODE "Compile with QREncode." OFF)
option(JUST_KTH_SOURCES "Just Knuth source code to be linted." OFF)


if (WITH_ICU)
  add_definitions(-DWITH_ICU -DBOOST_HAS_ICU)
endif()

if (WITH_PNG)
  add_definitions(-DWITH_PNG)
endif()

if (WITH_QRENCODE)
  add_definitions(-DWITH_QRENCODE)
endif()


if (JUST_KTH_SOURCES)
  message(STATUS "Knuth: Just Knuth source code to be linted: enabled")
endif()


set(MICROARCHITECTURE "x86_64" CACHE STRING "Specify the Microarchitecture (x86_64|...).")
message( STATUS "Knuth: Compiling for ${MICROARCHITECTURE}")

set(MARCH_ID "" CACHE STRING "Specify the Microarchitecture ID (x86_64|...).")
message( STATUS "Knuth: Compiling for Microarchitecture ID ${MARCH_ID}")

message( STATUS "Knuth: CONAN_CXX_FLAGS ${CONAN_CXX_FLAGS}")
message( STATUS "Knuth: CONAN_C_FLAGS ${CONAN_C_FLAGS}")

set(Boost_DEBUG ON)


find_package(Threads REQUIRED)

if (NOT GLOBAL_BUILD)
  find_package(secp256k1 REQUIRED)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  find_package(Boost COMPONENTS program_options REQUIRED)
  find_package(Boost COMPONENTS thread REQUIRED)
else()
  find_package(Boost REQUIRED)
endif()

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(ctre REQUIRED)

if (WITH_ICU)
  find_package(ICU REQUIRED)
endif()


include(CheckCXXCompilerFlag)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)
include(KnuthTools)

# Find simdutf for SIMD-optimized base64 encoding (not available for WebAssembly)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  find_package(simdutf REQUIRED)
endif()

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

set(MODE STATIC)
if (ENABLE_SHARED)
  set(MODE SHARED)
endif()

set(kth_sources_just_legacy
        src/machine/number.cpp

        src/config/authority.cpp
        src/config/base16.cpp
        src/config/base2.cpp
        src/config/base58.cpp
        src/config/base64.cpp
        src/config/checkpoint.cpp
        src/config/directory.cpp
        src/config/endpoint.cpp
        src/config/hash160.cpp
        src/config/hash256.cpp


        src/config/sodium.cpp

        src/error.cpp
        src/version.cpp

        src/formats/base_10.cpp
        src/formats/base_16.cpp
        src/formats/base_58.cpp
        src/formats/base_64.cpp
        src/formats/base_85.cpp

        src/math/checksum.cpp
        src/math/crypto.cpp
        src/math/elliptic_curve.cpp
        src/math/hash.cpp
        src/math/secp256k1_initializer.cpp
        src/math/secp256k1_initializer.hpp
        src/math/sip_hash.cpp

        src/math/external/aes256.h
        src/math/external/crypto_scrypt.h
        src/math/external/hmac_sha256.h
        src/math/external/hmac_sha512.h
        src/math/external/lax_der_parsing.h
        src/math/external/pbkdf2_sha256.h
        src/math/external/pkcs5_pbkdf2.h
        src/math/external/ripemd160.h
        # src/math/external/scrypt.h
        src/math/external/sha1.h
        src/math/external/sha256.h
        src/math/external/sha512.h
        src/math/external/zeroize.h
        src/math/external/aes256.c
        src/math/external/crypto_scrypt.c
        src/math/external/hmac_sha256.c
        src/math/external/hmac_sha512.c
        src/math/external/lax_der_parsing.c
        src/math/external/pbkdf2_sha256.c
        src/math/external/pkcs5_pbkdf2.c
        src/math/external/ripemd160.c
        src/math/external/sha1.c
        src/math/external/sha256.c
        src/math/external/sha512.c
        src/math/external/zeroize.c

        src/message/message_tools.cpp
        src/message/messages.cpp
        src/message/network_address.cpp

        src/unicode/console_streambuf.cpp
        src/unicode/ifstream.cpp
        src/unicode/ofstream.cpp
        src/unicode/unicode.cpp
        src/unicode/unicode_istream.cpp
        src/unicode/unicode_ostream.cpp
        src/unicode/unicode_streambuf.cpp

        src/utility/binary.cpp
        src/utility/conditional_lock.cpp
        src/utility/dispatcher.cpp
        src/utility/flush_lock.cpp
        src/utility/interprocess_lock.cpp
        src/utility/istream_reader.cpp
        src/utility/monitor.cpp
        src/utility/ostream_writer.cpp
        src/utility/prioritized_mutex.cpp
        src/utility/scope_lock.cpp

        src/utility/sequential_lock.cpp
        src/utility/string.cpp
        src/utility/thread.cpp

        src/wallet/dictionary.cpp
        src/wallet/hd_private.cpp
        src/wallet/hd_public.cpp
        src/wallet/mini_keys.cpp
        src/wallet/mnemonic.cpp

        src/wallet/uri.cpp

        src/utility/pseudo_random.cpp
    )

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(kth_sources_just_legacy
    ${kth_sources_just_legacy}

    src/config/parameter.cpp
    src/config/printer.cpp

    src/utility/deadline.cpp
    src/utility/sequencer.cpp
    src/utility/socket.cpp

    src/utility/pseudo_random_broken_do_not_use.cpp

    src/utility/threadpool.cpp

    src/utility/work.cpp
  )
endif()


set(kth_sources_just_legacy
  ${kth_sources_just_legacy}
  src/log/sink.cpp
)

if (WITH_PNG)
  set(kth_sources_just_legacy
    ${kth_sources_just_legacy}
    src/utility/png.cpp
  )
endif()

if (WITH_QRENCODE)
  set(kth_sources_just_legacy
    ${kth_sources_just_legacy}
    src/wallet/qrcode.cpp
  )
endif()

set(kth_sources_just_knuth
    # src/math/external/scrypt.cpp
    # src/math/external/scrypt-sse2.cpp
    src/wallet/cashaddr.cpp
)

if (NOT JUST_KTH_SOURCES)
  set(kth_sources
    ${kth_sources_just_legacy}
  )
endif()

set(kth_sources ${kth_sources} ${kth_sources_just_knuth})

set(kth_headers
    include/kth/infrastructure/compat.h
    include/kth/infrastructure/compat.hpp
    include/kth/infrastructure/constants.hpp
    # include/kth/infrastructure/concepts.hpp
    include/kth/infrastructure/define.hpp
    include/kth/infrastructure/error.hpp
    include/kth/infrastructure/handlers.hpp
    include/kth/infrastructure/path.hpp
    include/kth/infrastructure/version.hpp

    include/kth/infrastructure/machine/number.hpp
    include/kth/infrastructure/machine/script_pattern.hpp
    include/kth/infrastructure/machine/sighash_algorithm.hpp
    include/kth/infrastructure/machine/script_version.hpp

    include/kth/infrastructure/config/authority.hpp
    include/kth/infrastructure/config/base2.hpp
    include/kth/infrastructure/config/base16.hpp
    include/kth/infrastructure/config/base58.hpp
    include/kth/infrastructure/config/base64.hpp
    include/kth/infrastructure/config/checkpoint.hpp
    include/kth/infrastructure/config/directory.hpp
    include/kth/infrastructure/config/endpoint.hpp
    include/kth/infrastructure/config/hash160.hpp
    include/kth/infrastructure/config/hash256.hpp
    include/kth/infrastructure/config/parameter.hpp
    include/kth/infrastructure/config/printer.hpp
    include/kth/infrastructure/config/sodium.hpp

    include/kth/infrastructure/formats/base_10.hpp
    include/kth/infrastructure/formats/base_16.hpp
    include/kth/infrastructure/formats/base_58.hpp
    include/kth/infrastructure/formats/base_64.hpp
    include/kth/infrastructure/formats/base_85.hpp

    include/kth/infrastructure/impl/formats/base_16.ipp
    include/kth/infrastructure/impl/formats/base_58.ipp

    include/kth/infrastructure/impl/math/checksum.ipp
    include/kth/infrastructure/impl/math/hash.ipp

    include/kth/infrastructure/impl/machine/number.ipp

    include/kth/infrastructure/math/checksum.hpp
    include/kth/infrastructure/math/crypto.hpp
    include/kth/infrastructure/math/elliptic_curve.hpp
    include/kth/infrastructure/math/hash.hpp
    include/kth/infrastructure/math/sip_hash.hpp
    include/kth/infrastructure/math/uint256.hpp

    include/kth/infrastructure/impl/utility/collection.ipp
    include/kth/infrastructure/impl/utility/data.ipp
    include/kth/infrastructure/impl/utility/deserializer.ipp
    include/kth/infrastructure/impl/utility/endian.ipp
    include/kth/infrastructure/impl/utility/istream_reader.ipp
    include/kth/infrastructure/impl/utility/ostream_writer.ipp
    include/kth/infrastructure/impl/utility/pending.ipp
    include/kth/infrastructure/impl/utility/resubscriber.ipp
    include/kth/infrastructure/impl/utility/serializer.ipp
    include/kth/infrastructure/impl/utility/subscriber.ipp
    include/kth/infrastructure/impl/utility/string.ipp
    include/kth/infrastructure/impl/utility/track.ipp

    include/kth/infrastructure/message/message_tools.hpp

    include/kth/infrastructure/unicode/console_streambuf.hpp
    include/kth/infrastructure/unicode/file_lock.hpp
    include/kth/infrastructure/unicode/ifstream.hpp
    include/kth/infrastructure/unicode/ofstream.hpp
    include/kth/infrastructure/unicode/unicode.hpp
    include/kth/infrastructure/unicode/unicode_istream.hpp
    include/kth/infrastructure/unicode/unicode_ostream.hpp
    include/kth/infrastructure/unicode/unicode_streambuf.hpp

    include/kth/infrastructure/utility/asio.hpp
    include/kth/infrastructure/utility/assert.hpp
    include/kth/infrastructure/utility/atomic.hpp
    include/kth/infrastructure/utility/binary.hpp
    # include/kth/infrastructure/utility/byte_reader.hpp
    include/kth/infrastructure/utility/collection.hpp
    include/kth/infrastructure/utility/color.hpp
    include/kth/infrastructure/utility/conditional_lock.hpp
    include/kth/infrastructure/utility/container_sink.hpp
    include/kth/infrastructure/utility/container_source.hpp
    include/kth/infrastructure/utility/data.hpp
    include/kth/infrastructure/utility/deadline.hpp
    include/kth/infrastructure/utility/decorator.hpp
    include/kth/infrastructure/utility/delegates.hpp
    include/kth/infrastructure/utility/deserializer.hpp
    include/kth/infrastructure/utility/dispatcher.hpp
    include/kth/infrastructure/utility/enable_shared_from_base.hpp
    include/kth/infrastructure/utility/endian.hpp
    include/kth/infrastructure/utility/exceptions.hpp
    include/kth/infrastructure/utility/flush_lock.hpp
    include/kth/infrastructure/utility/interprocess_lock.hpp
    include/kth/infrastructure/utility/istream_reader.hpp
    include/kth/infrastructure/utility/monitor.hpp
    include/kth/infrastructure/utility/noncopyable.hpp
    include/kth/infrastructure/utility/ostream_writer.hpp
    include/kth/infrastructure/utility/pending.hpp
    include/kth/infrastructure/utility/png.hpp
    include/kth/infrastructure/utility/prioritized_mutex.hpp

    include/kth/infrastructure/utility/pseudo_random.hpp
    include/kth/infrastructure/utility/pseudo_random_broken_do_not_use.hpp


    include/kth/infrastructure/utility/reader.hpp
    include/kth/infrastructure/utility/resubscriber.hpp
    include/kth/infrastructure/utility/scope_lock.hpp
    include/kth/infrastructure/utility/sequencer.hpp
    include/kth/infrastructure/utility/sequential_lock.hpp
    include/kth/infrastructure/utility/serializer.hpp
    include/kth/infrastructure/utility/socket.hpp
    include/kth/infrastructure/utility/string.hpp
    include/kth/infrastructure/utility/subscriber.hpp
    include/kth/infrastructure/utility/synchronizer.hpp
    include/kth/infrastructure/utility/thread.hpp
    include/kth/infrastructure/utility/threadpool.hpp
    include/kth/infrastructure/utility/timer.hpp
    include/kth/infrastructure/utility/track.hpp
    include/kth/infrastructure/utility/work.hpp
    include/kth/infrastructure/utility/writer.hpp

    include/kth/infrastructure/wallet/cashaddr.hpp
    include/kth/infrastructure/wallet/dictionary.hpp
    include/kth/infrastructure/wallet/hd_private.hpp
    include/kth/infrastructure/wallet/hd_public.hpp
    include/kth/infrastructure/wallet/mini_keys.hpp
    include/kth/infrastructure/wallet/mnemonic.hpp
    include/kth/infrastructure/wallet/qrcode.hpp
    include/kth/infrastructure/wallet/uri.hpp

    include/kth/infrastructure.hpp
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
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DKI_STATIC)
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${Boost_INCLUDE_DIR})

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_link_libraries(${PROJECT_NAME} PUBLIC Boost::program_options)
  target_link_libraries(${PROJECT_NAME} PUBLIC Boost::thread)
else()
  target_compile_options(${PROJECT_NAME} PUBLIC "-pthread")
  target_link_options(${PROJECT_NAME} PUBLIC "-pthread")
  set_target_properties(${PROJECT_NAME} PROPERTIES
                          COMPILE_FLAGS "-pthread"
                          LINK_FLAGS "-pthread")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC fmt::fmt-header-only)
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog_header_only)
target_link_libraries(${PROJECT_NAME} PUBLIC secp256k1::secp256k1)
target_link_libraries(${PROJECT_NAME} PUBLIC ctre::ctre)

# Link simdutf for SIMD-optimized base64 encoding (not available for WebAssembly)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_link_libraries(${PROJECT_NAME} PUBLIC simdutf::simdutf)
endif()

if (WITH_ICU)
  target_link_libraries(${PROJECT_NAME} PUBLIC ICU::icuuc)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

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
  find_package(nanobench REQUIRED)

  set(kth_test_sources
    test/config/authority.cpp
    test/config/base58.cpp
    test/config/checkpoint.cpp
    test/config/endpoint.cpp
    test/config/hash256.cpp


    test/formats/base_10.cpp
    test/formats/base_16.cpp
    test/formats/base_58.cpp
    test/formats/base_64.cpp
    test/formats/base_85.cpp

    test/log/log.cpp

    test/machine/number.cpp

    test/math/checksum.cpp
    test/math/elliptic_curve.cpp
    test/math/hash.cpp
    test/math/uint256.cpp

    test/network_address.cpp

    test/unicode/unicode.cpp
    test/unicode/unicode_istream.cpp
    test/unicode/unicode_ostream.cpp

    test/utility/binary.cpp
    test/utility/collection.cpp
    test/utility/data.cpp
    test/utility/endian.cpp
    test/utility/operators.cpp
    test/utility/serializer.cpp
    test/utility/stream.cpp
    test/utility/thread.cpp

    test/wallet/hd_private.cpp
    test/wallet/hd_public.cpp
    test/wallet/mnemonic.cpp
    test/wallet/uri.cpp

    # test/utility/pseudo_random.cpp
  )

  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(kth_test_sources
      ${kth_test_sources}
      test/config/parameter.cpp
      test/config/printer.cpp
      test/utility/pseudo_random_broken_do_not_use.cpp
    )
  endif()

  if (WITH_PNG)
    set(kth_test_sources
        ${kth_test_sources}
        src/utility/png.cpp
    )
  endif()

  if (WITH_QRENCODE)
    set(kth_test_sources
        ${kth_test_sources}
        src/wallet/qrcode.cpp
    )
  endif()

  add_executable(kth_infrastructure_test ${kth_test_sources})

  target_include_directories(kth_infrastructure_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)

  target_link_libraries(kth_infrastructure_test PRIVATE ${PROJECT_NAME})
  target_link_libraries(kth_infrastructure_test PRIVATE Catch2::Catch2WithMain)

  include(CTest)
  # Try to use Catch2 automatic test discovery
  find_package(Catch2 QUIET)
  if(Catch2_FOUND)
    include(Catch)
    catch_discover_tests(kth_infrastructure_test)
  else()
    # Fallback to manual test registration
    add_test(NAME kth_infrastructure_test COMMAND kth_infrastructure_test)
  endif()

  _group_sources(kth_infrastructure_test "${CMAKE_CURRENT_LIST_DIR}/test")

  # Benchmark executable (separate from tests)
  add_executable(kth_infrastructure_benchmarks test/formats/base_encoding_benchmarks.cpp)
  target_include_directories(kth_infrastructure_benchmarks PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)
  target_link_libraries(kth_infrastructure_benchmarks PRIVATE ${PROJECT_NAME})
  target_link_libraries(kth_infrastructure_benchmarks PRIVATE nanobench::nanobench)

  _group_sources(kth_infrastructure_benchmarks "${CMAKE_CURRENT_LIST_DIR}/test")
endif()

# Examples
# ------------------------------------------------------------------------------
if (WITH_EXAMPLES)
  add_executable(kth_infrastructure_examples
    examples/main.cpp)

  target_link_libraries(kth_infrastructure_examples PUBLIC ${PROJECT_NAME})

  _group_sources(kth_infrastructure_examples "${CMAKE_CURRENT_LIST_DIR}/examples")
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


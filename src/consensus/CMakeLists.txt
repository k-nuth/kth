# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(consensus VERSION 0 LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SHARED "" OFF)
option(GLOBAL_BUILD "" ON)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
option(WITH_JAVA "Compile the Java bindings." OFF)
option(WITH_PYTHON "Compile the Python bindings." OFF)

if (WITH_JAVA)
  message(FATAL_ERROR "WITH_JAVA not yet implemented")
endif()

if (WITH_PYTHON)
  message(FATAL_ERROR "WITH_PYTHON not yet implemented")
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

# Set preprocessor defines.
include(CheckIncludeFile)
include(CheckCXXSymbolExists)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)
include(KnuthTools)

_check_has_decl(byteswap.h HAVE_BYTESWAP_H bswap_16 HAVE_DECL_BSWAP_16)
_check_has_decl(byteswap.h HAVE_BYTESWAP_H bswap_32 HAVE_DECL_BSWAP_32)
_check_has_decl(byteswap.h HAVE_BYTESWAP_H bswap_64 HAVE_DECL_BSWAP_64)
_check_has_decl(endian.h HAVE_ENDIAN_H le16toh HAVE_DECL_LE16TOH)
_check_has_decl(endian.h HAVE_ENDIAN_H le32toh HAVE_DECL_LE32TOH)
_check_has_decl(endian.h HAVE_ENDIAN_H le64toh HAVE_DECL_LE64TOH)
_check_has_decl(endian.h HAVE_ENDIAN_H htole16 HAVE_DECL_HTOLE16)
_check_has_decl(endian.h HAVE_ENDIAN_H htole32 HAVE_DECL_HTOLE32)
_check_has_decl(endian.h HAVE_ENDIAN_H htole64 HAVE_DECL_HTOLE64)
_check_has_decl(endian.h HAVE_ENDIAN_H be16toh HAVE_DECL_BE16TOH)
_check_has_decl(endian.h HAVE_ENDIAN_H be32toh HAVE_DECL_BE32TOH)
_check_has_decl(endian.h HAVE_ENDIAN_H be64toh HAVE_DECL_BE64TOH)
_check_has_decl(endian.h HAVE_ENDIAN_H htobe16 HAVE_DECL_HTOBE16)
_check_has_decl(endian.h HAVE_ENDIAN_H htobe32 HAVE_DECL_HTOBE32)
_check_has_decl(endian.h HAVE_ENDIAN_H htobe64 HAVE_DECL_HTOBE64)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H le16toh HAVE_DECL_LE16TOH)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H le32toh HAVE_DECL_LE32TOH)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H le64toh HAVE_DECL_LE64TOH)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H htole16 HAVE_DECL_HTOLE16)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H htole32 HAVE_DECL_HTOLE32)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H htole64 HAVE_DECL_HTOLE64)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H be16toh HAVE_DECL_BE16TOH)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H be32toh HAVE_DECL_BE32TOH)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H be64toh HAVE_DECL_BE64TOH)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H htobe16 HAVE_DECL_HTOBE16)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H htobe32 HAVE_DECL_HTOBE32)
_check_has_decl(sys/endian.h HAVE_SYS_ENDIAN_H htobe64 HAVE_DECL_HTOBE64)


if (NOT GLOBAL_BUILD)
  find_package(secp256k1 REQUIRED)
endif()
find_package(GMP REQUIRED)
find_package(Boost COMPONENTS boost REQUIRED)
find_package(OpenSSL REQUIRED)

include(CheckCXXCompilerFlag)

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

#------------------------------------------------------------------------------
if (${CURRENCY} STREQUAL "BCH")
  set(kth_sources_clone
    src/bch-rules/amount.h
    src/bch-rules/amount.cpp
    src/bch-rules/coins.cpp
    src/bch-rules/hash.cpp
    src/bch-rules/hash.h
    src/bch-rules/prevector.h
    src/bch-rules/pubkey.cpp
    src/bch-rules/pubkey.h
    src/bch-rules/feerate.cpp
    src/bch-rules/feerate.h
    src/bch-rules/random.cpp
    src/bch-rules/random.h
    src/bch-rules/serialize.h
    src/bch-rules/tinyformat.h
    src/bch-rules/uint256.cpp
    src/bch-rules/uint256.h
    src/bch-rules/version.h

    src/bch-rules/compat/byteswap.h
    src/bch-rules/compat/endian.h
    src/bch-rules/crypto/common.h
    src/bch-rules/crypto/chacha20.cpp
    src/bch-rules/crypto/chacha20.h
    src/bch-rules/crypto/hmac_sha512.cpp
    src/bch-rules/crypto/hmac_sha512.h
    src/bch-rules/crypto/ripemd160.cpp
    src/bch-rules/crypto/ripemd160.h
    src/bch-rules/crypto/sha1.cpp
    src/bch-rules/crypto/sha1.h
    src/bch-rules/crypto/sha256.cpp
    src/bch-rules/crypto/sha256.h
    src/bch-rules/crypto/sha512.cpp
    src/bch-rules/crypto/sha512.h
    src/bch-rules/crypto/siphash.h

    src/bch-rules/primitives/token.h
    src/bch-rules/primitives/token.cpp

    src/bch-rules/primitives/transaction.cpp
    src/bch-rules/primitives/transaction.h
    src/bch-rules/primitives/txid.h

    src/bch-rules/script/bigint.h
    src/bch-rules/script/bigint.cpp
    src/bch-rules/script/bitfield.cpp
    src/bch-rules/script/bitfield.h
    src/bch-rules/script/interpreter.cpp
    src/bch-rules/script/interpreter.h
    src/bch-rules/script/script.cpp
    src/bch-rules/script/script.h
    src/bch-rules/script/script_flags.h
    src/bch-rules/script/script_error.h
    src/bch-rules/script/script_execution_context.cpp
    src/bch-rules/script/script_num_encoding.h
    src/bch-rules/script/script_num_encoding.cpp
    src/bch-rules/script/sigencoding.h
    src/bch-rules/script/sigencoding.cpp

    src/bch-rules/support/cleanse.h
    src/bch-rules/support/cleanse.cpp
    src/bch-rules/support/lockedpool.h
    src/bch-rules/support/lockedpool.cpp


    src/bch-rules/util/strencodings.cpp
    src/bch-rules/util/strencodings.h
    src/bch-rules/util/string.cpp
    src/bch-rules/util/string.h
    src/bch-rules/util/time.h
    src/bch-rules/util/time.cpp
  )
else()
  set(kth_sources_clone
    src/btc-rules/amount.h
    src/btc-rules/hash.cpp
    src/btc-rules/hash.h
    src/btc-rules/prevector.h
    src/btc-rules/pubkey.cpp
    src/btc-rules/pubkey.h
    src/btc-rules/serialize.h
    src/btc-rules/tinyformat.h
    src/btc-rules/uint256.cpp
    src/btc-rules/uint256.h
    src/btc-rules/version.h
    src/btc-rules/compat/byteswap.h
    src/btc-rules/compat/endian.h
    src/btc-rules/crypto/common.h
    src/btc-rules/crypto/hmac_sha512.cpp
    src/btc-rules/crypto/hmac_sha512.h
    src/btc-rules/crypto/ripemd160.cpp
    src/btc-rules/crypto/ripemd160.h
    src/btc-rules/crypto/sha1.cpp
    src/btc-rules/crypto/sha1.h
    src/btc-rules/crypto/sha256.cpp
    src/btc-rules/crypto/sha256.h
    src/btc-rules/crypto/sha512.cpp
    src/btc-rules/crypto/sha512.h
    src/btc-rules/primitives/transaction.cpp
    src/btc-rules/primitives/transaction.h
    src/btc-rules/script/interpreter.cpp
    src/btc-rules/script/interpreter.h
    src/btc-rules/script/script.cpp
    src/btc-rules/script/script.h
    src/btc-rules/script/script_error.h
    src/btc-rules/util/strencodings.cpp
    src/btc-rules/util/strencodings.h
  )
endif()


# add_library(${PROJECT_NAME}-clone OBJECT)
add_library(${PROJECT_NAME}-clone ${MODE} ${kth_sources_clone})
add_library(${PROJECT_NAME}-clone::${PROJECT_NAME}-clone ALIAS ${PROJECT_NAME}-clone)
set_target_properties(${PROJECT_NAME}-clone PROPERTIES LINKER_LANGUAGE CXX CXX_STANDARD 23 CXX_STANDARD_REQUIRED TRUE)

if (ENABLE_POSITION_INDEPENDENT_CODE)
  set_property(TARGET ${PROJECT_NAME}-clone PROPERTY POSITION_INDEPENDENT_CODE ON)
endif(ENABLE_POSITION_INDEPENDENT_CODE)


if (${CURRENCY} STREQUAL "BCH")
  target_include_directories(${PROJECT_NAME}-clone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/bch-rules)
else()
  target_include_directories(${PROJECT_NAME}-clone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/btc-rules)
endif()

target_compile_definitions(${PROJECT_NAME}-clone PRIVATE $<TARGET_PROPERTY:secp256k1::secp256k1,INTERFACE_COMPILE_DEFINITIONS>)
target_include_directories(${PROJECT_NAME}-clone PRIVATE $<TARGET_PROPERTY:secp256k1::secp256k1,INTERFACE_INCLUDE_DIRECTORIES>)

target_link_libraries(${PROJECT_NAME}-clone PUBLIC Boost::boost OpenSSL::Crypto gmp::gmp)

if (${CURRENCY} STREQUAL "BCH")
  _group_sources(${PROJECT_NAME}-clone "${CMAKE_CURRENT_SOURCE_DIR}/src/bch-rules")
else()
  _group_sources(${PROJECT_NAME}-clone "${CMAKE_CURRENT_SOURCE_DIR}/src/btc-rules")
endif()


set(MODE STATIC)
if (ENABLE_SHARED)
  set(MODE SHARED)
endif()

set(kth_sources
  src/consensus/conversions.cpp
  src/consensus/consensus.cpp
  src/consensus/consensus.hpp
)

set(kth_headers
  include/kth/consensus.hpp
  include/kth/consensus/conversions.hpp
  include/kth/consensus/define.hpp
  include/kth/consensus/export.hpp
  include/kth/consensus/version.hpp
)

add_library(${PROJECT_NAME} ${MODE} ${kth_sources} ${kth_headers} $<TARGET_OBJECTS:${PROJECT_NAME}-clone>)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX CXX_STANDARD 23 CXX_STANDARD_REQUIRED TRUE)

if (ENABLE_POSITION_INDEPENDENT_CODE)
  set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
endif(ENABLE_POSITION_INDEPENDENT_CODE)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

if (NOT ENABLE_SHARED)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DKC_STATIC)
endif()

if (${CURRENCY} STREQUAL "BCH")
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bch-rules)
else()
  target_include_directories(${PROJECT_NAME} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/src/btc-rules)
endif()


target_link_libraries(${PROJECT_NAME} PUBLIC secp256k1::secp256k1)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::boost)
target_link_libraries(${PROJECT_NAME} PUBLIC OpenSSL::Crypto)
target_link_libraries(${PROJECT_NAME} PUBLIC gmp::gmp)

if (MINGW)
  target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 wsock32) #TODO(fernando): manage with Conan
endif()

_group_sources(${PROJECT_NAME} "${CMAKE_CURRENT_LIST_DIR}")

# Tests
#==============================================================================
# if (ENABLE_TEST)
#   enable_testing()
#   find_package(Catch2 3 REQUIRED)

#   add_executable(kth_consensus_test
#     test/consensus__script_error_to_verify_result.cpp
#     test/consensus__script_verify.cpp
#     test/consensus__verify_flags_to_script_flags.cpp
#     test/main.cpp
#     test/script.hpp)

#   if (${CURRENCY} STREQUAL "BCH")
#     target_include_directories(kth_consensus_test PRIVATE
#       ${CMAKE_CURRENT_SOURCE_DIR}/src
#       ${CMAKE_CURRENT_SOURCE_DIR}/src/bch-rules)
#   else()
#     target_include_directories(kth_consensus_test PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}/src
#         ${CMAKE_CURRENT_SOURCE_DIR}/src/btc-rules)
#   endif()

#   target_include_directories(kth_consensus_test PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>)
#   target_link_libraries(kth_consensus_test PUBLIC ${PROJECT_NAME})
#   target_link_libraries(kth_consensus_test PRIVATE Catch2::Catch2WithMain)

#   _group_sources(kth_consensus_test "${CMAKE_CURRENT_LIST_DIR}/test")

#   include(CTest)
#   # Try to use Catch2 automatic test discovery
#   find_package(Catch2 QUIET)
#   if(Catch2_FOUND)
#     include(Catch)
#     catch_discover_tests(kth_consensus_test)
#   else()
#     # Fallback to manual test registration
#     add_test(NAME kth_consensus_test COMMAND kth_consensus_test)
#   endif()
# endif()

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


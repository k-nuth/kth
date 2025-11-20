# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.15)

# asert-gen
#==============================================================================
project(asert-gen
        VERSION 0
        LANGUAGES CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Check for baseline language coverage in the compiler for the C++14 standard.
#------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(KTH_PROJECT_VERSION "-" CACHE STRING "Specify the Knuth Project Version.")
# # message(${KTH_PROJECT_VERSION})
# message( STATUS "Knuth: Compiling version ${KTH_PROJECT_VERSION}")

set(MARCH_ID "" CACHE STRING "Specify the Microarchitecture ID (x86_64|...).")
message( STATUS "Knuth: Compiling for Microarchitecture ID ${MARCH_ID}")

message( STATUS "Knuth: CONAN_CXX_FLAGS ${CONAN_CXX_FLAGS}")
message( STATUS "Knuth: CONAN_C_FLAGS ${CONAN_C_FLAGS}")




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


# Conan infrastructure
#==============================================================================
if(EXISTS ${CMAKE_SOURCE_DIR}../../ci_utils/cmake/knuthbuildinfo.cmake)
    include(${CMAKE_SOURCE_DIR}../../ci_utils/cmake/knuthbuildinfo.cmake)
else()
    message( STATUS "knuthbuildinfo doent exists")
endif()

# Set flags.
#==============================================================================
include(CheckCXXCompilerFlag)

# Including common functions
#------------------------------------------------------------------------------
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/ci_utils/cmake)

include(KnuthTools)


# Warn on all stuff.
#------------------------------------------------------------------------------
if (NOT MSVC)
  _add_c_compile_flag(-Wall _has_all_warning_flag)
else()
  _add_c_compile_flag(-W4 _has_all_warning_flag)
  add_definitions(-D_SCL_SECURE_NO_WARNINGS)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Warn on extra stuff.
#------------------------------------------------------------------------------
if (NOT MSVC)
  _add_c_compile_flag(-Wextra _has_extra_warning_flag)
endif()

# Be really annoying.
#------------------------------------------------------------------------------
_add_c_compile_flag(-Wpedantic _has_pedantic_warning_flag)
if (_has_pedantic_warning_flag)
  _add_c_compile_flag(-pedantic _has_pedantic_flag)
endif()

# Conform to style.
#------------------------------------------------------------------------------
_add_cxx_compile_flag(-Wno-missing-braces _has_no_missing_braces_warning_flag)

# Conflict in stdlib under clang. Enabled in clang only.
#------------------------------------------------------------------------------
_add_cxx_compile_flag(-Wno-mismatched-tags _has_no_mismatched_tags_warning_flag)

# Clean up boost 1.55 headers. Enabled in gcc only.
#------------------------------------------------------------------------------
_add_c_compile_flag(-Wno-deprecated-declarations _has_no_deprecated_declarations_warning_flag)

# Protect stack.
#------------------------------------------------------------------------------
_add_link_flag(-fstack-protector _has_stack_protector_flag)

# Protect stack comprehensively.
#------------------------------------------------------------------------------
_add_link_flag(-fstack-protector-all _has_stack_protector_all_flag)

# Hide internal functions from external libs. Enabled in gcc only.
#------------------------------------------------------------------------------
_add_cxx_compile_flag(-fvisibility-hidden _has_visibility_hidden_flag)

# Hide inlines from external libs. Enabled in gcc only.
#------------------------------------------------------------------------------
_add_cxx_compile_flag(-fvisibility-inlines-hidden _has_visibility_inlines_hidden_flag)

# Target Windows Vista. Enabled in msvc only.
#------------------------------------------------------------------------------
if (MSVC) # OR MINGW)
  add_definitions(-D_WIN32_WINNT=0x0600)
endif()

# Build
#==============================================================================
add_executable(asert-gen
    genenerate_test_vectors.cpp)


# target_compile_definitions(asert-gen PUBLIC -DKTH_PROJECT_VERSION="${KTH_PROJECT_VERSION}") #TODO(fernando): manage with Conan????
# Note: These should be inherited from dependencies via Conan
# target_compile_definitions(asert-gen PUBLIC -DKND_STATIC -DKB_STATIC -DKN_STATIC)

# target_link_libraries(asert-gen PUBLIC ${CONAN_LIBS})

set_target_properties(
    asert-gen PROPERTIES
        FOLDER "node"
        OUTPUT_NAME generator)

# Install
#==============================================================================
install(TARGETS asert-gen
        RUNTIME DESTINATION bin)

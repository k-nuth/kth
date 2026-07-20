# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Header-only, vendored ParlayHash (see README.md for the pinned commit).
# Exposed as an INTERFACE target; the include directory is marked SYSTEM so the
# library's deprecation warnings do not surface in the Knuth build.

add_library(parlayhash INTERFACE)
add_library(parlayhash::parlayhash ALIAS parlayhash)

target_include_directories(parlayhash SYSTEM INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(parlayhash INTERFACE cxx_std_23)

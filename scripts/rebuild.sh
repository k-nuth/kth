#!/usr/bin/env bash
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version> <test>"
    exit 1
fi
VERSION="$1"
TEST="$2"
if [ -z "$TEST" ]; then
    echo "No test specified, defaulting to '0'"
    TEST="0"
fi
echo "Building version: ${VERSION} with test: ${TEST}"

ARCH=$(uname -m)
MARCH_OPT=""
if [ "$ARCH" = "x86_64" ]; then
    MARCH_OPT='-o &:march_strategy=optimized'
fi

rm -rf build
rm -rf conan.lock
# Conan regenerates CMakeUserPresets.json on every `conan install`, but it
# appends to the existing include list rather than replacing it. Stale
# entries from older build trees (e.g. `build-release-0.79.0/...`) cause
# "Duplicate preset: conan-release" errors. Nuke it so conan writes a
# fresh one.
rm -f CMakeUserPresets.json

conan lock create conanfile.py --version="${VERSION}" $MARCH_OPT --update || exit 1
conan lock create conanfile.py --version "${VERSION}" $MARCH_OPT --lockfile=conan.lock --lockfile-out=build/conan.lock || exit 1
conan install conanfile.py --version="${VERSION}" $MARCH_OPT --lockfile=build/conan.lock -of build --build=missing || exit 1

CCACHE_OPT=""
if command -v ccache &>/dev/null; then
    CCACHE_OPT="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

cmake --preset conan-release \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DGLOBAL_BUILD=ON \
         -DENABLE_TEST=ON \
         -DCMAKE_BUILD_TYPE=Release \
         $CCACHE_OPT

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

cmake --build --preset conan-release --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd build/build/Release || exit 1
    # Run tests with ctest (CTestTestfile.cmake is now generated automatically by CMake)
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi

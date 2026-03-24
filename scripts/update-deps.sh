#!/usr/bin/env bash
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Update dependencies (conan install) and rebuild without cleaning.
# Use this when you changed a dependency version but don't want a full rebuild.
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version> [test]"
    exit 1
fi
VERSION="$1"
TEST="${2:-0}"

echo "Updating deps and building version: ${VERSION}"

ARCH=$(uname -m)
MARCH_OPT=""
if [ "$ARCH" = "x86_64" ]; then
    MARCH_OPT='-o &:march_strategy=optimized'
fi

rm -rf conan.lock

conan lock create conanfile.py --version="${VERSION}" $MARCH_OPT --update || exit 1
conan lock create conanfile.py --version "${VERSION}" $MARCH_OPT --lockfile=conan.lock --lockfile-out=build/conan.lock || exit 1
conan install conanfile.py --version="${VERSION}" $MARCH_OPT --lockfile=build/conan.lock -of build --build=missing || exit 1

# Reconfigure CMake (needed after conan install to pick up new deps)
cmake --preset conan-release \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DGLOBAL_BUILD=ON \
         -DENABLE_TEST=ON \
         -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

cmake --build --preset conan-release --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd build/build/Release || exit 1
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi

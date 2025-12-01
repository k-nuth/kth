#!/bin/bash
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

BUILD_DIR="build/build/Release"

# Only configure if not already configured or if explicitly requested
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ] || [ "$RECONFIGURE" = "1" ]; then
    echo "Configuring CMake with Ninja..."
    cmake --preset conan-release \
             -G Ninja \
             -DCMAKE_VERBOSE_MAKEFILE=ON \
             -DGLOBAL_BUILD=ON \
             -DENABLE_TEST=ON \
             -DCMAKE_BUILD_TYPE=Release

    if [ $? -ne 0 ]; then
        echo "CMake configuration failed"
        exit 1
    fi
else
    echo "Skipping CMake configuration (already configured). Set RECONFIGURE=1 to force."
fi

cmake --build --preset conan-release --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd ${BUILD_DIR}
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi

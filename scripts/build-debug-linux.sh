#!/bin/bash
# Build in full Debug mode (no optimizations, full debug info)
# NOTE: Requires running rebuild-debug.sh first to install conan dependencies with Debug build type
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version> [test]"
    exit 1
fi
VERSION="$1"
TEST="${2:-0}"
echo "Building version: ${VERSION} with test: ${TEST} (Debug)"

BUILD_DIR="build/build/Debug"

# Only configure if not already configured or if explicitly requested
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ] || [ "$RECONFIGURE" = "1" ]; then
    echo "Configuring CMake (Debug)..."
    cmake --preset conan-debug \
             -DCMAKE_VERBOSE_MAKEFILE=ON \
             -DGLOBAL_BUILD=ON \
             -DENABLE_TEST=ON \
             -DCMAKE_BUILD_TYPE=Debug

    if [ $? -ne 0 ]; then
        echo "CMake configuration failed"
        echo "Did you run rebuild-debug.sh first to install conan dependencies?"
        exit 1
    fi
else
    echo "Skipping CMake configuration (already configured). Set RECONFIGURE=1 to force."
fi

cmake --build --preset conan-debug --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo ""
echo "Build complete. Debug binary at:"
echo "  ${BUILD_DIR}/src/node-exe/kth"
echo ""
echo "To debug: gdb ${BUILD_DIR}/src/node-exe/kth"
echo "  Or with args: gdb --args ${BUILD_DIR}/src/node-exe/kth -c config.cfg"

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd "${BUILD_DIR}"
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi

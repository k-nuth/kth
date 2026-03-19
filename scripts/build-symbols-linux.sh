#!/bin/bash
# Build with Release optimizations + debug symbols (RelWithDebInfo)
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version> [test]"
    exit 1
fi
VERSION="$1"
TEST="${2:-0}"
echo "Building version: ${VERSION} with test: ${TEST} (RelWithDebInfo)"

BUILD_DIR="build/build/RelWithDebInfo"

# Only configure if not already configured or if explicitly requested
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ] || [ "$RECONFIGURE" = "1" ]; then
    echo "Configuring CMake (RelWithDebInfo)..."
    cmake --preset conan-relwithdebinfo \
             -DCMAKE_VERBOSE_MAKEFILE=ON \
             -DGLOBAL_BUILD=ON \
             -DENABLE_TEST=ON \
             -DCMAKE_BUILD_TYPE=RelWithDebInfo

    if [ $? -ne 0 ]; then
        echo "CMake configuration failed"
        exit 1
    fi
else
    echo "Skipping CMake configuration (already configured). Set RECONFIGURE=1 to force."
fi

cmake --build --preset conan-relwithdebinfo --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo ""
echo "Build complete. Binary with debug symbols at:"
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

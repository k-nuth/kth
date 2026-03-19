#!/bin/bash
# Build with Release optimizations + debug symbols (RelWithDebInfo)
set -x

TEST="${1:-0}"
echo "Building with test: ${TEST} (RelWithDebInfo)"

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
echo "To debug: lldb ${BUILD_DIR}/src/node-exe/kth"

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd "${BUILD_DIR}" || exit 1
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi

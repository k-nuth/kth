#!/usr/bin/env bash
# Incremental build with AddressSanitizer (assumes rebuild-asan.sh was run first)
# Uses build_asan/ directory with RelWithDebInfo preset
#
# Usage:
#   ./build-asan.sh           # just build
#   ./build-asan.sh 1         # build and run all tests
#   ./build-asan.sh "filter"  # build and run tests matching filter (regex)
#
# Examples:
#   ./build-asan.sh "peer_session send"
#   ./build-asan.sh "perform_handshake"
#
set -e

TEST="$1"
if [ -z "$TEST" ]; then
    echo "No test specified, defaulting to '0'"
    TEST="0"
fi

BUILD_DIR="build_asan"

if [ ! -d "${BUILD_DIR}/build/RelWithDebInfo" ]; then
    echo "Error: ${BUILD_DIR}/build/RelWithDebInfo does not exist."
    echo "Run rebuild-asan.sh <version> first to do a full build."
    exit 1
fi

echo "Building with AddressSanitizer (incremental)..."

cmake --build --preset conan-relwithdebinfo --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Run tests if specified
if [ "$TEST" = "0" ]; then
    echo "Skipping tests"
elif [ "$TEST" = "1" ]; then
    echo "Running all tests..."
    cd "${BUILD_DIR}/build/RelWithDebInfo"
    ctest --output-on-failure --parallel 4
else
    echo "Running tests matching: $TEST"
    cd "${BUILD_DIR}/build/RelWithDebInfo"
    ctest --output-on-failure -R "$TEST"
fi

echo ""
echo "=============================================="
echo "AddressSanitizer build complete!"
echo "=============================================="
echo ""
echo "Binary at: ${BUILD_DIR}/build/RelWithDebInfo/src/node-exe/kth"
echo ""
echo "To run: ./scripts/run-asan.sh"

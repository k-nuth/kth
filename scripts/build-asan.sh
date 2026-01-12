#!/bin/bash
# Incremental build with AddressSanitizer (assumes rebuild-asan.sh was run first)
# Uses build-asan/ directory
set -e

BUILD_DIR="build-asan"

if [ ! -d "${BUILD_DIR}/build/Release" ]; then
    echo "Error: ${BUILD_DIR}/build/Release does not exist."
    echo "Run rebuild-asan.sh <version> first to do a full build."
    exit 1
fi

echo "Building with AddressSanitizer (incremental)..."

cmake --build "${BUILD_DIR}/build/Release" --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo ""
echo "Build complete!"
echo "Binary at: ${BUILD_DIR}/build/Release/src/node-exe/kth"
echo ""
echo "To run:"
echo "  export ASAN_OPTIONS='detect_leaks=1:abort_on_error=0:print_stats=1'"
echo "  ${BUILD_DIR}/build/Release/src/node-exe/kth -c your_config.cfg"

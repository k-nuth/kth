#!/usr/bin/env bash
# Incremental build with AddressSanitizer (assumes rebuild-asan.sh was run first)
# Uses build_asan/ directory with RelWithDebInfo preset
set -e

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

echo ""
echo "Build complete!"
echo "Binary at: ${BUILD_DIR}/build/RelWithDebInfo/src/node-exe/kth"
echo ""
echo "To run:"
echo "  export ASAN_OPTIONS='detect_leaks=0:abort_on_error=1:print_stats=1:halt_on_error=0'"
echo "  ${BUILD_DIR}/build/RelWithDebInfo/src/node-exe/kth -c your_config.cfg"

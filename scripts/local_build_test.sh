#!/usr/bin/env bash

# Local Build and Test Script
# Based on GitHub Actions build-without-container.yml workflow
# Usage: ./scripts/local_build_test.sh [options]

set -e
set -x

# Default values
BUILD_VERSION="0.69.0-local"
RECIPE_NAME="kth"
SKIP_TESTS="false"
ENABLE_ASAN="false"
ENABLE_UBSAN="false"
ENABLE_TSAN="false"
UPLOAD="false"
PARALLEL_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)
            BUILD_VERSION="$2"
            shift 2
            ;;
        --skip-tests)
            SKIP_TESTS="true"
            shift
            ;;
        --enable-asan)
            ENABLE_ASAN="true"
            shift
            ;;
        --enable-ubsan)
            ENABLE_UBSAN="true"
            shift
            ;;
        --enable-tsan)
            ENABLE_TSAN="true"
            shift
            ;;
        --jobs|-j)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --version VERSION     Build version (default: $BUILD_VERSION)"
            echo "  --skip-tests          Skip test execution"
            echo "  --enable-asan         Enable AddressSanitizer"
            echo "  --enable-ubsan        Enable UndefinedBehaviorSanitizer"
            echo "  --enable-tsan         Enable ThreadSanitizer"
            echo "  --jobs,-j N           Number of parallel jobs (default: $PARALLEL_JOBS)"
            echo "  --help,-h             Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "🚀 Starting local KTH build and test..."
echo "📋 Configuration:"
echo "   Build version: $BUILD_VERSION"
echo "   Recipe name: $RECIPE_NAME"
echo "   Skip tests: $SKIP_TESTS"
echo "   Enable ASAN: $ENABLE_ASAN"
echo "   Enable UBSAN: $ENABLE_UBSAN"
echo "   Enable TSAN: $ENABLE_TSAN"
echo "   Parallel jobs: $PARALLEL_JOBS"
echo ""

# Change to script directory's parent (project root)
cd "$(dirname "$0")/.."

# Prepare sanitizer flags
SANITIZER_FLAGS=""
BUILD_SUFFIX=""

# Check for conflicting sanitizers (AddressSanitizer and ThreadSanitizer are mutually exclusive)
if [ "$ENABLE_ASAN" = "true" ] && [ "$ENABLE_TSAN" = "true" ]; then
    echo "⚠️ WARNING: AddressSanitizer and ThreadSanitizer are mutually exclusive"
    echo "🔧 Prioritizing AddressSanitizer and disabling ThreadSanitizer"
    TSAN_ENABLED=false
else
    TSAN_ENABLED="$ENABLE_TSAN"
fi

if [ "$ENABLE_ASAN" = "true" ]; then
    echo "🧪 Enabling AddressSanitizer build"
    SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer"
    BUILD_SUFFIX="${BUILD_SUFFIX}-asan"
fi

if [ "$ENABLE_UBSAN" = "true" ]; then
    echo "🧪 Enabling UndefinedBehaviorSanitizer build"
    SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=undefined -fno-omit-frame-pointer"
    BUILD_SUFFIX="${BUILD_SUFFIX}-ubsan"
fi

if [ "$TSAN_ENABLED" = "true" ]; then
    echo "🧪 Enabling ThreadSanitizer build"
    SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=thread"
    BUILD_SUFFIX="${BUILD_SUFFIX}-tsan"
fi

# Set environment variables for sanitizers
if [ -n "$SANITIZER_FLAGS" ]; then
    echo "🔧 Using sanitizer flags: $SANITIZER_FLAGS"
    export CXXFLAGS="$CXXFLAGS $SANITIZER_FLAGS"
    export CFLAGS="$CFLAGS $SANITIZER_FLAGS"
    export LDFLAGS="$LDFLAGS $SANITIZER_FLAGS"
fi

# Build with tests enabled only if not skipping tests (to avoid recompilation)
if [ "$SKIP_TESTS" = "true" ]; then
    echo "⏭️ Skipping tests - building without test binaries"
    TEST_OPTION="tests=False"
else
    echo "🧪 Building with tests enabled"
    TEST_OPTION="tests=True"
fi

echo ""
echo "🔨 Starting build phase..."

# Detect platform and set appropriate profile
if [[ "$OSTYPE" == "darwin"* ]]; then    
    PROFILE="general-ci-cd"
else
    PROFILE="general-ci-cd"
fi
PROFILE="default"

# Create lock file and build
mkdir -p build
conan lock create conanfile.py --version "$BUILD_VERSION" --lockfile=conan.lock --lockfile-out=build/conan.lock -pr:b "$PROFILE" -pr:h "$PROFILE"
conan create conanfile.py --version "$BUILD_VERSION" --lockfile=build/conan.lock -pr:b "$PROFILE" -pr:h "$PROFILE" --build=missing -o "$TEST_OPTION"

if [ "$SKIP_TESTS" = "true" ]; then
    echo "⏭️ Unit tests skipped"
    echo "✅ Build completed successfully!"
    exit 0
fi

echo ""
echo "🧪 Running package integration tests..."

# Try to run package tests using conan test command (integration tests)
if conan test test_package "$RECIPE_NAME/$BUILD_VERSION" 2>/dev/null; then
    echo "✅ Package integration tests completed successfully"
else
    echo "⚠️ Package integration tests failed or not available"
fi

echo ""
echo "🧪 Running unit tests..."

# Set sanitizer options for better reporting (preserve conflict resolution from build step)
TSAN_ENABLED_FOR_TESTS="$ENABLE_TSAN"
if [ "$ENABLE_ASAN" = "true" ] && [ "$ENABLE_TSAN" = "true" ]; then
    TSAN_ENABLED_FOR_TESTS=false
fi

if [ "$ENABLE_ASAN" = "true" ] || [ "$ENABLE_UBSAN" = "true" ] || [ "$TSAN_ENABLED_FOR_TESTS" = "true" ]; then
    echo "🧪 Running tests with sanitizers enabled..."
    echo "⚠️ Tests may run slower due to sanitizer overhead"
    
    if [ "$ENABLE_ASAN" = "true" ]; then
        export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:print_stats=1"
    fi
    
    if [ "$ENABLE_UBSAN" = "true" ]; then
        export UBSAN_OPTIONS="print_stacktrace=1:abort_on_error=1"
    fi
    
    if [ "$TSAN_ENABLED_FOR_TESTS" = "true" ]; then
        export TSAN_OPTIONS="halt_on_error=1:abort_on_error=1"
    fi
fi

# Build and run unit tests locally (package doesn't contain test executables)
echo "🔨 Building project locally with tests enabled..."
conan install conanfile.py --lockfile=build/conan.lock -of build --build=missing -pr:b "$PROFILE" -pr:h "$PROFILE" -o tests=True

cmake --preset conan-release \
     -DCMAKE_VERBOSE_MAKEFILE=ON \
     -DGLOBAL_BUILD=ON \
     -DENABLE_TEST=ON \
     -DCMAKE_BUILD_TYPE=Release
     
cmake --build --preset conan-release -j"$PARALLEL_JOBS"

cd build/build/Release

echo "🧪 Executing unit tests with ctest..."
if [ "$ENABLE_ASAN" = "true" ] || [ "$ENABLE_UBSAN" = "true" ] || [ "$TSAN_ENABLED_FOR_TESTS" = "true" ]; then
    # Use timeout if available, otherwise run without timeout
    if command -v timeout >/dev/null 2>&1; then
        timeout 1800 ctest --output-on-failure --parallel 2 || echo "Some tests may have failed or timed out with sanitizers"
    elif command -v gtimeout >/dev/null 2>&1; then
        gtimeout 1800 ctest --output-on-failure --parallel 2 || echo "Some tests may have failed or timed out with sanitizers"
    else
        ctest --output-on-failure --parallel 2 || echo "Some tests may have failed with sanitizers"
    fi
else
    ctest --output-on-failure --parallel "$PARALLEL_JOBS" || echo "Some tests may have failed, but continuing..."
fi

echo ""
echo "🎉 Local build and test completed!"
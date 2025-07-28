#!/bin/bash

# Script to build locally with sanitizers enabled
# Based on scripts/build.sh but with sanitizer support
#
# Usage:
#   ./scripts/build-sanitizer.sh <version>                   # Build with AddressSanitizer by default
#   ./scripts/build-sanitizer.sh <version> --asan           # Build with AddressSanitizer
#   ./scripts/build-sanitizer.sh <version> --ubsan          # Build with UndefinedBehaviorSanitizer
#   ./scripts/build-sanitizer.sh <version> --tsan           # Build with ThreadSanitizer
#   ./scripts/build-sanitizer.sh <version> --asan --ubsan   # Build with AddressSanitizer + UndefinedBehaviorSanitizer
#   ./scripts/build-sanitizer.sh <version> --all            # Build with all compatible sanitizers
#   ./scripts/build-sanitizer.sh <version> --help           # Show help

set -e

# Parse command line arguments
if [ -z "$1" ]; then
    echo "Usage: $0 <version> [SANITIZER_OPTIONS]"
    echo ""
    echo "Build the project locally with sanitizers enabled"
    echo ""
    echo "SANITIZER_OPTIONS:"
    echo "  --asan    Enable AddressSanitizer (memory errors, default if none specified)"
    echo "  --ubsan   Enable UndefinedBehaviorSanitizer (undefined behavior)"
    echo "  --tsan    Enable ThreadSanitizer (race conditions, threading issues)"
    echo "  --all     Enable all compatible sanitizers"
    echo "  --help    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 0.69.0              # Build with AddressSanitizer (default)"
    echo "  $0 0.69.0 --ubsan      # Build with UndefinedBehaviorSanitizer"
    echo "  $0 0.69.0 --asan --ubsan  # Build with AddressSanitizer + UndefinedBehaviorSanitizer"
    echo "  $0 0.69.0 --all        # Build with all compatible sanitizers"
    echo ""
    echo "Sanitizers detect:"
    echo "  • AddressSanitizer (ASAN): Memory leaks, buffer overflows, use-after-free"
    echo "  • UndefinedBehaviorSanitizer (UBSAN): Integer overflow, null pointer dereference"
    echo "  • ThreadSanitizer (TSAN): Data races, deadlocks, thread safety issues"
    echo ""
    echo "⚠️  WARNING: AddressSanitizer and ThreadSanitizer are mutually exclusive on Clang"
    echo "💡 TIP: Sanitizer builds are slower and use more memory"
    exit 1
fi

VERSION="$1"
shift

# Parse sanitizer options
ENABLE_ASAN=false
ENABLE_UBSAN=false
ENABLE_TSAN=false
ENABLE_ALL=false
SHOW_HELP=false

for arg in "$@"; do
    case $arg in
        --asan)
            ENABLE_ASAN=true
            ;;
        --ubsan)
            ENABLE_UBSAN=true
            ;;
        --tsan)
            ENABLE_TSAN=true
            ;;
        --all)
            ENABLE_ALL=true
            ;;
        --help|-h)
            SHOW_HELP=true
            ;;
        *)
            echo "Unknown option: $arg"
            SHOW_HELP=true
            ;;
    esac
done

if [ "$SHOW_HELP" = true ]; then
    echo "Usage: $0 <version> [SANITIZER_OPTIONS]"
    echo ""
    echo "See usage instructions above."
    exit 0
fi

# If no sanitizer specified, default to AddressSanitizer
if [ "$ENABLE_ASAN" = false ] && [ "$ENABLE_UBSAN" = false ] && [ "$ENABLE_TSAN" = false ] && [ "$ENABLE_ALL" = false ]; then
    ENABLE_ASAN=true
fi

# If --all specified, enable all compatible sanitizers
if [ "$ENABLE_ALL" = true ]; then
    ENABLE_ASAN=true
    ENABLE_UBSAN=true
    # Note: TSAN is not enabled with --all because it conflicts with ASAN on Clang
    echo "🔧 --all flag: Enabling AddressSanitizer + UndefinedBehaviorSanitizer"
    echo "   (ThreadSanitizer excluded due to conflict with AddressSanitizer on Clang)"
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🧪 Building version: ${VERSION} with sanitizers...${NC}"

# Check for conflicting sanitizers (AddressSanitizer and ThreadSanitizer are mutually exclusive on Clang)
if [ "$ENABLE_ASAN" = true ] && [ "$ENABLE_TSAN" = true ]; then
    echo -e "${YELLOW}⚠️ WARNING: AddressSanitizer and ThreadSanitizer are mutually exclusive on Clang${NC}"
    echo -e "${BLUE}🔧 Prioritizing AddressSanitizer and disabling ThreadSanitizer${NC}"
    ENABLE_TSAN=false
fi

# Prepare sanitizer flags
SANITIZER_FLAGS=""
BUILD_SUFFIX=""
SANITIZER_NAME=""

if [ "$ENABLE_ASAN" = true ]; then
    echo -e "${BLUE}🧪 Enabling AddressSanitizer build${NC}"
    SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer"
    BUILD_SUFFIX="${BUILD_SUFFIX}-asan"
    SANITIZER_NAME="AddressSanitizer"
fi

if [ "$ENABLE_UBSAN" = true ]; then
    echo -e "${BLUE}🧪 Enabling UndefinedBehaviorSanitizer build${NC}"
    SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=undefined -fno-omit-frame-pointer"
    BUILD_SUFFIX="${BUILD_SUFFIX}-ubsan"
    if [ -n "$SANITIZER_NAME" ]; then
        SANITIZER_NAME="$SANITIZER_NAME + UndefinedBehaviorSanitizer"
    else
        SANITIZER_NAME="UndefinedBehaviorSanitizer"
    fi
fi

if [ "$ENABLE_TSAN" = true ]; then
    echo -e "${BLUE}🧪 Enabling ThreadSanitizer build${NC}"
    SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=thread"
    BUILD_SUFFIX="${BUILD_SUFFIX}-tsan"
    if [ -n "$SANITIZER_NAME" ]; then
        SANITIZER_NAME="$SANITIZER_NAME + ThreadSanitizer"
    else
        SANITIZER_NAME="ThreadSanitizer"
    fi
fi

echo -e "${BLUE}🔧 Using sanitizer flags: $SANITIZER_FLAGS${NC}"
echo -e "${BLUE}📊 Building with: $SANITIZER_NAME${NC}"

# Set environment variables for sanitizers
if [ -n "$SANITIZER_FLAGS" ]; then
    export CXXFLAGS="$CXXFLAGS $SANITIZER_FLAGS"
    export CFLAGS="$CFLAGS $SANITIZER_FLAGS"
    export LDFLAGS="$LDFLAGS $SANITIZER_FLAGS"
    echo -e "${BLUE}🔧 Environment variables set with sanitizer flags${NC}"
fi

# Create build directory with sanitizer suffix
BUILD_DIR="build${BUILD_SUFFIX}"
echo -e "${BLUE}📁 Using build directory: $BUILD_DIR${NC}"

# Clean previous build
echo -e "${BLUE}🧹 Cleaning previous build...${NC}"
rm -rf "$BUILD_DIR"
rm -rf conan.lock

# Build with sanitizers using same logic as original build.sh
echo -e "${BLUE}🔐 Creating conan lock files...${NC}"
conan lock create conanfile.py --version="${VERSION}" --update
conan lock create conanfile.py --version "${VERSION}" --lockfile=conan.lock --lockfile-out="$BUILD_DIR/conan.lock"

echo -e "${BLUE}📦 Installing conan dependencies with tests enabled...${NC}"
conan install conanfile.py --lockfile="$BUILD_DIR/conan.lock" -of "$BUILD_DIR" --build=missing -o tests=True

echo -e "${BLUE}⚙️ Configuring CMake with sanitizers...${NC}"
cmake --preset conan-release \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DGLOBAL_BUILD=ON \
         -DENABLE_TEST=ON \
         -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ CMake configuration failed${NC}"
    exit 1
fi

echo -e "${BLUE}🔨 Building with sanitizers...${NC}"
cmake --build --preset conan-release -j4

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ CMake build failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Build completed successfully!${NC}"

# Run tests with sanitizers
echo -e "${BLUE}🧪 Running tests with $SANITIZER_NAME...${NC}"
echo -e "${YELLOW}⚠️ Tests may run slower due to sanitizer overhead${NC}"

cd "$BUILD_DIR/build/Release"

# Set sanitizer options for better reporting
if [ "$ENABLE_ASAN" = true ]; then
    export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:print_stats=1"
    echo -e "${BLUE}🔧 ASAN_OPTIONS: $ASAN_OPTIONS${NC}"
fi

if [ "$ENABLE_UBSAN" = true ]; then
    export UBSAN_OPTIONS="print_stacktrace=1:abort_on_error=1"
    echo -e "${BLUE}🔧 UBSAN_OPTIONS: $UBSAN_OPTIONS${NC}"
fi

if [ "$ENABLE_TSAN" = true ]; then
    export TSAN_OPTIONS="halt_on_error=1:abort_on_error=1"
    echo -e "${BLUE}🔧 TSAN_OPTIONS: $TSAN_OPTIONS${NC}"
fi

# Run tests with appropriate parallelism for sanitizers
if [ "$ENABLE_ASAN" = true ] || [ "$ENABLE_UBSAN" = true ] || [ "$ENABLE_TSAN" = true ]; then
    echo -e "${BLUE}🧪 Running tests with sanitizers (reduced parallelism)...${NC}"
    ctest --output-on-failure --parallel 2
else
    # Fallback for no sanitizers (shouldn't happen with this script)
    ctest --output-on-failure --parallel 4
fi

TEST_RESULT=$?

echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}✅ All tests passed with $SANITIZER_NAME!${NC}"
    echo -e "${GREEN}🎉 No memory errors, undefined behavior, or race conditions detected!${NC}"
else
    echo -e "${YELLOW}⚠️ Some tests may have failed or timed out with sanitizers${NC}"
    echo -e "${BLUE}💡 Check the output above for any sanitizer reports${NC}"
    echo ""
    echo -e "${BLUE}📊 Common sanitizer findings:${NC}"
    if [ "$ENABLE_ASAN" = true ]; then
        echo "  • AddressSanitizer: Memory leaks, buffer overflows, use-after-free"
    fi
    if [ "$ENABLE_UBSAN" = true ]; then
        echo "  • UndefinedBehaviorSanitizer: Integer overflow, null pointer dereference"
    fi
    if [ "$ENABLE_TSAN" = true ]; then
        echo "  • ThreadSanitizer: Data races, deadlocks, thread safety issues"
    fi
fi

echo ""
echo -e "${BLUE}🔬 Sanitizer build completed!${NC}"
echo -e "${BLUE}📁 Build artifacts available in: $BUILD_DIR${NC}"
echo -e "${BLUE}💡 Tips:${NC}"
echo "  • Run different sanitizers separately for better performance"
echo "  • Review any sanitizer reports carefully"
echo "  • AddressSanitizer is great for memory issues"
echo "  • UndefinedBehaviorSanitizer catches subtle bugs"
echo "  • ThreadSanitizer is essential for multithreaded code"

exit $TEST_RESULT
#!/bin/bash

# Script to test sanitizer builds locally
# This mirrors the sanitizer functionality in GitHub Actions build workflows
#
# Usage:
#   ./scripts/test-sanitizers.sh                    # Test AddressSanitizer by default
#   ./scripts/test-sanitizers.sh --asan             # Test AddressSanitizer
#   ./scripts/test-sanitizers.sh --ubsan            # Test UndefinedBehaviorSanitizer  
#   ./scripts/test-sanitizers.sh --tsan             # Test ThreadSanitizer
#   ./scripts/test-sanitizers.sh --all              # Test all sanitizers
#   ./scripts/test-sanitizers.sh --help             # Show help

set -e

# Parse command line arguments
ENABLE_ASAN=false
ENABLE_UBSAN=false
ENABLE_TSAN=false
TEST_ALL=false
SHOW_HELP=false

for arg in "$@"; do
    case $arg in
        --asan)
            ENABLE_ASAN=true
            shift
            ;;
        --ubsan)
            ENABLE_UBSAN=true
            shift
            ;;
        --tsan)
            ENABLE_TSAN=true
            shift
            ;;
        --all)
            TEST_ALL=true
            shift
            ;;
        --help|-h)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "Unknown option: $arg"
            SHOW_HELP=true
            ;;
    esac
done

# If no sanitizer specified, default to AddressSanitizer
if [ "$ENABLE_ASAN" = false ] && [ "$ENABLE_UBSAN" = false ] && [ "$ENABLE_TSAN" = false ] && [ "$TEST_ALL" = false ]; then
    ENABLE_ASAN=true
fi

# If --all specified, enable all sanitizers
if [ "$TEST_ALL" = true ]; then
    ENABLE_ASAN=true
    ENABLE_UBSAN=true
    ENABLE_TSAN=true
fi

if [ "$SHOW_HELP" = true ]; then
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Test C++ code with sanitizers to detect runtime errors"
    echo ""
    echo "OPTIONS:"
    echo "  --asan    Enable AddressSanitizer (memory errors, default if none specified)"
    echo "  --ubsan   Enable UndefinedBehaviorSanitizer (undefined behavior)"
    echo "  --tsan    Enable ThreadSanitizer (race conditions, threading issues)"
    echo "  --all     Test with all sanitizers"
    echo "  --help    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0              # Test with AddressSanitizer (default)"
    echo "  $0 --ubsan      # Test with UndefinedBehaviorSanitizer"
    echo "  $0 --all        # Test with all sanitizers"
    echo ""
    echo "Sanitizers detect:"
    echo "  • AddressSanitizer (ASAN): Memory leaks, buffer overflows, use-after-free"
    echo "  • UndefinedBehaviorSanitizer (UBSAN): Integer overflow, null pointer dereference"
    echo "  • ThreadSanitizer (TSAN): Data races, deadlocks, thread safety issues"
    echo ""
    echo "⚠️  WARNING: Sanitizer builds are slower and use more memory"
    echo "💡 TIP: Test with one sanitizer at a time for better performance"
    exit 0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🧪 Testing with sanitizers...${NC}"

# Check dependencies
echo -e "${BLUE}🔍 Checking dependencies...${NC}"

if ! command -v conan &> /dev/null; then
    echo -e "${RED}❌ Conan not found. Please install Conan package manager${NC}"
    exit 1
fi

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ CMake not found. Please install CMake${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Dependencies found${NC}"

# Check if conanfile.py exists
if [ ! -f "conanfile.py" ]; then
    echo -e "${RED}❌ conanfile.py not found. Please run this script from the project root directory${NC}"
    exit 1
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
echo -e "${BLUE}📊 Testing with: $SANITIZER_NAME${NC}"

# Set environment variables for sanitizers
if [ -n "$SANITIZER_FLAGS" ]; then
    export CXXFLAGS="$CXXFLAGS $SANITIZER_FLAGS"
    export CFLAGS="$CFLAGS $SANITIZER_FLAGS"
    export LDFLAGS="$LDFLAGS $SANITIZER_FLAGS"
fi

# Create build directory
BUILD_DIR="build${BUILD_SUFFIX}"
echo -e "${BLUE}📁 Using build directory: $BUILD_DIR${NC}"

# Clean previous build if it exists
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}🧹 Cleaning previous build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

echo -e "${BLUE}🔨 Building with sanitizers...${NC}"

# Determine build version
BUILD_VERSION="0.69.0-sanitizer-test"

# Build project with sanitizers
echo -e "${BLUE}🔧 Creating Conan lock file...${NC}"
conan lock create conanfile.py --version "$BUILD_VERSION" --lockfile=conan.lock --lockfile-out="$BUILD_DIR/conan.lock" -pr:b general-ci-cd -pr:h general-ci-cd || {
    echo -e "${YELLOW}⚠️ Using default profile if general-ci-cd not found...${NC}"
    conan lock create conanfile.py --version "$BUILD_VERSION" --lockfile=conan.lock --lockfile-out="$BUILD_DIR/conan.lock"
}

echo -e "${BLUE}📦 Installing dependencies with tests enabled...${NC}"
conan install conanfile.py --lockfile="$BUILD_DIR/conan.lock" -of "$BUILD_DIR" --build=missing -pr:b general-ci-cd -pr:h general-ci-cd -o tests=True || {
    echo -e "${YELLOW}⚠️ Using default profile if general-ci-cd not found...${NC}"
    conan install conanfile.py --lockfile="$BUILD_DIR/conan.lock" -of "$BUILD_DIR" --build=missing -o tests=True
}

echo -e "${BLUE}⚙️ Configuring CMake with tests enabled...${NC}"
cmake --preset conan-release \
     -DCMAKE_VERBOSE_MAKEFILE=ON \
     -DGLOBAL_BUILD=ON \
     -DENABLE_TEST=ON \
     -DCMAKE_BUILD_TYPE=Release

echo -e "${BLUE}🔨 Building project...${NC}"
cmake --build --preset conan-release -j4

echo -e "${BLUE}🧪 Running tests with sanitizers...${NC}"
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

echo -e "${BLUE}🧪 Running tests with $SANITIZER_NAME...${NC}"
echo -e "${YELLOW}⚠️ Tests may run slower due to sanitizer overhead${NC}"

# Run tests with extended timeout for sanitizers
if timeout 1800 ctest --output-on-failure --parallel 2; then
    echo ""
    echo -e "${GREEN}✅ All tests passed with $SANITIZER_NAME!${NC}"
    echo -e "${GREEN}🎉 No memory errors, undefined behavior, or race conditions detected!${NC}"
else
    echo ""
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
echo -e "${BLUE}🔬 Sanitizer testing completed!${NC}"
echo -e "${BLUE}💡 Tips:${NC}"
echo "  • Run different sanitizers separately for better performance"
echo "  • Review any sanitizer reports carefully"
echo "  • AddressSanitizer is great for memory issues"
echo "  • UndefinedBehaviorSanitizer catches subtle bugs"
echo "  • ThreadSanitizer is essential for multithreaded code"
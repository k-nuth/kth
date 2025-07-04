#!/bin/bash

# Pretty Test Runner for KTH Project
# Uses Catch2 reporters for beautiful output

set -e
# set -x

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m' # No Color

# Test configuration
BUILD_DIR="build/build/Release"
REPORTER="compact"  # Options: compact, console, json, junit, xml
USE_COLOR="yes"
PARALLEL_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")

# Single test execution mode
SINGLE_TEST_MODE=false
TARGET_SUBMODULE=""
TARGET_TEST=""

# Parse command line options
while [[ $# -gt 0 ]]; do
    case $1 in
        --reporter)
            REPORTER="$2"
            shift 2
            ;;
        --no-color)
            USE_COLOR="no"
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --verbose)
            REPORTER="console"
            shift
            ;;
        --test)
            SINGLE_TEST_MODE=true
            if [[ "$2" == *":"* ]]; then
                # Format: submodule:test_string
                TARGET_SUBMODULE="${2%%:*}"
                TARGET_TEST="${2##*:}"
            else
                # Just test string, search in all submodules
                TARGET_TEST="$2"
            fi
            shift 2
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --reporter REPORTER    Use specific Catch2 reporter (compact, console, json, junit, xml)"
            echo "  --no-color            Disable colors"
            echo "  --build-dir DIR       Build directory (default: build/build/Release)"
            echo "  --verbose             Use verbose console reporter"
            echo "  --test TEST           Run specific Catch2 test:"
            echo "                          - 'test string' (search in all submodules)"
            echo "                          - 'submodule:test string' (run in specific submodule)"
            echo "                        Submodules: secp256k1, infrastructure, domain, consensus,"
            echo "                                   database, blockchain, network, node, c-api"
            echo "                        Examples:"
            echo "                          --test \"payment_address cashAddr mainnet from string\""
            echo "                          --test \"domain:payment_address cashAddr mainnet from string\""
            echo "                          --test \"secp256k1:*\" (all secp256k1 tests)"
            echo "  --help                Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Print header
echo -e "${CYAN}╔══════════════════════════════════════╗${NC}"
echo -e "${CYAN}║${WHITE}        KTH Test Suite Runner         ${CYAN}║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════╝${NC}"
echo ""

# Find all test executables
if [[ ! -d "$BUILD_DIR" ]]; then
    echo -e "${RED}❌ Build directory not found: $BUILD_DIR${NC}"
    echo -e "${YELLOW}💡 Make sure you've built the project first${NC}"
    exit 1
fi

# Discover all test executables (compatible with macOS and Linux)
ALL_TEST_EXECUTABLES=($(find "$BUILD_DIR" -name "*test*" -type f -perm +111 -not -path "*/CMakeFiles/*" | sort))

# Define the execution order
ORDERED_TEST_NAMES=(
    "secp256k1-tests"
    "secp256k1-exhaustive_tests"
    "kth_infrastructure_test"
    "kth_consensus_test"
    "kth_domain_test"
    "kth_database_test"
    "kth_blockchain_test"
    "kth_network_test"
    "kth_node_test"
    "kth_capi_test"
)

# Function to find executable by name
find_executable() {
    local name="$1"
    for exec in "${ALL_TEST_EXECUTABLES[@]}"; do
        if [[ "$(basename "$exec")" == "$name" ]]; then
            echo "$exec"
            return 0
        fi
    done
    return 1
}

# Build ordered list of executables
TEST_EXECUTABLES=()
MISSING_TESTS=()

# Add ordered tests
for test_name in "${ORDERED_TEST_NAMES[@]}"; do
    if exec_path=$(find_executable "$test_name"); then
        TEST_EXECUTABLES+=("$exec_path")
    else
        MISSING_TESTS+=("$test_name")
    fi
done

# Function to map submodule name to test executables
get_submodule_executables() {
    local submodule="$1"
    local executables=()
    
    case "$submodule" in
        "secp256k1")
            for exec in "${ALL_TEST_EXECUTABLES[@]}"; do
                if [[ "$(basename "$exec")" == secp256k1-* ]]; then
                    executables+=("$exec")
                fi
            done
            ;;
        "infrastructure"|"domain"|"consensus"|"database"|"blockchain"|"network"|"node")
            local target_name="kth_${submodule}_test"
            for exec in "${ALL_TEST_EXECUTABLES[@]}"; do
                if [[ "$(basename "$exec")" == "$target_name" ]]; then
                    executables+=("$exec")
                    break
                fi
            done
            ;;
        "c-api")
            local target_name="kth_c_api_test"
            for exec in "${ALL_TEST_EXECUTABLES[@]}"; do
                if [[ "$(basename "$exec")" == "$target_name" ]]; then
                    executables+=("$exec")
                    break
                fi
            done
            ;;
        *)
            echo -e "${RED}❌ Unknown submodule: $submodule${NC}" >&2
            return 1
            ;;
    esac
    
    printf '%s\n' "${executables[@]}"
}

# Handle single test mode
if [[ "$SINGLE_TEST_MODE" == true ]]; then
    SINGLE_TEST_EXECUTABLES=()
    
    if [[ -n "$TARGET_SUBMODULE" ]]; then
        # Specific submodule requested
        echo -e "${BLUE}🎯 Running test \"${WHITE}$TARGET_TEST${BLUE}\" in submodule \"${WHITE}$TARGET_SUBMODULE${BLUE}\"${NC}"
        
        # Use a while loop instead of mapfile for compatibility
        SINGLE_TEST_EXECUTABLES=()
        while IFS= read -r exec_path; do
            if [[ -n "$exec_path" ]]; then
                SINGLE_TEST_EXECUTABLES+=("$exec_path")
            fi
        done < <(get_submodule_executables "$TARGET_SUBMODULE")
        
        if [[ ${#SINGLE_TEST_EXECUTABLES[@]} -eq 0 ]]; then
            echo -e "${RED}❌ No test executables found for submodule: $TARGET_SUBMODULE${NC}"
            exit 1
        fi
    else
        # Search in all submodules
        echo -e "${BLUE}🔍 Searching for test \"${WHITE}$TARGET_TEST${BLUE}\" in all submodules${NC}"
        SINGLE_TEST_EXECUTABLES=("${ALL_TEST_EXECUTABLES[@]}")
    fi
    
    TEST_EXECUTABLES=("${SINGLE_TEST_EXECUTABLES[@]}")
else
    # Add any remaining kth_*_test executables not in the ordered list
    for exec in "${ALL_TEST_EXECUTABLES[@]}"; do
        exec_name=$(basename "$exec")
        if [[ "$exec_name" == kth_*_test ]]; then
            # Check if already in our ordered list
            already_added=false
            for ordered_exec in "${TEST_EXECUTABLES[@]}"; do
                if [[ "$(basename "$ordered_exec")" == "$exec_name" ]]; then
                    already_added=true
                    break
                fi
            done
            if [[ "$already_added" == false ]]; then
                TEST_EXECUTABLES+=("$exec")
            fi
        fi
    done
fi

if [[ ${#TEST_EXECUTABLES[@]} -eq 0 ]]; then
    echo -e "${RED}❌ No test executables found in $BUILD_DIR${NC}"
    exit 1
fi

echo -e "${BLUE}🔍 Found ${#TEST_EXECUTABLES[@]} test executable(s) (in execution order):${NC}"
for exec in "${TEST_EXECUTABLES[@]}"; do
    exec_name=$(basename "$exec")
    echo -e "   ${CYAN}•${NC} $exec_name"
done

# Show warnings for missing tests
if [[ ${#MISSING_TESTS[@]} -gt 0 ]]; then
    echo ""
    echo -e "${YELLOW}⚠️  Missing test executables:${NC}"
    for missing_test in "${MISSING_TESTS[@]}"; do
        echo -e "   ${YELLOW}•${NC} $missing_test ${YELLOW}(not found)${NC}"
    done
fi
echo ""

# Prepare base Catch2 arguments
CATCH2_ARGS="--reporter $REPORTER"

# Add test filter for single test mode
if [[ "$SINGLE_TEST_MODE" == true ]]; then
    CATCH2_ARGS="$CATCH2_ARGS \"$TARGET_TEST\""
fi

# Test which colour option is supported by trying with the first executable
if [[ ${#TEST_EXECUTABLES[@]} -gt 0 && "$USE_COLOR" == "yes" ]]; then
    first_test="${TEST_EXECUTABLES[0]}"
    if "$first_test" --help 2>/dev/null | grep -q "\-\-use-colour"; then
        CATCH2_ARGS="$CATCH2_ARGS --use-colour yes"
    elif "$first_test" --help 2>/dev/null | grep -q "\-\-colour-mode"; then
        CATCH2_ARGS="$CATCH2_ARGS --colour-mode ansi"
    fi
    # If neither option exists, Catch2 will auto-detect colours
fi

# Add success/failure summary for compact reporter
if [[ "$REPORTER" == "compact" ]]; then
    CATCH2_ARGS="$CATCH2_ARGS --success"
fi

# Statistics
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
START_TIME=$(date +%s)

# Function to run a single test
run_test() {
    local test_exec="$1"
    local test_name=$(basename "$test_exec")
    
    echo -e "${MAGENTA}🧪 Running: ${WHITE}$test_name${NC}"
    echo -e "${CYAN}─────────────────────────────────────${NC}"
    
    # Run the test and capture exit code (with timeout handling for macOS/Linux)
    if command -v timeout >/dev/null 2>&1; then
        # Linux/GNU timeout
        eval "timeout 300 \"$test_exec\" $CATCH2_ARGS"
        local exit_code=$?
    elif command -v gtimeout >/dev/null 2>&1; then
        # macOS with GNU coreutils (brew install coreutils)
        eval "gtimeout 300 \"$test_exec\" $CATCH2_ARGS"
        local exit_code=$?
    else
        # Fallback: run without timeout on macOS
        eval "\"$test_exec\" $CATCH2_ARGS"
        local exit_code=$?
    fi
    
    echo ""
    if [[ $exit_code -eq 0 ]]; then
        echo -e "${GREEN}✅ $test_name: PASSED${NC}"
        ((PASSED_TESTS++))
    elif [[ $exit_code -eq 124 ]]; then
        echo -e "${RED}⏰ $test_name: TIMEOUT (>300s)${NC}"
        ((FAILED_TESTS++))
    else
        echo -e "${RED}❌ $test_name: FAILED (exit code: $exit_code)${NC}"
        ((FAILED_TESTS++))
    fi
    
    echo ""
}

# Function to extract test count from Catch2 output
extract_test_counts() {
    local output="$1"
    local test_count=$(echo "$output" | grep -o "[0-9]\+ test cases" | grep -o "[0-9]\+" || echo "0")
    TOTAL_TESTS=$((TOTAL_TESTS + test_count))
}

# Run tests
echo -e "${YELLOW}🚀 Starting test execution...${NC}"
echo ""

for test_exec in "${TEST_EXECUTABLES[@]}"; do
    # Check if executable exists and is executable
    if [[ -x "$test_exec" ]]; then
        run_test "$test_exec"
    else
        echo -e "${RED}❌ Cannot execute: $test_exec${NC}"
        ((FAILED_TESTS++))
    fi
done

# Calculate execution time
END_TIME=$(date +%s)
EXECUTION_TIME=$((END_TIME - START_TIME))

# Print summary
echo -e "${CYAN}╔══════════════════════════════════════╗${NC}"
echo -e "${CYAN}║${WHITE}            Test Summary              ${CYAN}║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════╝${NC}"
echo ""
echo -e "${WHITE}📊 Results:${NC}"
echo -e "   ${GREEN}✅ Passed:  $PASSED_TESTS${NC}"
echo -e "   ${RED}❌ Failed:  $FAILED_TESTS${NC}"
echo -e "   ${YELLOW}⏱️  Time:    ${EXECUTION_TIME}s${NC}"
echo ""

# Final status
if [[ $FAILED_TESTS -eq 0 ]]; then
    echo -e "${GREEN}🎉 All tests passed! 🎉${NC}"
    exit 0
else
    echo -e "${RED}💥 $FAILED_TESTS test(s) failed!${NC}"
    exit 1
fi
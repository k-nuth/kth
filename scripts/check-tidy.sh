#!/bin/bash

# Script to run clang-tidy analysis on C++ code
# This mirrors the same check that runs in GitHub Actions
#
# Usage:
#   ./scripts/check-tidy.sh           # Run analysis only
#   ./scripts/check-tidy.sh --fix     # Apply fixes automatically
#   ./scripts/check-tidy.sh --help    # Show help

set -e

# Parse command line arguments
FIX_MODE=false
SHOW_HELP=false

for arg in "$@"; do
    case $arg in
        --fix)
            FIX_MODE=true
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

if [ "$SHOW_HELP" = true ]; then
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Run clang-tidy analysis on C++ code"
    echo ""
    echo "OPTIONS:"
    echo "  --fix     Apply fixes automatically where possible"
    echo "  --help    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0              # Run analysis only"
    echo "  $0 --fix        # Apply automatic fixes"
    exit 0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

if [ "$FIX_MODE" = true ]; then
    echo -e "${BLUE}🔧 Running clang-tidy analysis with fixes...${NC}"
else
    echo -e "${BLUE}🔍 Running clang-tidy analysis...${NC}"
fi

# Check if clang-tidy is available
CLANG_TIDY=""
if command -v clang-tidy-15 &> /dev/null; then
    CLANG_TIDY="clang-tidy-15"
elif command -v clang-tidy &> /dev/null; then
    CLANG_TIDY="clang-tidy"
else
    echo -e "${RED}❌ clang-tidy not found. Please install it:${NC}"
    echo "  Ubuntu/Debian: sudo apt-get install clang-tidy-15"
    echo "  macOS: brew install llvm"
    exit 1
fi

echo -e "${BLUE}Using: $($CLANG_TIDY --version | head -1)${NC}"

# Test the .clang-tidy file
echo -e "${BLUE}📋 Testing .clang-tidy configuration...${NC}"
if [ -f ".clang-tidy" ]; then
    echo -e "${GREEN}✅ Found .clang-tidy configuration${NC}"
else
    echo -e "${YELLOW}⚠️ No .clang-tidy found, using default rules${NC}"
fi

# Find all C++ source files
echo -e "${BLUE}🔍 Finding C++ source files...${NC}"
find . \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.c" \) \
  -not -path "./build/*" \
  -not -path "./cmake-build-*" \
  -not -path "./.conan/*" \
  -not -path "./third_party/*" \
  -not -path "./ci_utils/*" > cpp_files_all.txt

# Take first 20 files to avoid overwhelming output
head -20 cpp_files_all.txt > cpp_files.txt

total_files=$(wc -l < cpp_files_all.txt)
check_files=$(wc -l < cpp_files.txt)
echo -e "${BLUE}📊 Found $total_files C++ files total, analyzing first $check_files files${NC}"

if [ "$check_files" -eq 0 ]; then
    echo -e "${YELLOW}⚠️ No C++ files found to analyze${NC}"
    rm -f cpp_files_all.txt cpp_files.txt
    exit 0
fi

# Run clang-tidy analysis
tidy_issues=0
fixed_files=0
echo -e "${BLUE}🔍 Analyzing files...${NC}"
echo

while IFS= read -r file; do
    if [ -f "$file" ]; then
        echo -e "${BLUE}🔍 Analyzing: $file${NC}"
        
        if [ "$FIX_MODE" = true ]; then
            # Fix mode: apply fixes
            if $CLANG_TIDY --fix "$file" -- -std=c++17 2>/dev/null; then
                echo -e "${GREEN}✅ Analyzed and fixed: $file${NC}"
                fixed_files=$((fixed_files + 1))
            else
                echo -e "${YELLOW}⚠️ Issues found (some may not be fixable): $file${NC}"
                tidy_issues=$((tidy_issues + 1))
            fi
        else
            # Analysis mode: only report
            if $CLANG_TIDY "$file" -- -std=c++17 2>/dev/null; then
                echo -e "${GREEN}✅ Clean: $file${NC}"
            else
                echo -e "${YELLOW}⚠️ Issues found in: $file${NC}"
                tidy_issues=$((tidy_issues + 1))
            fi
        fi
        echo
    fi
done < cpp_files.txt

# Cleanup
rm -f cpp_files_all.txt cpp_files.txt

echo
echo -e "${BLUE}📊 Clang-tidy Summary:${NC}"

if [ "$FIX_MODE" = true ]; then
    if [ $fixed_files -gt 0 ]; then
        echo -e "${GREEN}✅ Processed $fixed_files files with automatic fixes applied${NC}"
        echo -e "${BLUE}🎉 Clang-tidy fixes applied successfully!${NC}"
        echo
        echo -e "${BLUE}💡 Don't forget to review the changes and commit them:${NC}"
        echo "  git add ."
        echo "  git commit -m \"refactor: apply clang-tidy fixes\""
    fi
    if [ $tidy_issues -gt 0 ]; then
        echo -e "${YELLOW}⚠️ $tidy_issues files had issues that couldn't be auto-fixed${NC}"
        echo -e "${BLUE}🔍 Review these manually${NC}"
    fi
else
    # Analysis mode
    if [ $tidy_issues -gt 0 ]; then
        echo -e "${YELLOW}⚠️ Found potential issues in $tidy_issues files${NC}"
        echo -e "${BLUE}Note: Some warnings may be false positives${NC}"
        echo
        echo -e "${BLUE}🔧 To apply automatic fixes, run:${NC}"
        echo "  ./scripts/check-tidy.sh --fix"
    else
        echo -e "${GREEN}✅ No issues found in analyzed files!${NC}"
        echo -e "${GREEN}🎉 Code analysis passed!${NC}"
    fi
fi
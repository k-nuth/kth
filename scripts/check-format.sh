#!/bin/bash

# Script to check C++ code formatting with clang-format
# This mirrors the same check that runs in GitHub Actions
#
# Usage:
#   ./scripts/check-format.sh           # Check formatting only
#   ./scripts/check-format.sh --fix     # Apply formatting fixes
#   ./scripts/check-format.sh --help    # Show help

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
    echo "Check C++ code formatting with clang-format"
    echo ""
    echo "OPTIONS:"
    echo "  --fix     Apply formatting fixes automatically"
    echo "  --help    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0              # Check formatting only"
    echo "  $0 --fix        # Apply formatting fixes"
    exit 0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

if [ "$FIX_MODE" = true ]; then
    echo "🔧 Fixing C++ code formatting with clang-format..."
else
    echo "🔍 Checking C++ code formatting with clang-format..."
fi

# Check if clang-format is available
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}❌ clang-format not found. Please install it:${NC}"
    echo "  Ubuntu/Debian: sudo apt-get install clang-format"
    echo "  macOS: brew install clang-format"
    echo "  Or use clang-format-15 specifically if available"
    exit 1
fi

# Use clang-format-15 if available, otherwise fallback to clang-format
CLANG_FORMAT="clang-format"
if command -v clang-format-15 &> /dev/null; then
    CLANG_FORMAT="clang-format-15"
fi

echo "Using: $($CLANG_FORMAT --version)"

# Test the .clang-format file first
echo "📋 Testing .clang-format configuration..."
if ! echo 'int main(){return 0;}' | $CLANG_FORMAT --style=file --assume-filename=test.cpp > /dev/null 2>&1; then
    echo -e "${RED}❌ Invalid .clang-format configuration${NC}"
    exit 1
fi
echo -e "${GREEN}✅ .clang-format configuration is valid${NC}"

# Find all C++ source files, avoiding broken pipe by using temp files
echo "🔍 Finding C++ source files..."
find . \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.c" \) \
  -not -path "./build/*" \
  -not -path "./cmake-build-*" \
  -not -path "./.conan/*" \
  -not -path "./third_party/*" \
  -not -path "./ci_utils/*" > cpp_files_all.txt

# Take first 50 files to avoid overwhelming the output
head -50 cpp_files_all.txt > cpp_files.txt

total_files=$(wc -l < cpp_files_all.txt)
check_files=$(wc -l < cpp_files.txt)
echo "📊 Found $total_files C++ files total, checking first $check_files files"

if [ $check_files -eq 0 ]; then
    echo -e "${YELLOW}⚠️  No C++ files found to check${NC}"
    rm -f cpp_files_all.txt cpp_files.txt
    exit 0
fi

# Check formatting and collect results
format_issues=0
fixed_files=0

if [ "$FIX_MODE" = true ]; then
    echo "🔧 Applying formatting to files..."
else
    echo "🔍 Checking file formatting..."
fi
echo

while IFS= read -r file; do
    if [ -f "$file" ]; then
        if [ "$FIX_MODE" = true ]; then
            # Fix mode: apply formatting
            if ! $CLANG_FORMAT --dry-run --Werror "$file" 2>/dev/null; then
                echo -e "${YELLOW}🔧 Fixing formatting in: $file${NC}"
                $CLANG_FORMAT -i "$file"
                fixed_files=$((fixed_files + 1))
            else
                echo -e "${GREEN}✅ $file${NC} (already formatted)"
            fi
        else
            # Check mode: only report issues
            if ! $CLANG_FORMAT --dry-run --Werror "$file" 2>/dev/null; then
                echo -e "${RED}❌ Formatting issues in: $file${NC}"
                echo -e "${YELLOW}--- Expected format diff:${NC}"
                $CLANG_FORMAT "$file" | diff -u "$file" - || true
                echo
                format_issues=$((format_issues + 1))
            else
                echo -e "${GREEN}✅ $file${NC}"
            fi
        fi
    fi
done < cpp_files.txt

# Cleanup
rm -f cpp_files_all.txt cpp_files.txt

echo
echo "📊 Summary:"

if [ "$FIX_MODE" = true ]; then
    if [ $fixed_files -gt 0 ]; then
        echo -e "${GREEN}✅ Fixed formatting in $fixed_files files${NC}"
        echo "🎉 Code formatting fixes applied successfully!"
        echo
        echo "💡 Don't forget to review the changes and commit them:"
        echo "  git add ."
        echo "  git commit -m \"style: apply clang-format fixes\""
    else
        echo -e "${GREEN}✅ All checked files were already properly formatted!${NC}"
        echo "🎉 No fixes needed!"
    fi
else
    # Check mode
    if [ $format_issues -gt 0 ]; then
        echo -e "${RED}❌ Found $format_issues files with formatting issues${NC}"
        echo
        echo "🔧 To fix formatting issues, run:"
        echo "  ./scripts/check-format.sh --fix"
        echo
        echo "💡 Or fix individual files:"
        echo "  $CLANG_FORMAT -i <file>"
        echo
        exit 1
    else
        echo -e "${GREEN}✅ All checked files are properly formatted!${NC}"
        echo "🎉 Code formatting check passed!"
    fi
fi
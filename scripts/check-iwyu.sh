#!/bin/bash

# Script to run include-what-you-use analysis on C++ code
# This helps optimize #include dependencies and reduce compile times
#
# Usage:
#   ./scripts/check-iwyu.sh           # Run analysis only
#   ./scripts/check-iwyu.sh --fix     # Apply fixes automatically (experimental)
#   ./scripts/check-iwyu.sh --help    # Show help

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
    echo "Run include-what-you-use analysis to optimize #include dependencies"
    echo ""
    echo "OPTIONS:"
    echo "  --fix     Apply include fixes automatically (EXPERIMENTAL - use with caution)"
    echo "  --help    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0              # Analyze includes and show suggestions"
    echo "  $0 --fix        # Apply automatic include fixes (review changes!)"
    echo ""
    echo "IWYU helps by:"
    echo "  • Removing unnecessary #include statements"
    echo "  • Adding missing #include statements for used symbols"
    echo "  • Replacing indirect includes with direct ones"
    echo "  • Reducing compilation dependencies and build times"
    echo ""
    echo "⚠️  WARNING: --fix mode is experimental. Always review changes!"
    exit 0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

if [ "$FIX_MODE" = true ]; then
    echo -e "${BLUE}🔧 Running include-what-you-use analysis with fixes...${NC}"
    echo -e "${YELLOW}⚠️  WARNING: This will modify your files! Make sure you have backups.${NC}"
    read -p "Continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
else
    echo -e "${BLUE}🔍 Running include-what-you-use analysis...${NC}"
fi

# Check if IWYU is available
IWYU_TOOL=""
if command -v include-what-you-use &> /dev/null; then
    IWYU_TOOL="include-what-you-use"
elif command -v iwyu &> /dev/null; then
    IWYU_TOOL="iwyu"
else
    echo -e "${RED}❌ include-what-you-use not found. Please install it:${NC}"
    echo "  Ubuntu/Debian: sudo apt-get install iwyu"
    echo "  macOS: brew install include-what-you-use"
    echo "  Or build from source: https://github.com/include-what-you-use/include-what-you-use"
    exit 1
fi

echo -e "${BLUE}Using: $IWYU_TOOL${NC}"

# Check if source directory exists
if [ ! -d "src" ]; then
    echo -e "${RED}❌ Source directory 'src' not found${NC}"
    echo "Please run this script from the project root directory"
    exit 1
fi

echo -e "${BLUE}📊 Analyzing include dependencies...${NC}"

# Find C++ source files (focus on .cpp files for IWYU)
find . -name "*.cpp" \
  -not -path "./build/*" \
  -not -path "./cmake-build-*" \
  -not -path "./.conan/*" \
  -not -path "./third_party/*" \
  -not -path "./ci_utils/*" > cpp_source_files.txt

total_files=$(wc -l < cpp_source_files.txt)
echo -e "${BLUE}📊 Found $total_files C++ source files to analyze${NC}"

if [ "$total_files" -eq 0 ]; then
    echo -e "${YELLOW}⚠️ No C++ source files found to analyze${NC}"
    rm -f cpp_source_files.txt
    exit 0
fi

# Prepare include paths
INCLUDE_PATHS=""
if [ -d "src" ]; then
    INCLUDE_PATHS="$INCLUDE_PATHS -I./src"
fi
if [ -d "include" ]; then
    INCLUDE_PATHS="$INCLUDE_PATHS -I./include"
fi

# Find additional include directories
find src -type d -name include 2>/dev/null | while read -r dir; do
    echo "-I./$dir"
done > additional_includes.txt

if [ -s additional_includes.txt ]; then
    ADDITIONAL_INCLUDES=$(tr '\n' ' ' < additional_includes.txt)
    INCLUDE_PATHS="$INCLUDE_PATHS $ADDITIONAL_INCLUDES"
fi

echo -e "${BLUE}🔍 Using include paths: $INCLUDE_PATHS${NC}"

iwyu_output="iwyu_results.txt"
iwyu_suggestions=0
processed_files=0
files_with_suggestions=0

echo -e "${BLUE}🔍 Analyzing files...${NC}"
echo

# Run IWYU on each file
while IFS= read -r file; do
    if [ -f "$file" ]; then
        echo -e "${BLUE}🔍 Analyzing: $file${NC}"
        processed_files=$((processed_files + 1))
        
        # Create temporary output file for this file
        temp_output="iwyu_temp_$$.txt"
        
        # Run IWYU
        $IWYU_TOOL \
            -std=c++17 \
            $INCLUDE_PATHS \
            "$file" \
            2> "$temp_output" || true
        
        # Check if IWYU has suggestions
        if grep -q "should add\|should remove\|full include-list" "$temp_output" 2>/dev/null; then
            echo -e "${YELLOW}💡 Suggestions found for: $file${NC}"
            files_with_suggestions=$((files_with_suggestions + 1))
            
            # Append to main output
            echo "========== $file ==========" >> "$iwyu_output"
            cat "$temp_output" >> "$iwyu_output"
            echo >> "$iwyu_output"
            
            if [ "$FIX_MODE" = true ]; then
                # Try to apply fixes (this is experimental)
                echo -e "${YELLOW}🔧 Attempting to apply fixes to: $file${NC}"
                
                # This is a simplified fix attempt - in reality, you'd use fix_includes.py
                echo -e "${YELLOW}⚠️  Automatic fixing not yet implemented - manual review required${NC}"
            fi
        else
            echo -e "${GREEN}✅ No changes needed: $file${NC}"
        fi
        
        rm -f "$temp_output"
    fi
done < cpp_source_files.txt

echo
echo -e "${BLUE}📊 IWYU Analysis Summary:${NC}"
echo -e "  ${BLUE}Files analyzed: $processed_files${NC}"
echo -e "  ${BLUE}Files with suggestions: $files_with_suggestions${NC}"

if [ "$files_with_suggestions" -gt 0 ] && [ -f "$iwyu_output" ] && [ -s "$iwyu_output" ]; then
    echo
    echo -e "${BLUE}💡 Include optimization opportunities found!${NC}"
    echo
    echo -e "${BLUE}📄 Detailed suggestions:${NC}"
    echo "============================================"
    cat "$iwyu_output"
    echo "============================================"
    echo
    echo -e "${BLUE}🔧 To apply these suggestions:${NC}"
    echo "1. Review each suggestion carefully"
    echo "2. Manually edit the files to add/remove includes as suggested"
    echo "3. Test that the code still compiles after changes"
    echo "4. Run this script again to verify improvements"
    echo
    echo -e "${BLUE}💡 Benefits of applying these changes:${NC}"
    echo "• Faster compilation times"
    echo "• Cleaner dependencies"
    echo "• Better code organization"
    echo "• Reduced risk of circular dependencies"
    
    echo
    echo -e "${BLUE}📄 Full report saved to: $iwyu_output${NC}"
else
    echo
    echo -e "${GREEN}✅ No include optimizations suggested!${NC}"
    echo -e "${GREEN}🎉 Include dependencies are well-organized!${NC}"
    rm -f "$iwyu_output"
fi

# Cleanup
rm -f cpp_source_files.txt additional_includes.txt

echo
echo -e "${BLUE}🔍 IWYU specializes in:${NC}"
echo "• Finding unnecessary #include statements"
echo "• Detecting missing #include statements"
echo "• Optimizing header dependencies"
echo "• Reducing compilation time and build dependencies"
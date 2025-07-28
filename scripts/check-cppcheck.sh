#!/bin/bash

# Script to run cppcheck analysis on C++ code
# This mirrors the same check that runs in GitHub Actions
#
# Usage:
#   ./scripts/check-cppcheck.sh           # Run analysis only
#   ./scripts/check-cppcheck.sh --xml     # Output XML format for IDEs
#   ./scripts/check-cppcheck.sh --help    # Show help

set -e

# Parse command line arguments
XML_MODE=false
SHOW_HELP=false

for arg in "$@"; do
    case $arg in
        --xml)
            XML_MODE=true
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
    echo "Run cppcheck static analysis on C++ code"
    echo ""
    echo "OPTIONS:"
    echo "  --xml     Output results in XML format for IDE integration"
    echo "  --help    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0              # Run analysis with human-readable output"
    echo "  $0 --xml        # Generate XML report for IDEs"
    echo ""
    echo "Cppcheck finds:"
    echo "  • Memory leaks and buffer overflows"
    echo "  • Null pointer dereferences"
    echo "  • Uninitialized variables"
    echo "  • Dead code and unused functions"
    echo "  • Style and performance issues"
    exit 0
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🔍 Running cppcheck static analysis...${NC}"

# Check if cppcheck is available
if ! command -v cppcheck &> /dev/null; then
    echo -e "${RED}❌ cppcheck not found. Please install it:${NC}"
    echo "  Ubuntu/Debian: sudo apt-get install cppcheck"
    echo "  macOS: brew install cppcheck"
    exit 1
fi

echo -e "${BLUE}Using: $(cppcheck --version)${NC}"

# Check if source directory exists
if [ ! -d "src" ]; then
    echo -e "${RED}❌ Source directory 'src' not found${NC}"
    echo "Please run this script from the project root directory"
    exit 1
fi

echo -e "${BLUE}📊 Analyzing C++ code for bugs and security issues...${NC}"

# Prepare output files
if [ "$XML_MODE" = true ]; then
    output_file="cppcheck_results.xml"
    echo -e "${BLUE}📄 Results will be saved to: $output_file${NC}"
else
    output_file="cppcheck_results.txt"
fi

# Run cppcheck with comprehensive checks
echo -e "${BLUE}🔍 Running analysis...${NC}"

if [ "$XML_MODE" = true ]; then
    # XML output for IDE integration
    cppcheck \
        --enable=warning,style,performance,portability,information \
        --std=c++17 \
        --inline-suppr \
        --xml \
        --xml-version=2 \
        --template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
        --suppress=missingIncludeSystem \
        --suppress=unmatchedSuppression \
        --suppress=unusedFunction \
        --suppress=noExplicitConstructor \
        --error-exitcode=0 \
        src/ \
        2> "$output_file" || true
    
    echo -e "${GREEN}✅ XML report generated: $output_file${NC}"
    echo -e "${BLUE}💡 Import this file into your IDE for integrated warnings${NC}"
    
else
    # Human-readable output
    cppcheck \
        --enable=warning,style,performance,portability,information \
        --std=c++17 \
        --inline-suppr \
        --quiet \
        --template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
        --suppress=missingIncludeSystem \
        --suppress=unmatchedSuppression \
        --suppress=unusedFunction \
        --suppress=noExplicitConstructor \
        --error-exitcode=0 \
        src/ \
        2>&1 | tee "$output_file" || true
    
    # Analyze results
    if [ -f "$output_file" ] && [ -s "$output_file" ]; then
        # Count different types of issues
        total_issues=$(wc -l < "$output_file" 2>/dev/null || echo "0")
        error_issues=$(grep -c ": error:" "$output_file" 2>/dev/null || echo "0")
        warning_issues=$(grep -c ": warning:" "$output_file" 2>/dev/null || echo "0")
        style_issues=$(grep -c ": style:" "$output_file" 2>/dev/null || echo "0")
        performance_issues=$(grep -c ": performance:" "$output_file" 2>/dev/null || echo "0")
        portability_issues=$(grep -c ": portability:" "$output_file" 2>/dev/null || echo "0")
        information_issues=$(grep -c ": information:" "$output_file" 2>/dev/null || echo "0")
        
        echo ""
        echo -e "${BLUE}📊 Cppcheck Analysis Summary:${NC}"
        echo -e "  ${BLUE}Total issues: $total_issues${NC}"
        echo -e "  ${RED}Errors: $error_issues${NC}"
        echo -e "  ${YELLOW}Warnings: $warning_issues${NC}"
        echo -e "  ${BLUE}Style: $style_issues${NC}"
        echo -e "  ${GREEN}Performance: $performance_issues${NC}"
        echo -e "  ${BLUE}Portability: $portability_issues${NC}"
        echo -e "  ${BLUE}Information: $information_issues${NC}"
        
        if [ "$total_issues" -gt 0 ]; then
            echo ""
            echo -e "${BLUE}🔍 Issues by severity:${NC}"
            
            if [ "$error_issues" -gt 0 ]; then
                echo -e "${RED}❌ ERRORS (should be fixed):${NC}"
                grep ": error:" "$output_file" | head -10
                echo ""
            fi
            
            if [ "$warning_issues" -gt 0 ]; then
                echo -e "${YELLOW}⚠️ WARNINGS (review recommended):${NC}"
                grep ": warning:" "$output_file" | head -5
                echo ""
            fi
            
            if [ "$performance_issues" -gt 0 ]; then
                echo -e "${GREEN}🚀 PERFORMANCE (optimization opportunities):${NC}"
                grep ": performance:" "$output_file" | head -3
                echo ""
            fi
            
            echo -e "${BLUE}💡 Tips:${NC}"
            echo "  • Focus on errors first, then warnings"
            echo "  • Style issues are optional but improve consistency"
            echo "  • Some warnings may be false positives"
            echo ""
            echo -e "${BLUE}📄 Full report saved to: $output_file${NC}"
            
        else
            echo ""
            echo -e "${GREEN}✅ No issues found!${NC}"
            echo -e "${GREEN}🎉 Cppcheck analysis passed!${NC}"
            rm -f "$output_file"
        fi
    else
        echo ""
        echo -e "${GREEN}✅ No issues found!${NC}"
        echo -e "${GREEN}🎉 Cppcheck analysis passed!${NC}"
    fi
fi

echo ""
echo -e "${BLUE}🔍 Cppcheck specializes in finding:${NC}"
echo "  • Memory leaks and buffer overflows"
echo "  • Null pointer dereferences"
echo "  • Uninitialized variables"
echo "  • Dead code and unused functions"
echo "  • C++11/14/17 modernization opportunities"
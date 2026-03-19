#!/usr/bin/env bash
# Find dead files - source files that exist but are not referenced in CMakeLists.txt
#
# Usage: ./scripts/analysis/find-dead-files.sh [src_dir]
#
# This script finds .cpp, .hpp, and .ipp files that are not referenced
# in any CMakeLists.txt file, which may indicate dead/unused code.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC_DIR="${1:-$REPO_ROOT/src}"

if [[ ! -d "$SRC_DIR" ]]; then
    echo "Error: Directory $SRC_DIR does not exist"
    exit 1
fi

echo "==================================================================="
echo "Dead File Analysis"
echo "==================================================================="
echo "Source directory: $SRC_DIR"
echo "-------------------------------------------------------------------"
echo ""

# Collect all CMakeLists.txt content into a single temp file for faster searching
CMAKE_CONTENT=$(mktemp)
find "$SRC_DIR" -name "CMakeLists.txt" -exec cat {} + > "$CMAKE_CONTENT"

# Also check for files that might be included via wildcards or other cmake files
CMAKE_ALL=$(mktemp)
find "$SRC_DIR" -name "*.cmake" -exec cat {} + >> "$CMAKE_CONTENT" 2>/dev/null || true

dead_files=()
commented_files=()
referenced_files=()

# Find all source files
while IFS= read -r -d '' file; do
    # Get relative path from src directory
    rel_path="${file#$SRC_DIR/}"

    # Get just the filename
    filename=$(basename "$file")

    # Check if file is referenced in any CMakeLists.txt
    # We check for both the relative path and just the filename
    if grep -q "$filename" "$CMAKE_CONTENT" 2>/dev/null; then
        # Check if it's commented out (line starts with #)
        if grep -E "^[[:space:]]*#.*$filename" "$CMAKE_CONTENT" >/dev/null 2>&1; then
            # Check if there's also an uncommented reference
            if grep -E "^[[:space:]]*[^#]*$filename" "$CMAKE_CONTENT" | grep -v "^[[:space:]]*#" >/dev/null 2>&1; then
                referenced_files+=("$rel_path")
            else
                commented_files+=("$rel_path")
            fi
        else
            referenced_files+=("$rel_path")
        fi
    else
        dead_files+=("$rel_path")
    fi
done < <(find "$SRC_DIR" -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.c" -o -name "*.h" \) -print0 | sort -z)

# Clean up
rm -f "$CMAKE_CONTENT" "$CMAKE_ALL"

# Report results
echo "DEAD FILES (not referenced in any CMakeLists.txt):"
echo "-------------------------------------------------------------------"
if [[ ${#dead_files[@]} -eq 0 ]]; then
    echo "  (none found)"
else
    for f in "${dead_files[@]}"; do
        echo "  $f"
    done
fi
echo ""

echo "COMMENTED OUT FILES (referenced but commented with #):"
echo "-------------------------------------------------------------------"
if [[ ${#commented_files[@]} -eq 0 ]]; then
    echo "  (none found)"
else
    for f in "${commented_files[@]}"; do
        echo "  $f"
    done
fi
echo ""

echo "==================================================================="
echo "Summary:"
echo "  Total source files:  $(( ${#dead_files[@]} + ${#commented_files[@]} + ${#referenced_files[@]} ))"
echo "  Referenced:          ${#referenced_files[@]}"
echo "  Commented out:       ${#commented_files[@]}"
echo "  Dead (unreferenced): ${#dead_files[@]}"
echo "==================================================================="

# Exit with error if dead files found (useful for CI)
if [[ ${#dead_files[@]} -gt 0 || ${#commented_files[@]} -gt 0 ]]; then
    exit 1
fi

#!/bin/bash
# Script to analyze container usage in the codebase
# Helps identify where std::map, std::set, std::unordered_* are used
# for potential migration to boost::unordered_flat_*

set -e

echo "==================================================="
echo "Container Usage Analysis"
echo "==================================================="
echo ""

echo "Summary:"
echo "--------"
echo -n "std::map: "
grep -rn "std::map[^p]" src --include="*.cpp" --include="*.hpp" --include="*.h" | grep -v "std::map_" | wc -l

echo -n "std::set: "
grep -rn "std::set[^_]" src --include="*.cpp" --include="*.hpp" --include="*.h" | wc -l

echo -n "std::unordered_map: "
grep -rn "std::unordered_map" src --include="*.cpp" --include="*.hpp" --include="*.h" | wc -l

echo -n "std::unordered_set: "
grep -rn "std::unordered_set" src --include="*.cpp" --include="*.hpp" --include="*.h" | wc -l

echo -n "boost::unordered_map: "
grep -rn "boost::unordered_map" src --include="*.cpp" --include="*.hpp" --include="*.h" | wc -l

echo -n "boost::unordered_set: "
grep -rn "boost::unordered_set" src --include="*.cpp" --include="*.hpp" --include="*.h" | wc -l

echo ""
echo "==================================================="
echo "Files with most container usage (Top 20):"
echo "==================================================="
grep -rn "std::map[^p]\|std::set[^_]\|std::unordered" src --include="*.cpp" --include="*.hpp" --include="*.h" | \
    grep -v "std::map_" | \
    cut -d: -f1 | \
    sort | \
    uniq -c | \
    sort -rn | \
    head -20 | \
    awk '{printf "%3d occurrences: %s\n", $1, $2}'

echo ""
echo "==================================================="
echo "Breakdown by directory:"
echo "==================================================="

for dir in src/*/; do
    count=$(grep -rn "std::map[^p]\|std::set[^_]\|std::unordered" "$dir" --include="*.cpp" --include="*.hpp" --include="*.h" 2>/dev/null | grep -v "std::map_" | wc -l | tr -d ' ')
    if [ "$count" -gt 0 ]; then
        dirname=$(basename "$dir")
        printf "%-20s: %3d occurrences\n" "$dirname" "$count"
    fi
done | sort -t: -k2 -rn

echo ""
echo "==================================================="
echo "Detailed report saved to: container-report.txt"
echo "==================================================="
echo ""
echo "Next steps:"
echo "1. Review container-report.txt for detailed line-by-line locations"
echo "2. Decide which containers to migrate:"
echo "   - std::unordered_* -> boost::unordered_flat_* (straightforward)"
echo "   - std::map -> boost::unordered_flat_map (only if ordering not needed)"
echo "   - std::set -> boost::unordered_flat_set (only if ordering not needed)"
echo "3. Use migrate-containers.sh to perform the migration"
echo ""

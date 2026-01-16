#!/usr/bin/env bash

# Script to update version numbers in README files
# Usage: ./update_readme_versions.sh <new_version>

if [ $# -eq 0 ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.68.0"
    exit 1
fi

NEW_VERSION=$1

# Validate version format (x.y.z)
if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Version must be in format x.y.z (e.g., 0.68.0)"
    exit 1
fi

echo "Updating README files to version: $NEW_VERSION"

# Detect OS for sed compatibility
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    SED_CMD="sed -i ''"
else
    # Linux
    SED_CMD="sed -i"
fi

# Function to update version in a README file
update_readme() {
    local file=$1
    local package=$2
    
    if [ -f "$file" ]; then
        echo "Updating $file for package $package..."
        
        # Check if the file contains the package pattern
        if grep -q "\-\-requires=${package}/" "$file"; then
            # Update version pattern --requires=package/x.y.z
            if [[ "$OSTYPE" == "darwin"* ]]; then
                sed -i '' "s/--requires=${package}\/[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*/--requires=${package}\/${NEW_VERSION}/g" "$file"
            else
                sed -i "s/--requires=${package}\/[0-9]\+\.[0-9]\+\.[0-9]\+/--requires=${package}\/${NEW_VERSION}/g" "$file"
            fi
            echo "✅ Updated $file for package $package"
        else
            echo "⚠️  No $package version found in $file"
        fi
    else
        echo "❌ File not found: $file"
    fi
}

# Update main node-exe README (uses kth package)
update_readme "src/node-exe/README.md" "kth"

# Update individual library READMEs
update_readme "src/blockchain/README.md" "blockchain"
update_readme "src/consensus/README.md" "consensus"
update_readme "src/database/README.md" "database"
update_readme "src/domain/README.md" "domain"
update_readme "src/infrastructure/README.md" "infrastructure"
update_readme "src/network/README.md" "network"
update_readme "src/node/README.md" "node"
update_readme "src/c-api/README.md" "c-api"
update_readme "src/secp256k1/README.md" "secp256k1"

echo ""
echo "✅ Version update completed!"
echo ""
echo "Files that were modified:"
git diff --name-only src/*/README.md 2>/dev/null || echo "  (no git repository or no changes)"
echo ""
echo "To commit the changes:"
echo "  git add src/*/README.md"
echo "  git commit -m 'docs: update README versions to $NEW_VERSION'"

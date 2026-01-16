#!/usr/bin/env bash

# Script to prepare version synchronization across all Knuth repositories
# Usage: ./scripts/prepare_version_sync.sh <new_version>

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.68.0"
    exit 1
fi

NEW_VERSION="$1"

echo "🔄 Preparing version synchronization to v${NEW_VERSION}"

# List of repositories that need version updates
REPOS=(
    "src/infrastructure"
    "src/domain" 
    "src/consensus"
    "src/database"
    "src/network"
    "src/blockchain"
    "src/node"
    "src/c-api"
    "src/node-exe"
)

echo "📦 Repositories to update:"
for repo in "${REPOS[@]}"; do
    if [ -d "$repo" ]; then
        echo "  ✅ $repo"
    else
        echo "  ❌ $repo (not found)"
    fi
done

echo ""
echo "🚀 This will help ensure all repositories are ready for unified versioning v${NEW_VERSION}"
echo ""
echo "Next steps:"
echo "1. Update version in conanfile.py files if needed"
echo "2. Update any version references in CMakeLists.txt files"
echo "3. Run the release script: ./release.sh ${NEW_VERSION}"
echo ""
echo "The release script will:"
echo "- Use the prepared release notes entry with version sync explanation"
echo "- Generate unified release notes from all repositories" 
echo "- Create releases with synchronized version ${NEW_VERSION}"
echo ""
echo "⚠️  Important: This establishes unified versioning across all Knuth repositories"
echo "   All future releases will use the same version number across repos."

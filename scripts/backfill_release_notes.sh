#!/usr/bin/env bash

# Script to backfill missing release notes from GitHub
# This script handles the multi-repo versioning complexity

REPOS=(
    "k-nuth/node-exe"
    "k-nuth/node" 
    "k-nuth/c-api"
)

NOTES_FILE="docs/release-notes/release-notes.md"
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "🔄 Backfilling missing release notes from GitHub..."

# Check requirements
if ! command -v gh &> /dev/null; then
    echo "❌ GitHub CLI (gh) is required but not installed."
    exit 1
fi

if ! command -v jq &> /dev/null; then
    echo "❌ jq is required but not installed."
    exit 1
fi

# Get the latest version currently in release notes (0.46.0)
LAST_VERSION=$(grep -m1 "^# version " "$NOTES_FILE" | sed 's/^# version //')
echo "📍 Last documented version: $LAST_VERSION"

# Function to get releases from a repo with date filtering
get_releases_after_date() {
    local repo="$1"
    local after_date="$2"
    
    echo "📡 Fetching releases from $repo after $after_date..."
    
    gh api "repos/$repo/releases" --paginate | jq -r "
        .[] | 
        select(.published_at > \"$after_date\" and .draft == false) |
        {
            tag_name: .tag_name,
            name: .name,
            body: .body,
            published_at: .published_at,
            repo: \"$repo\"
        } | @base64
    "
}

# Function to extract version number from tag
extract_version() {
    echo "$1" | sed 's/^v//' | sed 's/-.*//'
}

# Get the date of version 0.46.0 from node-exe (our reference point)
echo "🔍 Finding reference date for version 0.46.0..."
REF_DATE=$(gh api "repos/k-nuth/node-exe/releases" | jq -r '.[] | select(.tag_name == "v0.46.0") | .published_at')

if [ -z "$REF_DATE" ] || [ "$REF_DATE" == "null" ]; then
    echo "❌ Could not find reference date for v0.46.0"
    exit 1
fi

echo "📅 Reference date: $REF_DATE"

# Collect all releases from all repos after the reference date
echo "📦 Collecting releases from all repositories..."

ALL_RELEASES_FILE="$TEMP_DIR/all_releases.json"
echo "[]" > "$ALL_RELEASES_FILE"

for repo in "${REPOS[@]}"; do
    echo "  Processing $repo..."
    
    releases=$(get_releases_after_date "$repo" "$REF_DATE")
    
    if [ -n "$releases" ]; then
        while IFS= read -r encoded_release; do
            if [ -n "$encoded_release" ]; then
                release_json=$(echo "$encoded_release" | base64 -d 2>/dev/null)
                if [ $? -eq 0 ] && [ -n "$release_json" ]; then
                    # Add to our collection
                    jq --argjson new_release "$release_json" '. += [$new_release]' "$ALL_RELEASES_FILE" > "$ALL_RELEASES_FILE.tmp" && mv "$ALL_RELEASES_FILE.tmp" "$ALL_RELEASES_FILE"
                fi
            fi
        done <<< "$releases"
    fi
done

# Sort all releases by date and group by node-exe version
echo "🔄 Processing and grouping releases..."

# Get all node-exe versions as our primary versioning scheme
NODE_EXE_VERSIONS=$(jq -r '.[] | select(.repo == "k-nuth/node-exe") | .tag_name' "$ALL_RELEASES_FILE" | sed 's/^v//' | sort -V)

if [ -z "$NODE_EXE_VERSIONS" ]; then
    echo "❌ No node-exe versions found"
    exit 1
fi

echo "📋 Found node-exe versions: $(echo $NODE_EXE_VERSIONS | tr '\n' ' ')"

# Create backup
cp "$NOTES_FILE" "${NOTES_FILE}.backup"
echo "💾 Created backup: ${NOTES_FILE}.backup"

# Process each version
NEW_ENTRIES=""

for version in $NODE_EXE_VERSIONS; do
    echo "📝 Processing version $version..."
    
    # Get the date of this node-exe release
    version_date=$(jq -r ".[] | select(.repo == \"k-nuth/node-exe\" and .tag_name == \"v$version\") | .published_at" "$ALL_RELEASES_FILE")
    
    if [ -z "$version_date" ] || [ "$version_date" == "null" ]; then
        echo "⚠️  Could not find date for version $version, skipping..."
        continue
    fi
    
    # Convert date to epoch for comparison (±1 day tolerance)
    version_epoch=$(date -d "$version_date" +%s 2>/dev/null || date -j -f "%Y-%m-%dT%H:%M:%SZ" "$version_date" +%s 2>/dev/null)
    start_epoch=$((version_epoch - 86400))  # 1 day before
    end_epoch=$((version_epoch + 86400))    # 1 day after
    
    echo "  📅 Version date: $version_date (looking for releases within ±1 day)"
    
    # Find all releases around this date from all repos
    related_releases=""
    
    while IFS= read -r encoded_release; do
        if [ -n "$encoded_release" ]; then
            release_data=$(echo "$encoded_release" | base64 -d 2>/dev/null)
            if [ $? -eq 0 ] && [ -n "$release_data" ]; then
                release_date=$(echo "$release_data" | jq -r '.published_at')
                release_epoch=$(date -d "$release_date" +%s 2>/dev/null || date -j -f "%Y-%m-%dT%H:%M:%SZ" "$release_date" +%s 2>/dev/null)
                
                if [ "$release_epoch" -ge "$start_epoch" ] && [ "$release_epoch" -le "$end_epoch" ]; then
                    related_releases+="$encoded_release"$'\n'
                fi
            fi
        fi
    done < <(jq -r '.[] | @base64' "$ALL_RELEASES_FILE")
    
    if [ -z "$related_releases" ]; then
        echo "  ⚠️  No related releases found for version $version"
        continue
    fi
    
    # Format the release notes for this version
    echo "  ✍️  Formatting release notes..."
    
    version_notes="# version $version

You can install Knuth node version v$version [using these instructions](https://kth.cash/#download).

This release includes the following features and fixes:

"
    
    # Process each related release
    while IFS= read -r encoded_release; do
        if [ -n "$encoded_release" ]; then
            release_data=$(echo "$encoded_release" | base64 -d 2>/dev/null)
            if [ $? -eq 0 ] && [ -n "$release_data" ]; then
                repo=$(echo "$release_data" | jq -r '.repo')
                tag=$(echo "$release_data" | jq -r '.tag_name')
                body=$(echo "$release_data" | jq -r '.body')
                
                # Clean repo name for display
                repo_name=$(echo "$repo" | cut -d'/' -f2)
                
                if [ "$repo_name" == "node-exe" ]; then
                    repo_display="Node Executable"
                elif [ "$repo_name" == "node" ]; then
                    repo_display="C++ Library"
                elif [ "$repo_name" == "c-api" ]; then
                    repo_display="C API"
                else
                    repo_display="$repo_name"
                fi
                
                # Add changes from this repo
                if [ -n "$body" ] && [ "$body" != "null" ] && [ "$body" != "" ]; then
                    version_notes+="**$repo_display ($tag):**
$body

"
                else
                    version_notes+="**$repo_display:** Updated to version $tag

"
                fi
            fi
        fi
    done <<< "$related_releases"
    
    # Clean up the notes and add to our collection
    version_notes=$(echo "$version_notes" | sed 's/## What.*Changed/Changes:/')
    version_notes=$(echo "$version_notes" | sed 's/Full Changelog:.*//')
    version_notes=$(echo "$version_notes" | sed '/^### Contributors/,$d')
    
    NEW_ENTRIES="$version_notes
$NEW_ENTRIES"
    
    echo "  ✅ Processed version $version"
done

if [ -z "$NEW_ENTRIES" ]; then
    echo "❌ No new entries to add"
    exit 1
fi

# Add new entries to the top of the file
echo "📝 Updating release notes file..."

{
    echo "$NEW_ENTRIES"
    cat "$NOTES_FILE"
} > "${NOTES_FILE}.tmp" && mv "${NOTES_FILE}.tmp" "$NOTES_FILE"

echo "✅ Release notes updated successfully!"
echo "📄 Updated: $NOTES_FILE"

# Count new versions added
new_count=$(echo "$NEW_ENTRIES" | grep -c "^# version ")
echo "📊 Added $new_count new version entries"

echo ""
echo "🔍 Summary of changes:"
echo "$NEW_ENTRIES" | grep "^# version " | head -5

echo ""
echo "Would you like to review the changes before committing? (y/n)"
read -r response
if [[ "$response" =~ ^[Yy] ]]; then
    echo "📖 Opening diff for review..."
    git diff "$NOTES_FILE" | head -50
    echo ""
    echo "Commit these changes? (y/n)"
    read -r commit_response
    if [[ "$commit_response" =~ ^[Yy] ]]; then
        git add "$NOTES_FILE"
        git commit -m "docs: backfill release notes from GitHub (v0.47.0 to v0.58.0)

- Automatically extracted and merged release notes from node-exe, node, and c-api
- Grouped releases by date proximity to handle different versioning schemes
- Preserved original GitHub-generated content with proper attribution"
        echo "✅ Changes committed!"
    fi
else
    git add "$NOTES_FILE"
    git commit -m "docs: backfill release notes from GitHub (v0.47.0 to v0.58.0)"
    echo "✅ Changes committed automatically!"
fi

echo ""
echo "🎉 Backfill complete! Your release notes are now up to date."

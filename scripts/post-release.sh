#!/bin/bash
set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Start ssh-agent if not already running to avoid multiple passphrase prompts
SSH_AGENT_STARTED=false
if [ -z "$SSH_AUTH_SOCK" ]; then
    echo -e "${GREEN}🔐 Starting SSH agent...${NC}"
    eval $(ssh-agent -s)
    SSH_AGENT_STARTED=true
fi

# Add SSH key (will ask for passphrase once)
echo -e "${GREEN}🔑 Adding SSH key...${NC}"
ssh-add ~/.ssh/id_rsa 2>/dev/null || ssh-add ~/.ssh/id_ed25519 2>/dev/null || ssh-add 2>/dev/null || true

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.71.0"
    echo ""
    echo "This script performs the post-CI steps of the release process:"
    echo "  - Merges the release PR"
    echo "  - Creates release notes"
    echo "  - Creates and pushes the release tag"
    echo "  - Cleans up release branch"
    exit 1
fi

VERSION="$1"

# Validate semver format
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "❌ Version must be in semver format (e.g., 0.71.0)"
    exit 1
fi

echo -e "${GREEN}📦 Post-release processing for version ${VERSION}${NC}"

# Verify release branch exists
if ! git ls-remote --heads origin "release/${VERSION}" | grep -q "release/${VERSION}"; then
    echo "❌ Release branch release/${VERSION} does not exist"
    exit 1
fi

# squash merge the PR, do not delete the branch
echo -e "${GREEN}🔀 Merging release PR...${NC}"
gh pr merge --squash --auto "release/${VERSION}"

# switch to master and pull latest changes
echo -e "${GREEN}🔄 Switching to master and pulling latest changes...${NC}"
git checkout master
git pull origin master

# Step 1: Create temporary release to generate notes
echo -e "${GREEN}📝 Creating temporary release to generate notes...${NC}"
TEMP_TAG="temp-v${VERSION}"
git tag -a "${TEMP_TAG}" -m "Temporary tag for release notes generation"
git push origin "${TEMP_TAG}"

gh release create "${TEMP_TAG}" \
    --title "temp-v${VERSION}" \
    --generate-notes \
    --prerelease

# Step 2: Extract the generated notes
echo -e "${GREEN}📤 Extracting generated release notes...${NC}"
RELEASE_NOTES=$(gh release view "${TEMP_TAG}" --json body -q '.body')

if [ -z "$RELEASE_NOTES" ]; then
    echo "❌ Failed to extract release notes"
    exit 1
fi

# Replace temporary tag references with final tag in the release notes
echo -e "${GREEN}🔧 Fixing references in release notes...${NC}"
RELEASE_NOTES=$(echo "$RELEASE_NOTES" | sed "s/temp-v${VERSION}/v${VERSION}/g")

# Step 3: Update local release notes file
echo -e "${GREEN}📄 Updating local release notes file...${NC}"
NOTES_FILE="doc/release-notes/release-notes.md"

# Create a backup
cp "$NOTES_FILE" "${NOTES_FILE}.backup"

# Prepare the new release notes entry
NEW_ENTRY="# version ${VERSION}

You can install Knuth node version v${VERSION} [using these instructions](https://kth.cash/#download).

${RELEASE_NOTES}

"

# Add the new entry at the top of the file (after any existing content)
{
    echo "$NEW_ENTRY"
    cat "$NOTES_FILE"
} > "${NOTES_FILE}.tmp" && mv "${NOTES_FILE}.tmp" "$NOTES_FILE"

echo -e "${GREEN}✅ Updated release notes file${NC}"

# Step 4: Commit the updated release notes
git add "$NOTES_FILE"
git commit -m "docs: update release notes for v${VERSION}"
git push origin master

# Step 5: Clean up temporary release and tag
echo -e "${GREEN}🧹 Cleaning up temporary release...${NC}"
gh release delete "${TEMP_TAG}" --yes
git tag -d "${TEMP_TAG}"
git push origin --delete "${TEMP_TAG}"

# Step 6: Create the real release with auto-generated notes
echo -e "${GREEN}🎉 Creating final release v${VERSION}...${NC}"
git tag -a "v${VERSION}" -m "Release version ${VERSION}"
git push origin "v${VERSION}"

# Create the final release with auto-generated notes (not copied text)
# This ensures GitHub generates the correct "Full Changelog" link
gh release create "v${VERSION}" \
    --title "v${VERSION}" \
    --generate-notes \
    --latest

echo -e "${GREEN}✅ Release v${VERSION} created successfully!${NC}"
echo -e "${GREEN}✅ Release notes have been updated in $NOTES_FILE${NC}"

# remove the release branch locally and remotely
echo -e "${GREEN}🧹 Cleaning up release branch...${NC}"
git push origin --delete "release/${VERSION}"
git branch -D "release/${VERSION}" 2>/dev/null || true

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}🎊 Release ${VERSION} completed successfully!${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Clean up ssh-agent if we started it
if [ "$SSH_AGENT_STARTED" = true ]; then
    echo -e "${GREEN}🔐 Stopping SSH agent...${NC}"
    ssh-agent -k
fi

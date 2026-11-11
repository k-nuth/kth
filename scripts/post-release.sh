#!/usr/bin/env bash
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
gh pr merge --squash "release/${VERSION}"

# switch to master and pull latest changes
echo -e "${GREEN}🔄 Switching to master and pulling latest changes...${NC}"
git checkout master
git pull origin master

# Publish the tested merge first. The tag stays on that source commit; the
# release-note update is a subsequent documentation commit on master.
echo -e "${GREEN}🎉 Creating final release v${VERSION}...${NC}"
git tag -a "v${VERSION}" -m "Release version ${VERSION}"
git push origin "v${VERSION}"
gh release create "v${VERSION}" \
    --title "v${VERSION}" \
    --generate-notes \
    --latest

# Read the actual final release, never the execution date or a temporary release.
echo -e "${GREEN}📤 Extracting published release metadata...${NC}"
RELEASE_NOTES=$(gh release view "v${VERSION}" --json body -q '.body')
RELEASE_DATE=$(gh release view "v${VERSION}" --json publishedAt --jq '.publishedAt[:10]')
if [ -z "$RELEASE_NOTES" ] || ! [[ "$RELEASE_DATE" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
    echo "❌ Failed to extract the final release body or publication date"
    exit 1
fi

# Step 3: Update local release notes file
echo -e "${GREEN}📄 Updating local release notes file...${NC}"
NOTES_FILE="docs/release-notes/release-notes.md"

# Create a backup
cp "$NOTES_FILE" "${NOTES_FILE}.backup"

# Prepare the new release notes entry
NEW_ENTRY="# version ${VERSION}

**Published:** [${RELEASE_DATE}](https://github.com/k-nuth/kth/releases/tag/v${VERSION})

${RELEASE_NOTES}

"

# Add the new entry at the top of the file (after any existing content)
{
    echo "$NEW_ENTRY"
    cat "$NOTES_FILE"
} > "${NOTES_FILE}.tmp" && mv "${NOTES_FILE}.tmp" "$NOTES_FILE"

# Refresh navigation after prepending the new release.
python3 scripts/update_release_notes_index.py "$NOTES_FILE"

echo -e "${GREEN}✅ Updated release notes file${NC}"

# Step 4: Commit the updated release notes
git add "$NOTES_FILE"
git commit -m "docs: update release notes for v${VERSION}"
git push origin master

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

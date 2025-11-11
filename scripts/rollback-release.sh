#!/bin/bash
set -e

# Colors for output
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
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
    echo "Usage: $0 <version> [--force]"
    echo "Example: $0 0.71.0"
    exit 1
fi

VERSION="$1"
FORCE=false

if [ "$2" == "--force" ]; then
    FORCE=true
fi

# Validate semver format
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo -e "${RED}❌ Version must be in semver format (e.g., 0.71.0)${NC}"
    exit 1
fi

echo -e "${YELLOW}🧹 Rollback release ${VERSION}...${NC}"

# Check if tag exists and how old it is
if git tag --list "v${VERSION}" | grep -q "v${VERSION}"; then
    TAG_DATE=$(git log -1 --format=%ai "v${VERSION}" 2>/dev/null || echo "")
    if [ -n "$TAG_DATE" ]; then
        TAG_TIMESTAMP=$(date -j -f "%Y-%m-%d %H:%M:%S %z" "$TAG_DATE" "+%s" 2>/dev/null || date -d "$TAG_DATE" "+%s" 2>/dev/null)
        CURRENT_TIMESTAMP=$(date "+%s")
        AGE_HOURS=$(( (CURRENT_TIMESTAMP - TAG_TIMESTAMP) / 3600 ))

        echo -e "${YELLOW}⚠️  Tag v${VERSION} exists and is ${AGE_HOURS} hours old${NC}"

        if [ $AGE_HOURS -gt 24 ] && [ "$FORCE" != true ]; then
            echo -e "${RED}❌ Tag is older than 24 hours. This might be an established release.${NC}"
            echo -e "${YELLOW}Use --force to proceed anyway${NC}"
            exit 1
        fi
    fi
fi

# Ask for confirmation unless --force is used
if [ "$FORCE" != true ]; then
    echo -e "${YELLOW}This will delete:${NC}"
    echo "  - Local branch: release/${VERSION}"
    echo "  - Remote branch: release/${VERSION}"
    echo "  - Local tag: v${VERSION}"
    echo "  - Remote tag: v${VERSION}"
    echo "  - GitHub release: v${VERSION}"
    echo "  - Temporary tag: temp-v${VERSION}"
    echo "  - Pull request for: release/${VERSION}"
    echo ""
    read -p "Are you sure you want to continue? (yes/no): " -r
    if [ "$REPLY" != "yes" ]; then
        echo "Aborted."
        exit 0
    fi
fi

# Switch to master first
echo -e "${GREEN}🔄 Switching to master branch...${NC}"
git checkout master 2>/dev/null || true

# 1. Cancel running workflows for the release branch
echo -e "${GREEN}🛑 Cancelling running workflows for release/${VERSION}...${NC}"
RUNNING_WORKFLOWS=$(gh run list --branch "release/${VERSION}" --status in_progress --json databaseId -q '.[].databaseId' 2>/dev/null || echo "")
if [ -n "$RUNNING_WORKFLOWS" ]; then
    for run_id in $RUNNING_WORKFLOWS; do
        echo "  Cancelling workflow run #${run_id}"
        gh run cancel "$run_id" 2>/dev/null || true
    done
else
    echo "  No running workflows found"
fi

# 2. Close and delete PR if it exists
echo -e "${GREEN}🗑️  Closing PR for release/${VERSION}...${NC}"
PR_NUMBER=$(gh pr list --head "release/${VERSION}" --json number -q '.[0].number' 2>/dev/null || echo "")
if [ -n "$PR_NUMBER" ]; then
    echo "  Closing PR #${PR_NUMBER}"
    gh pr close "$PR_NUMBER" --delete-branch 2>/dev/null || true
else
    echo "  No PR found"
fi

# 3. Delete GitHub release if it exists
if gh release view "v${VERSION}" >/dev/null 2>&1; then
    echo -e "${GREEN}🗑️  Deleting GitHub release v${VERSION}${NC}"
    gh release delete "v${VERSION}" --yes 2>/dev/null || true
else
    echo "  No GitHub release found for v${VERSION}"
fi

# 4. Delete temporary GitHub release if it exists
if gh release view "temp-v${VERSION}" >/dev/null 2>&1; then
    echo -e "${GREEN}🗑️  Deleting temporary GitHub release temp-v${VERSION}${NC}"
    gh release delete "temp-v${VERSION}" --yes 2>/dev/null || true
else
    echo "  No temporary GitHub release found"
fi

# 5. Delete remote tag if it exists
if git ls-remote --tags origin "v${VERSION}" | grep -q "v${VERSION}"; then
    echo -e "${GREEN}🗑️  Deleting remote tag v${VERSION}${NC}"
    git push origin --delete "v${VERSION}" 2>/dev/null || true
else
    echo "  No remote tag found for v${VERSION}"
fi

# 6. Delete remote temporary tag if it exists
if git ls-remote --tags origin "temp-v${VERSION}" | grep -q "temp-v${VERSION}"; then
    echo -e "${GREEN}🗑️  Deleting remote temporary tag temp-v${VERSION}${NC}"
    git push origin --delete "temp-v${VERSION}" 2>/dev/null || true
else
    echo "  No remote temporary tag found"
fi

# 7. Delete local tag if it exists
if git tag --list "v${VERSION}" | grep -q "v${VERSION}"; then
    echo -e "${GREEN}🗑️  Deleting local tag v${VERSION}${NC}"
    git tag -d "v${VERSION}" 2>/dev/null || true
else
    echo "  No local tag found for v${VERSION}"
fi

# 8. Delete local temporary tag if it exists
if git tag --list "temp-v${VERSION}" | grep -q "temp-v${VERSION}"; then
    echo -e "${GREEN}🗑️  Deleting local temporary tag temp-v${VERSION}${NC}"
    git tag -d "temp-v${VERSION}" 2>/dev/null || true
else
    echo "  No local temporary tag found"
fi

# 9. Delete remote release branch if it exists
if git ls-remote --heads origin "release/${VERSION}" | grep -q "release/${VERSION}"; then
    echo -e "${GREEN}🗑️  Deleting remote branch release/${VERSION}${NC}"
    git push origin --delete "release/${VERSION}" 2>/dev/null || true
else
    echo "  No remote branch found for release/${VERSION}"
fi

# 10. Delete local release branch if it exists
if git branch --list "release/${VERSION}" | grep -q "release/${VERSION}"; then
    echo -e "${GREEN}🗑️  Deleting local branch release/${VERSION}${NC}"
    git branch -D "release/${VERSION}" 2>/dev/null || true
else
    echo "  No local branch found for release/${VERSION}"
fi

# 11. Check for release notes commit and offer to revert it
echo -e "${GREEN}🔍 Checking for release notes commit...${NC}"
RELEASE_NOTES_COMMIT=$(git log --all --grep="docs: update release notes for v${VERSION}" --format="%H" -n 1 2>/dev/null || echo "")
if [ -n "$RELEASE_NOTES_COMMIT" ]; then
    echo -e "${YELLOW}⚠️  Found release notes commit: ${RELEASE_NOTES_COMMIT}${NC}"
    if [ "$FORCE" == true ]; then
        REVERT_ANSWER="yes"
    else
        read -p "Do you want to revert this commit? (yes/no): " -r REVERT_ANSWER
    fi

    if [ "$REVERT_ANSWER" == "yes" ]; then
        echo "  Reverting commit ${RELEASE_NOTES_COMMIT}"
        git revert --no-edit "$RELEASE_NOTES_COMMIT" 2>/dev/null || echo "  Could not revert automatically"
    fi
else
    echo "  No release notes commit found"
fi

echo -e "${GREEN}✅ Rollback completed for version ${VERSION}${NC}"
echo ""
echo -e "${YELLOW}Summary:${NC}"
echo "  - Cancelled running workflows"
echo "  - Closed and deleted PR"
echo "  - Deleted GitHub releases (main and temp)"
echo "  - Deleted tags (local and remote)"
echo "  - Deleted branches (local and remote)"
echo ""
echo -e "${GREEN}You can now fix the issues and run the release script again.${NC}"

# Clean up ssh-agent if we started it
if [ "$SSH_AGENT_STARTED" = true ]; then
    echo -e "${GREEN}🔐 Stopping SSH agent...${NC}"
    ssh-agent -k
fi

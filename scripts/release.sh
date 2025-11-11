#!/bin/bash
set -x

# Error handling - rollback on failure
trap 'echo "❌ Script failed. Consider running cleanup manually."; exit 1' ERR

# Start ssh-agent if not already running to avoid multiple passphrase prompts
SSH_AGENT_STARTED=false
if [ -z "$SSH_AUTH_SOCK" ]; then
    echo "🔐 Starting SSH agent..."
    eval $(ssh-agent -s)
    SSH_AGENT_STARTED=true
fi

# Add SSH key (will ask for passphrase once)
echo "🔑 Adding SSH key..."
ssh-add ~/.ssh/id_rsa 2>/dev/null || ssh-add ~/.ssh/id_ed25519 2>/dev/null || ssh-add 2>/dev/null || true

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi
VERSION="$1"

# Validate semver format
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "❌ Version must be in semver format (e.g., 0.71.0)"
    exit 1
fi

echo "Building version: ${VERSION}"

# Verify GitHub CLI authentication
echo "🔐 Verifying GitHub CLI authentication..."
if ! gh auth status >/dev/null 2>&1; then
    echo "❌ Not authenticated with GitHub CLI. Run: gh auth login"
    exit 1
fi

# Cleanup previous release attempts (if any)
echo "🧹 Cleaning up previous release artifacts..."

# First, ensure we're on master branch
echo "🔄 Ensuring we're on master branch..."
git checkout master 2>/dev/null || true

# Delete local release branch if it exists
if git branch --list "release/${VERSION}" | grep -q "release/${VERSION}"; then
    echo "🗑️  Deleting local branch release/${VERSION}"
    git branch -D "release/${VERSION}" 2>/dev/null || true
fi

# Delete remote release branch if it exists
if git ls-remote --heads origin "release/${VERSION}" | grep -q "release/${VERSION}"; then
    echo "🗑️  Deleting remote branch release/${VERSION}"
    git push origin --delete "release/${VERSION}" 2>/dev/null || true
fi

# Delete local tag if it exists
if git tag --list "v${VERSION}" | grep -q "v${VERSION}"; then
    echo "🗑️  Deleting local tag v${VERSION}"
    git tag -d "v${VERSION}" 2>/dev/null || true
fi

# Delete remote tag if it exists  
if git ls-remote --tags origin "v${VERSION}" | grep -q "v${VERSION}"; then
    echo "🗑️  Deleting remote tag v${VERSION}"
    git push origin --delete "v${VERSION}" 2>/dev/null || true
fi

# Delete GitHub release if it exists
if gh release view "v${VERSION}" >/dev/null 2>&1; then
    echo "🗑️  Deleting GitHub release v${VERSION}"
    gh release delete "v${VERSION}" --yes 2>/dev/null || true
fi

echo "✅ Cleanup completed"

# Check for staged changes before proceeding
if git status --porcelain | grep -q "^[MADRCU]"; then
    echo "⚠️ You have staged changes. Please commit or unstash them first."
    exit 1
fi

# Smart stash handling - only stash if there are changes, and track if we did
STASH_CREATED=false
if ! git diff-index --quiet HEAD --; then
    echo "📦 Stashing local changes..."
    git stash push -m "release script temporary stash for ${VERSION}"
    STASH_CREATED=true
else
    echo "📦 No local changes to stash"
fi

git pull origin master

# Only pop if we actually created a stash
if [ "$STASH_CREATED" = true ]; then
    echo "📦 Restoring stashed changes..."
    git stash pop
fi

# Verify the branch doesn't exist before creating
if git show-ref --verify --quiet "refs/heads/release/${VERSION}"; then
    echo "❌ Branch release/${VERSION} already exists locally"
    exit 1
fi

git checkout -b "release/${VERSION}"

# Update README versions before creating the release
echo "Updating README files to version: ${VERSION}"
./scripts/update_readme_versions.sh "${VERSION}"

# Track if we made any changes
CHANGES_MADE=false

# Check for README changes
echo "Checking for README changes..."
if git status --porcelain | grep -q "^ M\|^M\|^ A\|^A"; then
    echo "📝 Changes detected in working directory"
    git status --short
    CHANGES_MADE=true
else
    echo "📝 No changes detected - README files likely already have version ${VERSION}"
fi

rm -rf build
rm -rf conan.lock
rm -rf conan-wasm.lock

echo "🔒 Creating conan-wasm.lock for version ${VERSION}..."
conan lock create conanfile.py --version="${VERSION}" --update -pr ems2
mv conan.lock conan-wasm.lock

echo "🔒 Creating conan.lock for version ${VERSION}..."
conan lock create conanfile.py --version="${VERSION}" --update

# Check if lockfiles were created/changed
echo "🔒 Checking lockfile changes..."
if git status --porcelain | grep -q "conan.lock\|conan-wasm.lock"; then
    echo "📝 Lockfiles were updated"
    CHANGES_MADE=true
else
    echo "📝 No lockfile changes detected"
fi

# Commit everything in a single commit
if [ "$CHANGES_MADE" = true ]; then
    echo "📝 Creating single commit with all changes..."
    git add .
    git commit -m "release: prepare version ${VERSION}

- Update README versions to ${VERSION}
- Generate conan.lock and conan-wasm.lock for ${VERSION}"
else
    echo "📝 No changes to commit"
fi
git push origin "release/${VERSION}"

# Only create PR if we made changes, otherwise just use the branch for CI
if [ "$CHANGES_MADE" = true ]; then
    echo "📋 Changes were made, creating PR for release/${VERSION}..."
    if ! gh pr create --title "release: ${VERSION}" --body "release: ${VERSION}" --base master --head "release/${VERSION}" 2>/dev/null; then
        echo "PR creation failed. Checking if PR already exists..."
        
        # Check for existing PR with more robust approach
        existing_prs=$(gh pr list --head "release/${VERSION}" --base master --json number,title,url 2>/dev/null || echo "[]")
        
        if [ "$existing_prs" != "[]" ] && [ -n "$existing_prs" ]; then
            pr_number=$(echo "$existing_prs" | jq -r '.[0].number // empty')
            pr_title=$(echo "$existing_prs" | jq -r '.[0].title // empty')
            pr_url=$(echo "$existing_prs" | jq -r '.[0].url // empty')
            
            if [ -n "$pr_number" ]; then
                echo "✅ Found existing PR #${pr_number}: ${pr_title}"
                echo "URL: ${pr_url}"
            else
                echo "❌ Failed to create PR and no existing PR found"
                echo "Debug info - existing_prs: $existing_prs"
                exit 1
            fi
        else
            echo "❌ Failed to create PR and no existing PR found"
            echo "Debug info - existing_prs: $existing_prs"
            exit 1
        fi
    else
        echo "✅ PR created successfully for release/${VERSION}"
    fi
else
    echo "📋 No changes made - skipping PR creation"
    echo "🚀 Branch release/${VERSION} pushed for CI to build and upload Conan packages"
    
    # Check if there's already a PR from a previous run
    existing_prs=$(gh pr list --head "release/${VERSION}" --base master --json number,title,url 2>/dev/null || echo "[]")
    if [ "$existing_prs" != "[]" ] && [ -n "$existing_prs" ]; then
        pr_number=$(echo "$existing_prs" | jq -r '.[0].number // empty')
        pr_title=$(echo "$existing_prs" | jq -r '.[0].title // empty')
        pr_url=$(echo "$existing_prs" | jq -r '.[0].url // empty')
        
        if [ -n "$pr_number" ]; then
            echo "📋 Found existing PR #${pr_number}: ${pr_title}"
            echo "URL: ${pr_url}"
        fi
    fi
fi

echo "Waiting for the build to finish for branch: release/${VERSION}"
echo "⏰ Recording current time to filter only new workflow runs..."
# Subtract 10 seconds to account for the time it takes to push and create the workflow
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS date command
    RELEASE_START_TIME=$(date -u -v-10S +"%Y-%m-%dT%H:%M:%SZ")
else
    # Linux date command
    RELEASE_START_TIME=$(date -u -d '10 seconds ago' +"%Y-%m-%dT%H:%M:%SZ")
fi
echo "Filtering workflow runs created after: ${RELEASE_START_TIME}"
sleep 5  # Small delay to ensure GitHub has registered the push

MAX_WAIT_TIME=7200  # 2 hours
ELAPSED=0
while [ $ELAPSED -lt $MAX_WAIT_TIME ]; do
    # Get workflow runs for the current branch, including event type
    run_info=$(gh run list --branch "release/${VERSION}" --workflow "Build and Test" --limit 10 --json status,conclusion,url,number,event,createdAt)
    
    if [ -z "$run_info" ] || [ "$run_info" == "[]" ]; then
        echo "No workflow runs found for branch release/${VERSION}. Waiting..."
        sleep 30
        ELAPSED=$((ELAPSED + 30))
        continue
    fi
    
    # Find the most recent run that was triggered by 'push' (not 'pull_request')
    # Only consider runs created AFTER we pushed the release branch
    # Sort by creation time and filter out cancelled/failed runs from previous attempts
    push_run=$(echo "$run_info" | jq -r --arg start_time "$RELEASE_START_TIME" '
        .[] |
        select(.event == "push") |
        select(.createdAt >= $start_time) |
        select(.status == "in_progress" or .status == "queued" or (.status == "completed" and .conclusion == "success")) |
        . | @base64
    ' | head -1)
    
    # If no active/successful push run found, look for the most recent push run regardless of status
    if [ -z "$push_run" ]; then
        echo "No active push-triggered workflow runs found. Checking most recent push run..."
        push_run=$(echo "$run_info" | jq -r --arg start_time "$RELEASE_START_TIME" '
            .[] |
            select(.event == "push") |
            select(.createdAt >= $start_time) |
            . | @base64
        ' | head -1)
    fi
    
    if [ -z "$push_run" ]; then
        echo "No push-triggered workflow runs found for branch release/${VERSION}. Waiting..."
        sleep 30
        ELAPSED=$((ELAPSED + 30))
        continue
    fi
    
    # Decode the base64 encoded JSON
    push_run_decoded=$(echo "$push_run" | base64 --decode)
    
    status=$(echo "$push_run_decoded" | jq -r '.status')
    conclusion=$(echo "$push_run_decoded" | jq -r '.conclusion')
    url=$(echo "$push_run_decoded" | jq -r '.url')
    run_number=$(echo "$push_run_decoded" | jq -r '.number')
    event=$(echo "$push_run_decoded" | jq -r '.event')
    created_at=$(echo "$push_run_decoded" | jq -r '.createdAt')
    
    echo "Workflow run #${run_number} (${event}): status=${status}, conclusion=${conclusion}"
    echo "Created: ${created_at}"
    echo "URL: ${url}"
    
    if [ "$status" == "completed" ]; then
        if [ "$conclusion" == "success" ]; then
            echo "✅ Build completed successfully!"
            break
        elif [ "$conclusion" == "cancelled" ]; then
            echo "⚠️ Most recent workflow was cancelled. This might be from a previous release attempt."
            echo "Waiting for a new workflow to start or checking if there's a more recent one..."
            sleep 30
            ELAPSED=$((ELAPSED + 30))
            continue
        else
            echo "❌ Build completed but failed with conclusion: ${conclusion}"
            echo "Please check the workflow at: ${url}"

            # Ask user if they want to continue waiting for a new run or exit
            echo "This might be from a previous release attempt. Continue waiting? (y/n)"
            read -r response
            if [ "$response" != "y" ] && [ "$response" != "Y" ]; then
                exit 1
            fi
            sleep 30
            ELAPSED=$((ELAPSED + 30))
            continue
        fi
    else
        echo "🔄 Build is still in progress (${status}). Waiting..."
        sleep 30
        ELAPSED=$((ELAPSED + 30))
    fi
done

# If we exit the loop due to timeout
if [ $ELAPSED -ge $MAX_WAIT_TIME ]; then
    echo "❌ Timeout waiting for CI after $MAX_WAIT_TIME seconds ($(($MAX_WAIT_TIME / 60)) minutes)"
    exit 1
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ CI build completed successfully!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "📦 Pre-release steps completed:"
echo "  ✓ Release branch created: release/${VERSION}"
echo "  ✓ PR created and ready for merge"
echo "  ✓ CI builds passed"
echo ""
echo "🎯 Next step: Run the post-release script to complete the release:"
echo "   ./scripts/post-release.sh ${VERSION}"
echo ""
echo "The post-release script will:"
echo "  - Merge the release PR"
echo "  - Generate and commit release notes"
echo "  - Create the release tag and GitHub release"
echo "  - Clean up the release branch"
echo ""

# Clean up ssh-agent if we started it
if [ "$SSH_AGENT_STARTED" = true ]; then
    echo "🔐 Stopping SSH agent..."
    ssh-agent -k
fi

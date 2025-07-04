#!/bin/bash
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi
VERSION="$1"

echo "Building version: ${VERSION}"

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

git checkout -b "release/${VERSION}"

# Update README versions before creating the release
echo "Updating README files to version: ${VERSION}"
./scripts/update_readme_versions.sh "${VERSION}"

# Track if we made any changes
CHANGES_MADE=false

# Commit README updates
echo "Checking for README changes..."
if git status --porcelain | grep -q "^ M\|^M\|^ A\|^A"; then
    echo "📝 Changes detected in working directory:"
    git status --short
    echo "Committing README version updates..."
    git add .
    git commit -m "docs: update README versions to ${VERSION}"
    CHANGES_MADE=true
else
    echo "📝 No changes detected - README files likely already have version ${VERSION}"
fi

rm -rf build
rm -rf conan.lock

echo "🔒 Creating conan.lock for version ${VERSION}..."
conan lock create conanfile.py --version="${VERSION}" --update

echo "🔒 Checking conan.lock changes..."
if git status --porcelain | grep -q "conan.lock"; then
    echo "📝 conan.lock was updated, committing..."
    git add conan.lock
    git commit -m "release: update conan.lock for version ${VERSION}"
    CHANGES_MADE=true
else
    echo "📝 No conan.lock changes detected"
    # Add anyway in case it's a new file
    git add conan.lock 2>/dev/null || true
    if git diff --cached --quiet; then
        echo "📝 No changes to commit for conan.lock"
    else
        echo "📝 Committing new conan.lock..."
        git commit -m "release: update conan.lock for version ${VERSION}"
        CHANGES_MADE=true
    fi
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
while true; do
    # Get workflow runs for the current branch, including event type
    run_info=$(gh run list --branch "release/${VERSION}" --workflow "Build and Test" --limit 10 --json status,conclusion,url,number,event,createdAt)
    
    if [ -z "$run_info" ] || [ "$run_info" == "[]" ]; then
        echo "No workflow runs found for branch release/${VERSION}. Waiting..."
        sleep 30
        continue
    fi
    
    # Find the most recent run that was triggered by 'push' (not 'pull_request')
    # Sort by creation time and filter out cancelled/failed runs from previous attempts
    push_run=$(echo "$run_info" | jq -r '
        .[] | 
        select(.event == "push") | 
        select(.status == "in_progress" or .status == "queued" or (.status == "completed" and .conclusion == "success")) |
        . | @base64
    ' | head -1)
    
    # If no active/successful push run found, look for the most recent push run regardless of status
    if [ -z "$push_run" ]; then
        echo "No active push-triggered workflow runs found. Checking most recent push run..."
        push_run=$(echo "$run_info" | jq -r '
            .[] | 
            select(.event == "push") | 
            . | @base64
        ' | head -1)
    fi
    
    if [ -z "$push_run" ]; then
        echo "No push-triggered workflow runs found for branch release/${VERSION}. Waiting..."
        sleep 30
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
            continue
        fi
    else
        echo "🔄 Build is still in progress (${status}). Waiting..."
        sleep 30
    fi
done

# squash merge the PR, do not delete the branch
gh pr merge --squash --auto "release/${VERSION}"

# switch to master and pull latest changes
git checkout master
git pull origin master

# Step 1: Create temporary release to generate notes
echo "Creating temporary release to generate notes..."
TEMP_TAG="temp-v${VERSION}"
git tag -a "${TEMP_TAG}" -m "Temporary tag for release notes generation"
git push origin "${TEMP_TAG}"

gh release create "${TEMP_TAG}" \
    --title "temp-v${VERSION}" \
    --generate-notes \
    --prerelease

# Step 2: Extract the generated notes
echo "Extracting generated release notes..."
RELEASE_NOTES=$(gh release view "${TEMP_TAG}" --json body -q '.body')

if [ -z "$RELEASE_NOTES" ]; then
    echo "❌ Failed to extract release notes"
    exit 1
fi

# Replace temporary tag references with final tag in the release notes
echo "Fixing references in release notes..."
RELEASE_NOTES=$(echo "$RELEASE_NOTES" | sed "s/temp-v${VERSION}/v${VERSION}/g")

# Step 3: Update local release notes file
echo "Updating local release notes file..."
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

echo "✅ Updated release notes file"

# Step 4: Commit the updated release notes
git add "$NOTES_FILE"
git commit -m "docs: update release notes for v${VERSION}"
git push origin master

# Step 5: Clean up temporary release and tag
echo "Cleaning up temporary release..."
gh release delete "${TEMP_TAG}" --yes
git tag -d "${TEMP_TAG}"
git push origin --delete "${TEMP_TAG}"

# Step 6: Create the real release with auto-generated notes
echo "Creating final release v${VERSION}..."
git tag -a "v${VERSION}" -m "Release version ${VERSION}"
git push origin "v${VERSION}"

# Create the final release with auto-generated notes (not copied text)
# This ensures GitHub generates the correct "Full Changelog" link
gh release create "v${VERSION}" \
    --title "v${VERSION}" \
    --generate-notes \
    --latest

echo "✅ Release v${VERSION} created successfully!"
echo "✅ Release notes have been updated in $NOTES_FILE"

# remove the release branch locally and remotely
git push origin --delete "release/${VERSION}"
git branch -D "release/${VERSION}"

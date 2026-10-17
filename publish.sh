#!/usr/bin/env bash
set -e

# 1. Check for required commit message
if [ -z "$1" ]; then
    echo "❌ Error: Please provide a commit message."
    echo "Usage: ./publish.sh \"your commit message\" [optional_version_tag]"
    exit 1
fi

COMMIT_MSG="$1"
TAG="$2"
CURRENT_BRANCH=$(git branch --show-current)

echo "📦 Staging project source files and scripts..."
git add CMakeLists.txt README.md core/ .gitignore publish.sh

# Check if there are actually changes staged to prevent commit errors
if git diff --cached --quiet; then
    echo "ℹ️ No new changes detected. Skipping commit step."
else
    echo "✍️ Committing changes with GPG Signing onto [$CURRENT_BRANCH]..."
    git commit -S -m "$COMMIT_MSG"
fi

echo "🚀 Pushing code to origin/$CURRENT_BRANCH..."
git push origin "$CURRENT_BRANCH"

# 2. Check and handle version tagging if provided
if [ ! -z "$TAG" ]; then
    echo "🏷️ Creating signed release tag: $TAG..."
    # Force tag update if it already failed halfway last time
    git tag -f -s "$TAG" -m "Release $TAG"
    git push -f origin "$TAG"
fi

echo "✅ Automation run finished successfully!"


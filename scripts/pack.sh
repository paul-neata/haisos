#!/bin/bash
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cd "$REPO_ROOT"

# Read version (strip quotes)
VERSION=$(tr -d '"' < "$REPO_ROOT/VERSION")

# Get branch name
if [ -n "$GITHUB_REF_NAME" ]; then
    BRANCH="$GITHUB_REF_NAME"
else
    BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

# Extract branch name: strip before first slash, keep before second slash
BRANCH_NAME="${BRANCH#*/}"
BRANCH_NAME="${BRANCH_NAME%%/*}"

# Get full git hash and first 15 chars
GIT_HASH=$(git rev-parse HEAD)
GIT_HASH_SHORT="${GIT_HASH:0:15}"

# Get base branch
if [ -n "$GITHUB_BASE_REF" ]; then
    BASE_BRANCH="$GITHUB_BASE_REF"
else
    DEFAULT_REF=$(git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null || true)
    if [ -n "$DEFAULT_REF" ]; then
        BASE_BRANCH="${DEFAULT_REF#refs/remotes/origin/}"
    else
        BASE_BRANCH="unknown"
    fi
fi

# Compute minute of year
day_of_year=$(date +%j)
hour=$(date +%H)
minute=$(date +%M)
min_of_year=$(( (10#$day_of_year - 1) * 1440 + 10#$hour * 60 + 10#$minute ))

# Build zip name
ZIP_NAME="haisos.${BRANCH_NAME}.${VERSION}.${min_of_year}.${GIT_HASH_SHORT}.zip"

# Create staging directory
STAGING=$(mktemp -d)
mkdir -p "$STAGING/bin/linux"
mkdir -p "$STAGING/bin/windows"
mkdir -p "$STAGING/meta"

# Copy binaries (ignore missing)
cp -r "$REPO_ROOT/output/linux/"* "$STAGING/bin/linux/" 2>/dev/null || true
cp -r "$REPO_ROOT/output/windows/"* "$STAGING/bin/windows/" 2>/dev/null || true

# Compute GitHub commit URL
if [ -n "$GITHUB_SERVER_URL" ] && [ -n "$GITHUB_REPOSITORY" ]; then
    GITHUB_COMMIT_URL="${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}/commit/${GIT_HASH}"
elif [ -n "$GITHUB_REPOSITORY" ]; then
    GITHUB_COMMIT_URL="https://github.com/${GITHUB_REPOSITORY}/commit/${GIT_HASH}"
else
    GITHUB_COMMIT_URL="https://github.com/paul-neata/haisos/commit/${GIT_HASH}"
fi

# Write meta files
echo "https://github.com/paul-neata/haisos" > "$STAGING/meta/repo"
echo "$BRANCH" > "$STAGING/meta/branch"
echo "$GIT_HASH" > "$STAGING/meta/git_hash"
echo "$VERSION" > "$STAGING/meta/version"
echo "$BASE_BRANCH" > "$STAGING/meta/base_branch"
echo "$GITHUB_COMMIT_URL" > "$STAGING/meta/github_commit_url"

# Copy LICENSE
cp "$REPO_ROOT/LICENSE" "$STAGING/LICENSE"

# Create zip
cd "$STAGING"
if command -v zip >/dev/null 2>&1; then
    zip -r "$REPO_ROOT/$ZIP_NAME" .
else
    python3 -c "
import zipfile
import os
import sys
repo_root = sys.argv[1]
zip_name = sys.argv[2]
with zipfile.ZipFile(os.path.join(repo_root, zip_name), 'w', zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk('.'):
        for file in files:
            filepath = os.path.join(root, file)
            zf.write(filepath, filepath)
" "$REPO_ROOT" "$ZIP_NAME"
fi

echo "Created: $ZIP_NAME"

# Cleanup
rm -rf "$STAGING"

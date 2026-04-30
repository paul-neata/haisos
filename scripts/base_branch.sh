#!/bin/bash
set -e

# Detect the base branch of the current HEAD.
# Prints the base branch name to stdout.
# Exits with code 1 if the base branch cannot be determined.

# 1. GitHub Actions PR context
if [ -n "$GITHUB_BASE_REF" ]; then
    echo "$GITHUB_BASE_REF"
    exit 0
fi

# 2. GitHub CLI: open PR for current branch
if command -v gh >/dev/null 2>&1; then
    BASE_BRANCH=$(gh pr view --json baseRefName -q .baseRefName 2>/dev/null || true)
    if [ -n "$BASE_BRANCH" ]; then
        echo "$BASE_BRANCH"
        exit 0
    fi
fi

# 3. Git upstream tracking
UPSTREAM=$(git rev-parse --abbrev-ref @{upstream} 2>/dev/null || true)
if [ -n "$UPSTREAM" ]; then
    echo "${UPSTREAM#origin/}"
    exit 0
fi

# 4. Git config merge ref for current branch
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)
if [ -n "$CURRENT_BRANCH" ]; then
    BASE_BRANCH=$(git config "branch.$CURRENT_BRANCH.merge" 2>/dev/null | sed 's|refs/heads/||' || true)
    if [ -n "$BASE_BRANCH" ]; then
        echo "$BASE_BRANCH"
        exit 0
    fi
fi

# 5. Default branch from origin/HEAD
DEFAULT_REF=$(git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null || true)
if [ -n "$DEFAULT_REF" ]; then
    echo "${DEFAULT_REF#refs/remotes/origin/}"
    exit 0
fi

# 6. Ancestry check against origin/master and origin/main
for CANDIDATE in master main; do
    if git merge-base --is-ancestor "origin/$CANDIDATE" HEAD 2>/dev/null || \
       [ -n "$(git log --oneline "origin/$CANDIDATE..HEAD" 2>/dev/null | head -1)" ]; then
        echo "$CANDIDATE"
        exit 0
    fi
done

# 7. Fallback to local refs
for CANDIDATE in main master; do
    if git show-ref --verify --quiet "refs/heads/$CANDIDATE" 2>/dev/null || \
       git show-ref --verify --quiet "refs/remotes/origin/$CANDIDATE" 2>/dev/null; then
        echo "$CANDIDATE"
        exit 0
    fi
done

echo "Error: could not detect base branch" >&2
exit 1

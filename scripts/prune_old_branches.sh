#!/bin/bash
set -e

# Delete local branches that are provably merged into the base branch as-is.
#
# A branch is deleted only when its tip commit -- and so every commit on it,
# by exact hash -- is already contained in origin/<base>. Branches merged by
# squash or rebase have different hashes on the base, so they are kept and
# reported, together with the command that deletes them by hand.
#
# Never touches the base branch, the current branch, a branch checked out in
# another worktree, or any remote branch.
#
# Usage: scripts/prune_old_branches.sh [base_branch]   (default: master)

BASE_BRANCH="${1:-master}"

git fetch --quiet origin "$BASE_BRANCH"
BASE_REF="origin/$BASE_BRANCH"
if ! git rev-parse --verify --quiet "$BASE_REF" >/dev/null; then
    echo "Error: $BASE_REF does not exist" >&2
    exit 1
fi

CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)

# Branches checked out in any worktree (including this one) cannot be deleted.
CHECKED_OUT=$(git worktree list --porcelain | sed -n 's|^branch refs/heads/||p')

PRUNED=()
KEPT=()
SKIPPED=()

while IFS= read -r BRANCH; do
    if [ "$BRANCH" = "$BASE_BRANCH" ] || [ "$BRANCH" = "master" ] || [ "$BRANCH" = "main" ]; then
        continue
    fi
    if [ "$BRANCH" = "$CURRENT_BRANCH" ]; then
        SKIPPED+=("$BRANCH (current branch)")
        continue
    fi
    if printf '%s\n' "$CHECKED_OUT" | grep -qxF "$BRANCH"; then
        SKIPPED+=("$BRANCH (checked out in another worktree)")
        continue
    fi

    if git merge-base --is-ancestor "refs/heads/$BRANCH" "$BASE_REF"; then
        TIP=$(git rev-parse --short "refs/heads/$BRANCH")
        git branch -D --quiet "$BRANCH"
        PRUNED+=("$BRANCH (was $TIP)")
    else
        UNMERGED=$(git rev-list --count "$BASE_REF..refs/heads/$BRANCH")
        KEPT+=("$BRANCH|$UNMERGED")
    fi
done < <(git for-each-ref --format='%(refname:short)' refs/heads/)

echo "Base: $BASE_REF"
echo

echo "Pruned (every commit already on $BASE_REF by exact hash):"
if [ ${#PRUNED[@]} -eq 0 ]; then
    echo "  (none)"
else
    for ENTRY in "${PRUNED[@]}"; do echo "  $ENTRY"; done
fi
echo

echo "Kept (has commits whose hashes are not on $BASE_REF -- e.g. squash-merged, or unfinished):"
if [ ${#KEPT[@]} -eq 0 ]; then
    echo "  (none)"
else
    for ENTRY in "${KEPT[@]}"; do
        BRANCH="${ENTRY%|*}"
        COUNT="${ENTRY##*|}"
        echo "  $BRANCH ($COUNT commit(s) not on $BASE_REF)"
        echo "    to delete it: git branch -D $BRANCH"
    done
fi

if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo
    echo "Skipped:"
    for ENTRY in "${SKIPPED[@]}"; do echo "  $ENTRY"; done
fi

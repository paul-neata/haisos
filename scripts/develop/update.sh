#!/bin/bash
# The engine of /develop-update: brings this clone's develop up to date with
# origin/develop and publishes the local plan changes -- how the plan and the
# implement sessions talk (see .claude/develop/WORKFLOW.md, "Two sessions, one
# develop").
#
# Usage: scripts/develop/update.sh [-m "<message>"] [--log "<entry>"] [--no-pr]
#   -m "<message>"   commit the changes in develop-plan/, notes/ and HAISOS_VERSION
#   --log "<entry>"  first append "- <UTC time> -- <entry>" to develop-plan/log.md
#                    (the implement session's log, shown on the develop PR); the
#                    commit message defaults to "Log: <entry>"
#   --no-pr          do not refresh the develop PR afterwards
# Steps: fetch; commit; rebase onto origin/develop (other local changes are
# stashed and restored); report what came in; push; refresh the develop PR.
# Exit status: 0 done (pushed, or nothing to push); 3 a rebase conflict -- the
# rebase is left in progress: resolve the files, `git add` them,
# `GIT_EDITOR=true git rebase --continue`, and run this again without -m;
# 4 plan changes to commit but no -m; 1 other errors.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

MSG="" LOG_ENTRY="" REFRESH_PR=1
while [ $# -gt 0 ]; do
    case "$1" in
        -m) MSG="${2:-}"; shift 2;;
        --log) LOG_ENTRY="${2:-}"; shift 2;;
        --no-pr) REFRESH_PR=0; shift;;
        *) die "usage: update.sh [-m \"<message>\"] [--log \"<entry>\"] [--no-pr]";;
    esac
done

cd "$DEV_REPO" || exit 1
if [ -d "$(git rev-parse --git-path rebase-merge)" ] || [ -d "$(git rev-parse --git-path rebase-apply)" ]; then
    echo "A rebase is in progress: resolve it (git add, GIT_EDITOR=true git rebase --continue), then run this again."
    git status --short | grep -E '^(UU|AA|DU|UD|AU|UA|DD) ' || true
    exit 3
fi
[ "$(git rev-parse --abbrev-ref HEAD)" = develop ] || die "not on develop (git switch develop)"

BEFORE=$(git rev-parse -q --verify origin/develop || true)
git fetch -q origin --prune || die "git fetch failed"

if [ -n "$LOG_ENTRY" ]; then
    mkdir -p develop-plan
    [ -f develop-plan/log.md ] || printf '# Log\n\n' > develop-plan/log.md
    printf -- '- %s -- %s\n' "$(date -u '+%Y-%m-%d %H:%M UTC')" "$LOG_ENTRY" >> develop-plan/log.md
    [ -n "$MSG" ] || MSG="Log: ${LOG_ENTRY:0:60}"
fi

PATHS=()
for p in develop-plan notes HAISOS_VERSION; do
    if [ -e "$p" ] || git ls-files --error-unmatch "$p" > /dev/null 2>&1; then PATHS+=("$p"); fi
done
CHANGED=""
[ ${#PATHS[@]} -eq 0 ] || CHANGED=$(git status --porcelain -- "${PATHS[@]}")
if [ -n "$CHANGED" ]; then
    if [ -z "$MSG" ]; then
        echo "UNCOMMITTED plan changes -- run again with -m \"<message>\":"
        echo "$CHANGED"
        exit 4
    fi
    git add -A -- "${PATHS[@]}"
    git commit -q -m "$MSG" -- "${PATHS[@]}"
    echo "COMMITTED: $(git log -1 --format='%h %s')"
fi
OTHER=$(git status --porcelain | grep -vE '^.. "?(develop-plan/|notes/|HAISOS_VERSION)' || true)
[ -z "$OTHER" ] || { echo "NOT COMMITTED (outside the plan, left as they are):"; echo "$OTHER" | take 10; }

for attempt in 1 2 3 4 5; do
    if ! git pull -q --rebase --autostash origin develop; then
        echo "CONFLICT while rebasing onto origin/develop:"
        git status --short | grep -E '^(UU|AA|DU|UD|AU|UA|DD) ' || git status --short
        exit 3
    fi
    if [ "$attempt" = 1 ]; then
        NOW=$(git rev-parse origin/develop)
        if [ -n "$BEFORE" ] && [ "$BEFORE" != "$NOW" ]; then
            N=$(git rev-list --count "$BEFORE..$NOW")
            echo "INCOMING: $N commit(s) on origin/develop since this clone's last sync:"
            git log --format='  %h %<(12,trunc)%an %s' "$BEFORE..$NOW" | take 15
            [ "$N" -le 15 ] || echo "  ..."
            git diff --stat=100,70 "$BEFORE" "$NOW" -- develop-plan notes HAISOS_VERSION | sed 's/^/  /' | tail -n 16
            ROWS=$(git diff -U0 "$BEFORE" "$NOW" -- develop-plan/playbook.md | grep -E '^\+(\||Pause:|Phase:|- )' | take 20 || true)
            [ -z "$ROWS" ] || { echo "  playbook now:"; echo "$ROWS" | sed 's/^+/    /'; }
        else
            echo "INCOMING: nothing new on origin/develop"
        fi
    fi
    if [ -z "$(git rev-list origin/develop..develop)" ]; then
        echo "PUSHED: nothing to push; develop = origin/develop at $(git rev-parse --short HEAD)"
        break
    fi
    if git push -q origin develop; then
        echo "PUSHED: $(git log -1 --format='%h %s' develop)"
        break
    fi
    log "push rejected (attempt $attempt), rebasing again"
    sleep $((attempt * 2))
    [ "$attempt" -lt 5 ] || die "could not push develop after 5 attempts"
done

if [ "$REFRESH_PR" = 1 ]; then
    bash "$DEV_SCRIPTS/develop_pr.sh" update 2>&1 | tail -n 1 || true
fi

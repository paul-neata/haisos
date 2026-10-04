#!/bin/bash
# Waits for a PR's checks on its current head commit, and on a failure saves
# the failed jobs' logs.
#
# Usage: scripts/develop/wait_ci.sh <pr-number> [--timeout-min <n>] [--log <file>]
#   --timeout-min  how long to wait in all (default 60)
#   --log          where to write the failed jobs' log tails (default: none)
# The checks are those of the push runs of the PR's head commit (CI runs on
# every push, see .github/workflows/ci.yml); right after a push they take a
# little while to appear.
# Prints one line per check, then "CI: PASS | FAIL | TIMEOUT".
# Exit status: 0 pass, 1 fail, 2 timeout, 3 error.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

PR="${1:-}"; [ -n "$PR" ] || { echo "usage: wait_ci.sh <pr-number> [--timeout-min n] [--log file]" >&2; exit 3; }
shift
TIMEOUT_MIN=60; LOG=""
while [ $# -gt 0 ]; do
    case "$1" in
        --timeout-min) TIMEOUT_MIN="$2"; shift 2;;
        --log) LOG="$2"; shift 2;;
        *) echo "unknown option '$1'" >&2; exit 3;;
    esac
done

DEADLINE=$(( $(date +%s) + TIMEOUT_MIN * 60 ))
HEAD_SHA=$(gh pr view "$PR" --json headRefOid -q .headRefOid) || { echo "CI: ERROR (no PR #$PR)"; exit 3; }

# 1. Wait for the checks of this head commit to exist.
while :; do
    N=$(gh pr checks "$PR" --json name -q 'length' 2>/dev/null || echo 0)
    [ "${N:-0}" -gt 0 ] && break
    [ "$(date +%s)" -lt "$DEADLINE" ] || { echo "CI: TIMEOUT (no checks appeared for ${HEAD_SHA:0:12})"; exit 2; }
    sleep 15
done

# 2. Wait for them to finish.
LEFT=$(( (DEADLINE - $(date +%s)) / 60 + 1 ))
rc=0
timeout "${LEFT}m" gh pr checks "$PR" --watch --fail-fast --interval 30 > /dev/null 2>&1 || rc=$?

gh pr checks "$PR" --json name,bucket,link -q '.[] | "\(.bucket)\t\(.name)\t\(.link)"' 2>/dev/null || true
if [ "$rc" = 124 ]; then echo "CI: TIMEOUT"; exit 2; fi
FAILED=$(gh pr checks "$PR" --json bucket -q '[.[] | select(.bucket == "fail" or .bucket == "cancel")] | length' 2>/dev/null || echo 1)
PENDING=$(gh pr checks "$PR" --json bucket -q '[.[] | select(.bucket == "pending")] | length' 2>/dev/null || echo 0)
if [ "$rc" = 0 ] && [ "${FAILED:-1}" = 0 ] && [ "${PENDING:-0}" = 0 ]; then
    echo "CI: PASS (${HEAD_SHA:0:12})"
    exit 0
fi

# 3. A failure: keep the failed jobs' logs.
if [ -n "$LOG" ]; then
    : > "$LOG"
    for RUN in $(gh run list --commit "$HEAD_SHA" --json databaseId,conclusion \
                   -q '.[] | select(.conclusion == "failure") | .databaseId' 2>/dev/null); do
        echo "=== failed run $RUN" >> "$LOG"
        gh run view "$RUN" --log-failed 2>/dev/null | tail -n 300 >> "$LOG" || true
    done
fi
echo "CI: FAIL (${HEAD_SHA:0:12})"
exit 1

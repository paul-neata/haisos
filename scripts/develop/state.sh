#!/bin/bash
# The develop's state in a few lines: the plan on develop, and what GitHub
# knows about its task branches and PRs (see .claude/develop/WORKFLOW.md).
#
# Usage: scripts/develop/state.sh [--pull]
#   --pull  also bring the local develop up to date (rebasing local plan commits)
#           when it is checked out; without it nothing local changes but the
#           remote-tracking refs.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

PULL=0
[ "${1:-}" = "--pull" ] && PULL=1
GIT=(git -C "$DEV_REPO")

"${GIT[@]}" fetch -q origin --prune
if ! "${GIT[@]}" rev-parse -q --verify origin/develop > /dev/null; then
    echo "DEVELOP: none (no origin/develop; start one with /develop-create)"
    exit 0
fi
CURRENT=$("${GIT[@]}" rev-parse --abbrev-ref HEAD)
if [ "$PULL" = 1 ] && [ "$CURRENT" = develop ]; then
    "${GIT[@]}" pull -q --rebase --autostash origin develop \
        || { echo "PULL: conflict -- resolve the rebase in progress, then run again"; exit 1; }
fi

GOAL=$("${GIT[@]}" show origin/develop:develop-plan/goal.md 2>/dev/null || true)
PLAYBOOK=$("${GIT[@]}" show origin/develop:develop-plan/playbook.md 2>/dev/null || true)
TITLE=$(sed -n 's/^# Develop: *//p' <<< "$GOAL" | take 1)
CREATED=$(sed -n 's/^- Created: *//p' <<< "$GOAL" | take 1)
HEAD_SHORT=$("${GIT[@]}" rev-parse --short origin/develop)
SYNC="not checked out here"
if [ "$CURRENT" = develop ]; then
    read -r AHEAD BEHIND < <("${GIT[@]}" rev-list --left-right --count develop...origin/develop)
    SYNC="local develop: ahead $AHEAD, behind $BEHIND"
fi
echo "DEVELOP: $(develop_version) \"${TITLE:-untitled}\", created ${CREATED:-?}, origin/develop at $HEAD_SHORT ($SYNC)"
echo "PHASE: $(sed -n 's/^Phase: *//p' <<< "$PLAYBOOK" | take 1)   PAUSE: $(sed -n 's/^Pause: *//p' <<< "$PLAYBOOK" | take 1)"

ROWS=$(grep -E '^\| *[0-9]+ *\|' <<< "$PLAYBOOK" || true)
if [ -n "$ROWS" ]; then
    COUNTS=$(awk -F'|' '{gsub(/ /, "", $4); n[$4]++} END {for (s in n) printf "%s %d, ", s, n[s]}' <<< "$ROWS")
    echo "PLAYBOOK: $(wc -l <<< "$ROWS") tasks: ${COUNTS%, }"
    awk -F'|' '{s = $4; gsub(/ /, "", s); if (s != "done" && s != "obsolete") print "  " $0}' <<< "$ROWS" | take 12
else
    echo "PLAYBOOK: no tasks yet"
fi
QUESTIONS=$(awk '/^## Questions/ {f = 1; next} /^## / {f = 0} f && /^- /' <<< "$PLAYBOOK" | grep -vc -- '-> answered' || true)
echo "QUESTIONS: ${QUESTIONS:-0} open"

echo "GITHUB: PRs into develop${CREATED:+ since $CREATED}:"
SEARCH=""
[ -z "$CREATED" ] || SEARCH="created:>=$CREATED"
gh pr list --base develop --state all --limit 200 ${SEARCH:+--search "$SEARCH"} \
    --json number,state,headRefName,title \
    -q 'sort_by(.number) | .[] | "  #\(.number) \(.state) \(.headRefName) -- \(.title)"' 2>/dev/null \
    || echo "  (gh failed)"
WITH_PR=$(gh pr list --base develop --state all --limit 200 --json headRefName -q '.[].headRefName' 2>/dev/null || true)
LONE=$("${GIT[@]}" for-each-ref --format='%(refname:lstrip=3)' 'refs/remotes/origin/task/' \
    | sed 's|^|task/|' | grep -vxF -f <(printf '%s\n' "$WITH_PR") || true)
echo "TASK BRANCHES WITHOUT A PR: ${LONE:-none}" | tr '\n' ' '; echo
echo "DEVELOP PR INTO MASTER: $(gh pr list --base master --head develop --state open --limit 1 \
    --json number,isDraft,title,url -q '.[0] // empty | "#\(.number) \"\(.title)\"\(if .isDraft then " (draft)" else "" end) \(.url)"' 2>/dev/null || true)"
RUNNING=$(docker ps --filter "name=$DEV_CONTAINER_PREFIX-" --format '{{.Names}} ({{.RunningFor}})' 2>/dev/null || true)
echo "CONTAINER: ${RUNNING:-none running}"

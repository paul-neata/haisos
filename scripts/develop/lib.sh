#!/bin/bash
# Shared by the host-side scripts in scripts/develop/ -- source it, do not run it.
# The workflow these scripts implement is described in .claude/develop/WORKFLOW.md.
#
# The repository is the one the caller stands in (git rev-parse --show-toplevel),
# the scripts are the ones next to this file. HAISOS_DEVELOP_HOME (default
# ~/.haisos-develop) holds everything outside the repository:
#   work/     the task container's workspace (a git repository only containers touch)
#   in/ out/  the current container run's input and output
#   runs/<develop-id>/<task-id>/<NN>-<mode>[-<model>]/   every run's files, kept
#   review/<task-id>/   the worktree /develop-code-review fixes a PR in
#   lock      held while a container runs

set -euo pipefail

DEV_SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEV_REPO="$(git rev-parse --show-toplevel 2>/dev/null)" || { echo "ERROR: not inside a git repository" >&2; exit 1; }
DEV_HOME="${HAISOS_DEVELOP_HOME:-$HOME/.haisos-develop}"
DEV_PLAN="$DEV_REPO/develop-plan"
DEV_IMAGE_REPO="haisos-develop"
DEV_CONTAINER_PREFIX="haisos-develop"
DEV_DEFAULT_MODEL="kimi-k3:cloud"

die() { echo "ERROR: $*" >&2; exit 1; }

# `take N` is `head -n N` that reads all its input: under pipefail, a reader
# that stops early kills the writer with SIGPIPE, failing the pipeline and,
# under set -e, the script. Use it instead of head in every pipeline.
take() { awk -v n="$1" 'NR <= n'; }
log() { echo "[$(date +%H:%M:%S)] $*" >&2; }

# A task id is <big-rock>--<small-rock>, each 1-3 lowercase words joined by '-'.
check_task_id() {
    [[ "$1" =~ ^[a-z0-9]+(-[a-z0-9]+)*--[a-z0-9]+(-[a-z0-9]+)*$ ]] \
        || die "bad task id '$1' (expected <big-rock>--<small-rock>, e.g. parser--quoted-tokens)"
}

# setting <key> <default>: the value of a "- <key>: <value>" line in
# develop-plan/goal.md (its "## Settings" section), or the default.
setting() {
    local key="$1" def="$2" val=""
    if [ -f "$DEV_PLAN/goal.md" ]; then
        val=$(sed -n "s/^- ${key}:[[:space:]]*//p" "$DEV_PLAN/goal.md" | take 1 | sed 's/[[:space:]]*$//')
    fi
    echo "${val:-$def}"
}

# The version of the develop in progress (HAISOS_VERSION on origin/develop).
develop_version() {
    local v
    v=$(git -C "$DEV_REPO" show origin/develop:HAISOS_VERSION 2>/dev/null | tr -d '[:space:]') || true
    echo "${v:-no-develop}"
}

# A stable id of the develop in progress, for its run folders: the creation
# time in develop-plan/goal.md ("- Created: 2026-09-28T10:00:00Z" becomes
# 20260928T100000Z) -- the version changes with every task, this does not.
develop_id() {
    local created
    created=$(sed -n 's/^- Created:[[:space:]]*//p' "$DEV_PLAN/goal.md" 2>/dev/null | take 1)
    [ -n "$created" ] || created=$(git -C "$DEV_REPO" show origin/develop:develop-plan/goal.md 2>/dev/null \
        | sed -n 's/^- Created:[[:space:]]*//p' | take 1)
    created=$(tr -d ':[:space:]-' <<< "$created")
    echo "${created:-no-develop}"
}

trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

# The task rows of develop-plan/playbook.md, one per line, the cells separated
# by \037 (not a tab: bash's read merges empty tab-separated fields):
#   num id status version depends pr tries review notes
# (the table's columns: | # | Task | Status | Version | Depends | PR | Tries | Review | Notes |).
playbook_rows() {
    [ -f "$DEV_PLAN/playbook.md" ] || return 0
    local num task status version depends pr tries review notes id
    grep -E '^\| *[0-9]+ *\|' "$DEV_PLAN/playbook.md" \
    | while IFS='|' read -r _ num task status version depends pr tries review notes _; do
        id=$(sed -n 's/.*\[\([a-z0-9-]*--[a-z0-9-]*\)\].*/\1/p' <<< "$task")
        [ -n "$id" ] || id=$(trim "$task")
        printf '%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\n' "$(trim "$num")" "$id" "$(trim "$status")" \
            "$(trim "$version")" "$(trim "$depends")" "$(trim "$pr")" "$(trim "$tries")" \
            "$(trim "$review")" "$(trim "$notes")"
    done
}

# A field of a task plan's header list: "- <key>: <value>".
plan_field() {
    local file="$1" key="$2"
    sed -n "s/^- ${key}:[[:space:]]*//p" "$file" | take 1 | sed 's/[[:space:]]*$//'
}

# Values of credentials present on this machine, one per line, for gate.sh to
# look for verbatim. Never printed.
local_secret_values() {
    { gh auth token 2>/dev/null || true
      for v in ANTHROPIC_API_KEY ANTHROPIC_AUTH_TOKEN HAISOS_API_KEY OLLAMA_API_KEY GH_TOKEN GITHUB_TOKEN; do
          printf '%s\n' "${!v:-}"
      done
    } | awk 'length($0) >= 12 && $0 != "ollama"'
}

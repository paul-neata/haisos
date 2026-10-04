#!/bin/bash
# Runs one task container (see .claude/develop/WORKFLOW.md, "The task container").
#
# Usage: scripts/develop/container.sh <implement|fix|test> <task-id> [options]
#   --base <rev>          the develop commit the task starts from (default origin/develop)
#   --branch-rev <rev>    fix/test: the branch head to start from (default origin/task/<id>)
#   --branch-name <name>  the branch name inside the container (default task/<id>)
#   --plan <file>         the task plan (default develop-plan/tasks/<id>.md)
#   --feedback <file>     what failed before: a CI log, review findings, a failed attempt
#   --model <model>       the Ollama model (implement, fix)
#   --timeout-min <n>     minutes the model may run (default: the "Task timeout minutes" setting, else 120)
#   --tests <selector>    check after the run: U (default), a test_linux.sh selector, ci, none
#   --continue            implement: continue the task branch left in the workspace
#   --clean-build         remove build/ and output/ first
#   --launcher <l>        ollama (default) or direct
#
# The container gets the workspace -- the repository's subrepo/ folder at
# /work, its git metadata at /gitdir (see lib.sh) -- its input (read-only) and
# its output folder; no credentials, no home directory, nothing else of /mnt.
# The user's git name and email are its commit identity. The run's files are kept in
# $HAISOS_DEVELOP_HOME/runs/<develop-id>/<task-id>/<NN>-<mode>[-<model>]/
# (the develop id is its creation time, see lib.sh).
# Prints the result.json on one line, then the run folder on the last line.
# Exit status: 0 when the container ran (read result.json for how the task
# went), 1 on an infrastructure error.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

MODE="${1:-}"; TASK_ID="${2:-}"
[ -n "$MODE" ] && [ -n "$TASK_ID" ] || die "usage: container.sh <implement|fix|test> <task-id> [options]"
shift 2
case "$MODE" in implement|fix|test) ;; *) die "unknown mode '$MODE'";; esac

BASE=origin/develop BRANCH_REV="" BRANCH_NAME="" PLAN="" FEEDBACK="" MODEL=""
TIMEOUT_MIN="" TESTS=U CONTINUE=0 CLEAN_BUILD=0 LAUNCHER=ollama
while [ $# -gt 0 ]; do
    case "$1" in
        --base) BASE="$2"; shift 2;;
        --branch-rev) BRANCH_REV="$2"; shift 2;;
        --branch-name) BRANCH_NAME="$2"; shift 2;;
        --plan) PLAN="$2"; shift 2;;
        --feedback) FEEDBACK="$2"; shift 2;;
        --model) MODEL="$2"; shift 2;;
        --timeout-min) TIMEOUT_MIN="$2"; shift 2;;
        --tests) TESTS="$2"; shift 2;;
        --continue) CONTINUE=1; shift;;
        --clean-build) CLEAN_BUILD=1; shift;;
        --launcher) LAUNCHER="$2"; shift 2;;
        *) die "unknown option '$1'";;
    esac
done

BRANCH_NAME="${BRANCH_NAME:-task/$TASK_ID}"
PLAN="${PLAN:-$DEV_PLAN/tasks/$TASK_ID.md}"
TIMEOUT_MIN="${TIMEOUT_MIN:-$(setting 'Task timeout minutes' 120)}"
if [ "$MODE" != test ]; then
    [ -n "$MODEL" ] || MODEL=$(setting 'Task models' "$DEV_DEFAULT_MODEL" | cut -d, -f1 | tr -d ' ')
    [ -f "$PLAN" ] || die "no task plan at $PLAN"
fi
[ "$MODE" = implement ] || BRANCH_REV="${BRANCH_REV:-origin/task/$TASK_ID}"

BASE_SHA=$(git -C "$DEV_REPO" rev-parse --verify -q "$BASE^{commit}") || die "no commit '$BASE'"
BRANCH_SHA=""
if [ -n "$BRANCH_REV" ]; then
    BRANCH_SHA=$(git -C "$DEV_REPO" rev-parse --verify -q "$BRANCH_REV^{commit}") || die "no commit '$BRANCH_REV'"
fi

IMAGE=$(bash "$DEV_SCRIPTS/image.sh")

lock_subrepo
ensure_subrepo

IN="$DEV_SUBREPO_HOME/in"; OUT="$DEV_SUBREPO_HOME/out"
rm -rf "$IN" "$OUT"
mkdir -p "$IN" "$OUT"

# The input: a bundle of exactly the commits the container may start from.
git -C "$DEV_REPO" update-ref refs/develop-in/base "$BASE_SHA"
BUNDLE_REFS=(refs/develop-in/base)
if [ -n "$BRANCH_SHA" ]; then
    git -C "$DEV_REPO" update-ref refs/develop-in/branch "$BRANCH_SHA"
    BUNDLE_REFS+=(refs/develop-in/branch)
fi
git -C "$DEV_REPO" bundle create "$IN/in.bundle" "${BUNDLE_REFS[@]}" 2>/dev/null \
    || die "cannot create the input bundle"
git -C "$DEV_REPO" update-ref -d refs/develop-in/base
[ -z "$BRANCH_SHA" ] || git -C "$DEV_REPO" update-ref -d refs/develop-in/branch
if [ -f "$PLAN" ]; then cp "$PLAN" "$IN/task.md"; else : > "$IN/task.md"; fi
[ -z "$FEEDBACK" ] || cp "$FEEDBACK" "$IN/feedback.md"

# The run's own folder, numbered in order.
TASK_RUNS="$DEV_HOME/runs/$(develop_id)/$TASK_ID"
mkdir -p "$TASK_RUNS"
N=$(find "$TASK_RUNS" -mindepth 1 -maxdepth 1 -type d | wc -l)
RUN_DIR="$TASK_RUNS/$(printf '%02d' $((N + 1)))-$MODE${MODEL:+-${MODEL//[:\/]/_}}"
mkdir -p "$RUN_DIR"

NAME="$DEV_CONTAINER_PREFIX-$$"
trap 'docker rm -f "$NAME" >/dev/null 2>&1 || true; sanitize_subrepo' EXIT
log "container $NAME: $MODE $TASK_ID${MODEL:+ with $MODEL}, log: $RUN_DIR/container.log"
rc=0
timeout --kill-after=30 "$((TIMEOUT_MIN + 200))m" \
    docker run --rm --init --name "$NAME" \
        --network host \
        --user "$(id -u):$(id -g)" \
        --cap-drop ALL --security-opt no-new-privileges --pids-limit 4096 \
        -v "$DEV_SUBREPO:/work" -v "$DEV_SUBREPO_GIT:/gitdir" -v "$IN:/in:ro" -v "$OUT:/out" \
        -e MODE="$MODE" -e TASK_ID="$TASK_ID" -e TASK_BRANCH="$BRANCH_NAME" \
        -e MODEL="$MODEL" -e TIMEOUT_MIN="$TIMEOUT_MIN" -e TESTS="$TESTS" \
        -e CONTINUE="$CONTINUE" -e CLEAN_BUILD="$CLEAN_BUILD" -e LAUNCHER="$LAUNCHER" \
        -e OLLAMA_HOST=127.0.0.1:11434 \
        -e USER_GIT_NAME="$(user_git_name)" -e USER_GIT_EMAIL="$(user_git_email)" \
        ${HAISOS_ENDPOINT:+-e HAISOS_ENDPOINT="$HAISOS_ENDPOINT"} \
        ${HAISOS_MODEL:+-e HAISOS_MODEL="$HAISOS_MODEL"} \
        "$IMAGE" > "$RUN_DIR/container.log" 2>&1 || rc=$?

cp "$IN/task.md" "$RUN_DIR/task.md"
[ ! -f "$IN/feedback.md" ] || cp "$IN/feedback.md" "$RUN_DIR/feedback.md"
cp -r "$OUT/." "$RUN_DIR/" 2>/dev/null || true
echo "base=$BASE_SHA branch=${BRANCH_SHA:-} image=$IMAGE docker_exit=$rc" > "$RUN_DIR/run.info"

[ -f "$RUN_DIR/result.json" ] || { tail -n 20 "$RUN_DIR/container.log" >&2; die "the container left no result.json (docker exit $rc); see $RUN_DIR/container.log"; }
jq -c . "$RUN_DIR/result.json"
echo "$RUN_DIR"

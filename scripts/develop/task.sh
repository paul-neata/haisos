#!/bin/bash
# The /develop-task pipeline, host side (see .claude/develop/WORKFLOW.md,
# "Running a task"). Deterministic on purpose: no Claude tokens are spent
# while a task is implemented, pushed and waited for.
#
# Usage:
#   task.sh run <task-id> [options]     implement the task, or resume it, up to a PR
#                                       into develop with green checks
#   task.sh fix <task-id> --feedback <file> [options]
#                                       one Ollama fix round on the pushed branch (e.g.
#                                       review findings), then gate, push and checks
#   task.sh verify <task-id> [--tests <selector>]
#                                       build and test the local branch task/<id> in the
#                                       container, no model (after a reviewer's commits)
#   task.sh push <task-id> [options]    gate and push the local branch task/<id>, then checks
# Options:
#   --models <m1,m2>          Ollama models, tried in this order (default: "Task models" setting)
#   --attempts-per-model <n>  (default: "Attempts per model" setting, else 2)
#   --ci-rounds <n>           CI fix rounds (default: "CI fix rounds" setting, else 3)
#   --timeout-min <n>         per container run (default: "Task timeout minutes", else 120)
#   --plan <file>             the task plan (default develop-plan/tasks/<id>.md)
#   --feedback <file>         fix: what to fix
#   --tests <selector>        verify: test selector (default U)
#
# Task branches are pushed by this script (and by the reviewer, through
# "push") only: fast-forward, after gate.sh let them through. Nothing from a
# task branch runs on the host.
# Windows cannot be built in the container: when only Windows checks fail, no
# Ollama round is spent on them -- the result is windows-failed, and the
# Windows build is fixed on the host after the review (see /develop-implement).
# The last lines are the report, starting with
#   RESULT: ready | merged | windows-failed | failed | ci-failed | blocked | error
# Exit status: 0 for ready or merged, 1 otherwise.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CMD="${1:-}"; TASK_ID="${2:-}"
[ -n "$CMD" ] && [ -n "$TASK_ID" ] || die "usage: task.sh <run|fix|verify|push> <task-id> [options]"
shift 2
check_task_id "$TASK_ID"

MODELS="" APM="" CIR="" TMO="" PLAN="" FEEDBACK="" TESTS=U
while [ $# -gt 0 ]; do
    case "$1" in
        --models) MODELS="$2"; shift 2;;
        --attempts-per-model) APM="$2"; shift 2;;
        --ci-rounds) CIR="$2"; shift 2;;
        --timeout-min) TMO="$2"; shift 2;;
        --plan) PLAN="$2"; shift 2;;
        --feedback) FEEDBACK="$2"; shift 2;;
        --tests) TESTS="$2"; shift 2;;
        *) die "unknown option '$1'";;
    esac
done
MODELS="${MODELS:-$(setting 'Task models' "$DEV_DEFAULT_MODEL")}"
APM="${APM:-$(setting 'Attempts per model' 2)}"
CIR="${CIR:-$(setting 'CI fix rounds' 3)}"
TMO="${TMO:-$(setting 'Task timeout minutes' 120)}"
PLAN="${PLAN:-$DEV_PLAN/tasks/$TASK_ID.md}"
IFS=',' read -r -a MODEL_LIST <<< "${MODELS// /}"
FIRST_MODEL="${MODEL_LIST[0]}"

BR="task/$TASK_ID"
BASE_BRANCH=develop
GIT=(git -C "$DEV_REPO")
TASK_RUNS="$DEV_HOME/runs/$(develop_id)/$TASK_ID"
mkdir -p "$TASK_RUNS"

WINDOWS_LOG="" PR="" PR_URL="" PR_STATE="" LAST_FAILURE="" LAST_RUN="" GATE_LINE="" CHECK_LINE="" CI_ROUNDS_USED=0
USED_MODEL="$FIRST_MODEL"

finish() {  # finish <result>
    local result="$1" tokens
    "${GIT[@]}" update-ref -d "refs/develop-quarantine/$TASK_ID" 2>/dev/null || true
    tokens=$(cat "$TASK_RUNS"/*/result.json 2>/dev/null | jq -s -r \
        '"input \(map(.usage.input_tokens // 0) | add // 0), output \(map(.usage.output_tokens // 0) | add // 0), in \(length) container runs"' \
        2>/dev/null || echo "n/a")
    {
        echo "RESULT: $result"
        echo "TASK: $TASK_ID  BRANCH: $BR  PR: ${PR:+#$PR }${PR_URL:-none}"
        echo "MODEL: $USED_MODEL  CI FIX ROUNDS: $CI_ROUNDS_USED of $CIR"
        [ -z "$CHECK_LINE" ] || echo "CONTAINER CHECK: $CHECK_LINE"
        [ -z "$GATE_LINE" ] || echo "$GATE_LINE"
        echo "OLLAMA TOKENS: $tokens"
        [ -z "$LAST_FAILURE" ] || echo "LAST FAILURE: $LAST_FAILURE"
        [ -z "$WINDOWS_LOG" ] || echo "WINDOWS LOG: $WINDOWS_LOG"
        echo "RUN FOLDER: ${LAST_RUN:-$TASK_RUNS}"
    } | tee "$TASK_RUNS/report.txt"
    case "$result" in ready|merged) exit 0;; *) exit 1;; esac
}

sync_remote() { "${GIT[@]}" fetch -q origin --prune; }

# Sets PR, PR_URL and PR_STATE (OPEN, MERGED, or empty) from the task's newest PR
# that was not closed unmerged.
find_pr() {
    local line
    PR="" PR_URL="" PR_STATE=""
    line=$(gh pr list --head "$BR" --base "$BASE_BRANCH" --state all --json number,state,url \
        -q 'sort_by(.number) | reverse | map(select(.state != "CLOSED")) | .[0] // empty | "\(.number) \(.state) \(.url)"' 2>/dev/null || true)
    [ -z "$line" ] || read -r PR PR_STATE PR_URL <<< "$line"
}

# Runs container.sh; sets LAST_RUN. Returns 1 on an infrastructure error.
container() {
    local out
    out=$(bash "$DEV_SCRIPTS/container.sh" "$@") || { LAST_FAILURE="container.sh failed: $(echo "$out" | tail -n 3 | tr '\n' ' ')"; return 1; }
    LAST_RUN=$(echo "$out" | tail -n 1)
}

# Judges a model run by its result.json; on a failure sets LAST_FAILURE and
# writes $LAST_RUN/next-feedback.md for the next attempt. Returns 0 when good.
judge_run() {
    local r="$LAST_RUN/result.json" fb="$LAST_RUN/next-feedback.md"
    local err commits build tests_exit failed passed none timed
    err=$(jq -r '.error' "$r"); commits=$(jq -r '.commits' "$r"); build=$(jq -r '.build_exit' "$r")
    tests_exit=$(jq -r '.tests.exit' "$r"); failed=$(jq -r '.tests.failed' "$r"); passed=$(jq -r '.tests.passed' "$r")
    none=$(jq -r '.tests.none_found' "$r"); timed=$(jq -r '.timed_out' "$r")
    CHECK_LINE="build exit $build, tests ($(jq -r '.tests.selector' "$r")) $passed passed, $failed failed"
    : > "$fb"
    if [ -n "$err" ]; then
        LAST_FAILURE="container: $err"
    elif [ "$commits" = 0 ]; then
        LAST_FAILURE="no commits$([ "$timed" = true ] && echo ' (timed out)')"
        { echo "The previous run made no commits$([ "$timed" = true ] && echo ' and ran out of time')."
          echo "Its summary, if any:"; sed 's/^/> /' "$LAST_RUN/summary.md" 2>/dev/null | take 40; } > "$fb"
    elif [ "$build" != 0 ]; then
        LAST_FAILURE="build failed"
        { echo "The build failed after the previous run (./scripts/build_linux_on_linux.sh). The errors:"
          echo '```'; grep -E 'error|Error' "$LAST_RUN/build.log" | take 60; echo '...'; tail -n 40 "$LAST_RUN/build.log"; echo '```'; } > "$fb"
    elif [ "$tests_exit" != 0 ] || [ "$failed" != 0 ] || [ "$none" = true ]; then
        LAST_FAILURE="tests failed ($passed passed, $failed failed)"
        { echo "The unit tests failed after the previous run (./scripts/test_linux.sh L U):"
          echo '```'; tail -n 80 "$LAST_RUN/test.log"; echo '```'; } > "$fb"
    else
        return 0
    fi
    return 1
}

# Fetches the run's bundle into refs/develop-quarantine/<id>, checks it descends
# from <parent>, and runs the gate from <gate-base>. Sets QUARANTINE.
# Returns 0 (may push), 1 (failed: LAST_FAILURE set, feedback written), 2 (blocked).
take_run() {
    local parent="$1" gate_base="$2" bundle="$LAST_RUN/out.bundle" fb="$LAST_RUN/next-feedback.md" rc=0
    QUARANTINE=""
    [ -f "$bundle" ] || { LAST_FAILURE="no bundle"; return 1; }
    "${GIT[@]}" bundle verify -q "$bundle" > /dev/null 2>&1 || { LAST_FAILURE="the bundle does not verify"; return 1; }
    "${GIT[@]}" bundle list-heads "$bundle" | grep -q " refs/heads/$BR\$" || { LAST_FAILURE="the bundle lacks $BR"; return 1; }
    "${GIT[@]}" fetch -q --no-tags "$bundle" "+refs/heads/$BR:refs/develop-quarantine/$TASK_ID" \
        || { LAST_FAILURE="cannot fetch the bundle"; return 1; }
    QUARANTINE=$("${GIT[@]}" rev-parse "refs/develop-quarantine/$TASK_ID")
    if ! "${GIT[@]}" merge-base --is-ancestor "$parent" "$QUARANTINE"; then
        LAST_FAILURE="history rewritten: ${QUARANTINE:0:12} does not descend from ${parent:0:12}"
        echo "Your commits rewrote history that already existed; only add new commits." > "$fb"
        return 1
    fi
    bash "$DEV_SCRIPTS/gate.sh" "$gate_base" "$QUARANTINE" --out "$LAST_RUN/gate.txt" > /dev/null || rc=$?
    GATE_LINE=$(head -n 1 "$LAST_RUN/gate.txt" 2>/dev/null || echo "GATE: error")
    case "$rc" in
        0|1) return 0;;
        2)  LAST_FAILURE="gate blocked: $(grep -c '^BLOCK' "$LAST_RUN/gate.txt") item(s), see $LAST_RUN/gate.txt"
            { echo "Your changes were refused by the security gate, for these reasons:"
              grep '^BLOCK' "$LAST_RUN/gate.txt"
              echo; echo "Start over without them: never touch those paths, links, secrets or large files."; } > "$fb"
            return 2;;
        *)  LAST_FAILURE="gate error"; return 1;;
    esac
}

push_quarantine() {
    "${GIT[@]}" update-ref "refs/heads/$BR" "$QUARANTINE"
    "${GIT[@]}" config "branch.$BR.haisos-base" "$BASE_BRANCH"
    "${GIT[@]}" push -q origin "$QUARANTINE:refs/heads/$BR" || { LAST_FAILURE="push rejected (not a fast-forward?)"; finish error; }
    "${GIT[@]}" update-ref -d "refs/develop-quarantine/$TASK_ID"
    log "pushed $BR at ${QUARANTINE:0:12}"
}

# The task's version: its playbook row's Version, else develop's version with
# the patch number plus one (see WORKFLOW.md, "Versions").
task_version() {
    local v cur
    v=$(playbook_rows | awk -F'\037' -v id="$TASK_ID" '$2 == id && !found {print $4; found = 1}')
    if [ -z "$v" ]; then
        cur=$("${GIT[@]}" show "origin/$BASE_BRANCH:HAISOS_VERSION" 2>/dev/null | tr -d '[:space:]')
        if [[ "$cur" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
            v="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$((BASH_REMATCH[3] + 1))"
        fi
    fi
    echo "$v"
}

ensure_pr() {
    [ -z "$PR" ] || return 0
    local title body goal version prefix develop_pr
    title=$(plan_field "$PLAN" 'PR title' 2>/dev/null || true)
    [ -n "$title" ] || title="$TASK_ID"
    version=$(task_version)
    prefix="${version:+[$version] }"
    develop_pr=$(bash "$DEV_SCRIPTS/develop_pr.sh" number 2>/dev/null || true)
    body="$TASK_RUNS/pr-body.md"
    {
        echo "${prefix}Develop task \`$TASK_ID\`${develop_pr:+, part of #$develop_pr}; planned in \`develop-plan/tasks/$TASK_ID.md\` on \`$BASE_BRANCH\`."
        echo
        goal=$(awk '/^## Goal/ {f = 1; next} /^## / {f = 0} f' "$PLAN" 2>/dev/null | sed '/./,$!d' | take 20)
        [ -z "$goal" ] || printf '%s\n\n' "$goal"
        echo "Implemented by \`$USED_MODEL\` in the task container. Container check: $CHECK_LINE."
        echo "Security gate: ${GATE_LINE#GATE: }."
        if [ -s "$LAST_RUN/summary.md" ]; then
            echo
            echo "The implementer's own summary (unreviewed, written by the model):"
            echo
            sed -e 's/^#\+ *//' -e 's/^/> /' "$LAST_RUN/summary.md" | take 40
        fi
    } > "$body"
    PR_URL=$(gh pr create --base "$BASE_BRANCH" --head "$BR" --title "$prefix$title" --body-file "$body") \
        || { LAST_FAILURE="gh pr create failed"; finish error; }
    PR="${PR_URL##*/}"
    log "opened PR #$PR"
}

# Waits for the checks; on a failure runs Ollama fix rounds. Ends the script.
ci_loop() {
    local rc ci_log
    while :; do
        ci_log="$TASK_RUNS/ci-failure-$(date +%s).log"
        rc=0
        bash "$DEV_SCRIPTS/wait_ci.sh" "$PR" --timeout-min 90 --log "$ci_log" > "$TASK_RUNS/ci-last.txt" || rc=$?
        case "$rc" in
            0) finish ready;;
            2) LAST_FAILURE="CI timed out"; finish ci-failed;;
            3) LAST_FAILURE="cannot read the PR's checks"; finish error;;
        esac
        local failed_checks
        failed_checks=$(grep -E '^(fail|cancel)' "$TASK_RUNS/ci-last.txt" | cut -f2 || true)
        LAST_FAILURE="CI failed: $(echo $failed_checks)"
        if [ -n "$failed_checks" ] && ! grep -viq windows <<< "$failed_checks"; then
            WINDOWS_LOG="$ci_log"
            finish windows-failed
        fi
        [ "$CI_ROUNDS_USED" -lt "$CIR" ] || finish ci-failed
        CI_ROUNDS_USED=$((CI_ROUNDS_USED + 1))
        local fb="$TASK_RUNS/ci-feedback.md"
        { echo "CI failed on this branch. These checks failed:"
          grep -E '^(fail|cancel)' "$TASK_RUNS/ci-last.txt" | cut -f2
          echo
          echo "The failed jobs' logs (last lines). Fix the Linux failures; Windows failures are fixed"
          echo "separately, on a Windows machine -- touch them only where the same error shows on Linux."
          echo '```'; cat "$ci_log"; echo '```'; } > "$fb"
        fix_round "$fb" || true
    done
}

# One fix run on the pushed branch; pushes when good. Returns 1 when nothing was pushed.
fix_round() {
    local fb="$1" remote parent rc=0
    sync_remote
    remote=$("${GIT[@]}" rev-parse "origin/$BR")
    container fix "$TASK_ID" --base "origin/$BASE_BRANCH" --branch-rev "$remote" --plan "$PLAN" \
        --feedback "$fb" --model "$USED_MODEL" --timeout-min "$TMO" || finish error
    judge_run || return 1
    parent=$("${GIT[@]}" merge-base "origin/$BASE_BRANCH" "$remote")
    take_run "$remote" "$parent" || rc=$?
    [ "$rc" = 0 ] || { [ "$rc" = 2 ] && finish blocked; return 1; }
    push_quarantine
}

implement() {
    local attempt=0 m i fresh rc
    local feedback=()
    for m in "${MODEL_LIST[@]}"; do
        feedback=() fresh=1                     # each model starts from develop
        for ((i = 1; i <= APM; i++)); do
            attempt=$((attempt + 1))
            USED_MODEL="$m"
            local cont=()
            [ "$fresh" = 1 ] || cont=(--continue)
            log "attempt $attempt: $m${cont[*]:+ (continuing)}"
            container implement "$TASK_ID" --base "origin/$BASE_BRANCH" --plan "$PLAN" --model "$m" \
                --timeout-min "$TMO" "${feedback[@]}" "${cont[@]}" || finish error
            rc=1
            if judge_run; then
                rc=0
                local base
                base=$(jq -r .base_sha "$LAST_RUN/result.json")
                take_run "$base" "$base" || rc=$?
                [ "$rc" != 0 ] || return 0
            fi
            feedback=(--feedback "$LAST_RUN/next-feedback.md")
            fresh=0
            [ "$rc" != 2 ] || fresh=1          # a branch the gate refused is not continued
            log "attempt $attempt failed: $LAST_FAILURE"
        done
    done
    return 1
}

# verify: judges a test-only container run.
judge_test_run() {
    local r="$LAST_RUN/result.json"
    CHECK_LINE="build exit $(jq -r .build_exit "$r"), tests ($TESTS) $(jq -r .tests.passed "$r") passed, $(jq -r .tests.failed "$r") failed"
    [ "$(jq -r .build_exit "$r")" = 0 ] && [ "$(jq -r .tests.exit "$r")" = 0 ] && [ "$(jq -r .tests.failed "$r")" = 0 ]
}

case "$CMD" in
    run)
        [ -f "$PLAN" ] || die "no task plan at $PLAN"
        sync_remote
        find_pr
        if [ "$PR_STATE" = MERGED ]; then finish merged; fi
        if [ -z "$PR_STATE" ] && ! "${GIT[@]}" rev-parse -q --verify "origin/$BR" > /dev/null; then
            implement || finish failed
            push_quarantine
        fi
        ensure_pr
        ci_loop;;
    fix)
        [ -n "$FEEDBACK" ] && [ -f "$FEEDBACK" ] || die "fix needs --feedback <file>"
        sync_remote
        find_pr
        [ "$PR_STATE" = OPEN ] || die "no open PR for $BR"
        fix_round "$FEEDBACK" || { [ -n "$LAST_FAILURE" ] || LAST_FAILURE="fix run failed"; finish failed; }
        ci_loop;;
    verify)
        sync_remote
        "${GIT[@]}" rev-parse -q --verify "refs/heads/$BR" > /dev/null || die "no local branch $BR"
        container test "$TASK_ID" --base "origin/$BASE_BRANCH" --branch-rev "refs/heads/$BR" --tests "$TESTS" || finish error
        if judge_test_run; then finish ready; else LAST_FAILURE="$CHECK_LINE"; finish failed; fi;;
    push)
        sync_remote
        find_pr
        [ "$PR_STATE" = OPEN ] || die "no open PR for $BR"
        local_sha=$("${GIT[@]}" rev-parse --verify "refs/heads/$BR") || die "no local branch $BR"
        remote=$("${GIT[@]}" rev-parse "origin/$BR")
        "${GIT[@]}" merge-base --is-ancestor "$remote" "$local_sha" || die "$BR is not a fast-forward of origin/$BR"
        LAST_RUN="$TASK_RUNS"
        rc=0
        bash "$DEV_SCRIPTS/gate.sh" "$("${GIT[@]}" merge-base "origin/$BASE_BRANCH" "$local_sha")" "$local_sha" \
            --out "$TASK_RUNS/gate-push.txt" > /dev/null || rc=$?
        GATE_LINE=$(head -n 1 "$TASK_RUNS/gate-push.txt")
        [ "$rc" != 2 ] || { LAST_FAILURE="gate blocked, see $TASK_RUNS/gate-push.txt"; finish blocked; }
        [ "$rc" != 3 ] || { LAST_FAILURE="gate error"; finish error; }
        "${GIT[@]}" push -q origin "$local_sha:refs/heads/$BR" || { LAST_FAILURE="push rejected"; finish error; }
        ci_loop;;
    *)
        die "unknown command '$CMD'";;
esac

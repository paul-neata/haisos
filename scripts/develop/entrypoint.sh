#!/bin/bash
# The task container's entrypoint (see scripts/develop/Dockerfile) -- the one
# trusted program in the container. Everything else it runs (the model, and the
# branch's own build scripts and tests) is untrusted: before collecting results
# it kills every other process in the container and resets the repository's git
# metadata, so nothing left behind can alter what it hands back.
#
# Environment:
#   MODE           implement | fix | test | interactive (/claude-docker: Claude
#                  on the terminal, on the base plus /in/uncommitted.patch)
#   TASK_ID, TASK_BRANCH
#   MODEL          the Ollama model (implement, fix)
#   TIMEOUT_MIN    minutes the model may run (default 120)
#   TESTS          after the run: U (default), another test_linux.sh selector,
#                  ci (what CI runs), or none (no build either)
#   CONTINUE=1     implement: continue the task branch already in the workspace
#   CLEAN_BUILD=1  remove build/ and output/ first
#   LAUNCHER       ollama (default: `ollama launch claude`) | direct (claude with
#                  the ANTHROPIC_* variables pointed at ollama)
#   OLLAMA_HOST    the host's ollama server (default 127.0.0.1:11434)
#   USER_GIT_NAME, USER_GIT_EMAIL   interactive: the identity for commits
# In (read-only): /in/in.bundle holding refs/develop-in/base (and, for fix and
#   test, refs/develop-in/branch), /in/task.md, /in/feedback.md (optional),
#   /in/uncommitted.patch (interactive, optional).
# Out: /out/result.json (always, written last); /out/out.bundle (implement,
#   fix and interactive, when there are new commits); /out/uncommitted.patch
#   (interactive: what was left uncommitted, new files included);
#   /out/prompt.md, /out/claude.jsonl, /out/claude.stderr, /out/summary.md
#   (written by the model), /out/build.log, /out/test.log.
set -uo pipefail

MODE="${MODE:?MODE is required}"
TASK_ID="${TASK_ID:?TASK_ID is required}"
TASK_BRANCH="${TASK_BRANCH:?TASK_BRANCH is required}"
MODEL="${MODEL:-}"
TIMEOUT_MIN="${TIMEOUT_MIN:-120}"
TESTS="${TESTS:-U}"
CONTINUE="${CONTINUE:-0}"
CLEAN_BUILD="${CLEAN_BUILD:-0}"
LAUNCHER="${LAUNCHER:-ollama}"
export OLLAMA_HOST="${OLLAMA_HOST:-127.0.0.1:11434}"

SAFE_PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH="$SAFE_PATH"
GIT_NAME="haisos-task (${MODEL:-no model})"
GIT_EMAIL="task@haisos.invalid"
if [ "$MODE" = interactive ]; then
    GIT_NAME="${USER_GIT_NAME:-$GIT_NAME}"
    GIT_EMAIL="${USER_GIT_EMAIL:-$GIT_EMAIL}"
fi

STARTED=$(date +%s)
ERROR=""
START_SHA=""
BASE_SHA=""
HEAD_SHA=""
COMMITS=0
LEFTOVER=false
WRONG_BRANCH=false
MODEL_RC=-1
TIMED_OUT=false
BUILD_RC=-1
TEST_RC=-1
PASSED=0
FAILED=0
NO_TESTS=false
BUNDLE=false
UNCOMMITTED_FILES=0
HEAD_BRANCH=""

say() { echo "[entrypoint $(date +%H:%M:%S)] $*"; }

# git as the entrypoint runs it: no global or system config, no hooks, no
# fsmonitor, no pager, no signing -- whatever the untrusted programs left.
g() {
    env -i PATH="$SAFE_PATH" HOME=/nonexistent LANG=C.UTF-8 ${GIT_INDEX_FILE:+GIT_INDEX_FILE="$GIT_INDEX_FILE"} \
        GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_TERMINAL_PROMPT=0 \
        GIT_AUTHOR_NAME="$GIT_NAME" GIT_AUTHOR_EMAIL="$GIT_EMAIL" \
        GIT_COMMITTER_NAME="$GIT_NAME" GIT_COMMITTER_EMAIL="$GIT_EMAIL" \
        git -c core.hooksPath=/dev/null -c core.fsmonitor=false -c core.pager=cat \
            -c commit.gpgSign=false -c core.untrackedCache=false -C /work "$@"
}

# Undoes what an earlier program may have planted in the repository's metadata.
reset_git_meta() {
    if [ ! -d /work/.git ] || [ -L /work/.git ]; then
        rm -rf /work/.git
        g init -q
    fi
    rm -rf /work/.git/hooks /work/.git/info/attributes /work/.git/config /work/.git/objects/info/alternates
    mkdir -p /work/.git/hooks
    printf '[core]\n\trepositoryformatversion = 0\n\tfilemode = true\n\tbare = false\n\tlogallrefupdates = true\n' \
        > /work/.git/config
}

# Kills every process in the container except the init and this script.
kill_others() {
    kill -KILL -1 2>/dev/null
    sleep 1
}

fetch_in() {
    g fetch -q --no-tags --force /in/in.bundle '+refs/develop-in/*:refs/in/*'
}

prepare() {
    reset_git_meta
    fetch_in || { ERROR="cannot fetch /in/in.bundle"; return 1; }
    case "$MODE" in
        implement)
            if [ "$CONTINUE" = 1 ] && g rev-parse -q --verify "refs/heads/$TASK_BRANCH" >/dev/null \
                && g merge-base --is-ancestor refs/in/base "refs/heads/$TASK_BRANCH"; then
                g checkout -q -f "$TASK_BRANCH" || { ERROR="cannot check out $TASK_BRANCH"; return 1; }
                say "continuing $TASK_BRANCH at $(g rev-parse --short HEAD)"
            else
                g checkout -q -f -B "$TASK_BRANCH" refs/in/base || { ERROR="cannot create $TASK_BRANCH"; return 1; }
            fi ;;
        fix|test)
            g checkout -q -f -B "$TASK_BRANCH" refs/in/branch || { ERROR="cannot check out $TASK_BRANCH"; return 1; } ;;
        interactive)
            g checkout -q -f -B "$TASK_BRANCH" refs/in/base || { ERROR="cannot create $TASK_BRANCH"; return 1; } ;;
        *)
            ERROR="unknown MODE '$MODE'"; return 1 ;;
    esac
    g clean -q -ffdx -e /build/ -e /output/ -e /extern/
    if [ "$CLEAN_BUILD" = 1 ]; then
        rm -rf /work/build /work/output
    fi
    if [ "$MODE" = interactive ] && [ -s /in/uncommitted.patch ]; then
        g apply --binary --whitespace=nowarn /in/uncommitted.patch \
            || { ERROR="the uncommitted changes do not apply"; return 1; }
    fi
    START_SHA=$(g rev-parse HEAD)
    BASE_SHA=$(g rev-parse refs/in/base)
    say "$MODE $TASK_ID on $TASK_BRANCH at ${START_SHA:0:12} (base ${BASE_SHA:0:12})"
}

run_model() {
    [ -n "$MODEL" ] || { ERROR="no MODEL given"; return 1; }
    local instructions template prompt
    case "$MODE" in
        implement)
            instructions="Implement the task plan below completely: every change, test and doc
update it lists; then go through its acceptance checklist. The plan was
written against the current code, but the code is the truth: where the plan
is wrong, do what it intends and note the deviation."
            if [ -s /in/feedback.md ]; then
                instructions+="

An earlier attempt at this task failed; the feedback at the end says why."
                if [ "$CONTINUE" = 1 ]; then
                    instructions+=" The branch already holds that attempt's commits:
fix them rather than starting over."
                fi
            fi ;;
        fix)
            instructions="The task plan below is already implemented on this branch, and a check
failed afterwards. The feedback at the end says what failed: a CI log, or
findings of the code review to fix. Fix exactly that, keep the rest of the
branch as it is, and commit the fix." ;;
    esac
    template=$(cat /opt/haisos-develop/task_prompt.md)
    prompt=${template//'{{MODE_INSTRUCTIONS}}'/$instructions}
    prompt=${prompt//'{{TASK_ID}}'/$TASK_ID}
    prompt=${prompt//'{{MODE}}'/$MODE}
    prompt=${prompt//'{{BRANCH}}'/$TASK_BRANCH}
    prompt=${prompt//'{{TIMEOUT_MIN}}'/$TIMEOUT_MIN}
    prompt+=$'\n\n## The task plan\n\n'"$(cat /in/task.md)"
    if [ -s /in/feedback.md ]; then
        prompt+=$'\n\n## Feedback\n\n'"$(cat /in/feedback.md)"
    fi
    printf '%s\n' "$prompt" > /out/prompt.md

    local settings='{"attribution":{"commit":"","pr":""}}'
    local args=(-p "$prompt" --dangerously-skip-permissions --output-format stream-json --verbose --settings "$settings")
    say "running $MODEL via $LAUNCHER for at most $TIMEOUT_MIN minutes"
    (
        cd /work || exit 2
        export ANTHROPIC_DEFAULT_OPUS_MODEL="$MODEL" ANTHROPIC_DEFAULT_SONNET_MODEL="$MODEL"
        export ANTHROPIC_DEFAULT_HAIKU_MODEL="$MODEL" CLAUDE_CODE_SUBAGENT_MODEL="$MODEL"
        export DISABLE_AUTOUPDATER=1 DISABLE_TELEMETRY=1 DISABLE_ERROR_REPORTING=1
        export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1
        export GIT_AUTHOR_NAME="$GIT_NAME" GIT_AUTHOR_EMAIL="$GIT_EMAIL"
        export GIT_COMMITTER_NAME="$GIT_NAME" GIT_COMMITTER_EMAIL="$GIT_EMAIL"
        case "$LAUNCHER" in
            ollama)
                exec timeout --kill-after=60 "${TIMEOUT_MIN}m" \
                    ollama launch claude --model "$MODEL" --yes -- "${args[@]}" ;;
            direct)
                export ANTHROPIC_BASE_URL="http://$OLLAMA_HOST" ANTHROPIC_AUTH_TOKEN=ollama ANTHROPIC_API_KEY=""
                exec timeout --kill-after=60 "${TIMEOUT_MIN}m" claude --model "$MODEL" "${args[@]}" ;;
            *)
                echo "unknown LAUNCHER '$LAUNCHER'" >&2; exit 2 ;;
        esac
    ) < /dev/null > /out/claude.jsonl 2> /out/claude.stderr
    MODEL_RC=$?
    [ "$MODEL_RC" = 124 ] || [ "$MODEL_RC" = 137 ] && TIMED_OUT=true
    say "model exited with $MODEL_RC"
}

# After the model: nothing of it keeps running, its uncommitted work is kept.
collect_leftovers() {
    kill_others
    reset_git_meta
    local branch
    branch=$(g symbolic-ref -q --short HEAD || true)
    if [ "$branch" != "$TASK_BRANCH" ]; then
        WRONG_BRANCH=true
        g checkout -q -f "$TASK_BRANCH" 2>/dev/null
    elif [ -n "$(g status --porcelain 2>/dev/null)" ]; then
        g add -A && g commit -q -m "Uncommitted changes left by the task run" && LEFTOVER=true
    fi
}

# /claude-docker: Claude Code on the terminal, as long as the user wants it.
run_interactive() {
    [ -n "$MODEL" ] || { ERROR="no MODEL given"; return 1; }
    local settings='{"attribution":{"commit":"","pr":""}}'
    say "Claude Code on $MODEL, in /work on $TASK_BRANCH -- exit it (/exit) to end the session"
    (
        cd /work || exit 2
        export ANTHROPIC_DEFAULT_OPUS_MODEL="$MODEL" ANTHROPIC_DEFAULT_SONNET_MODEL="$MODEL"
        export ANTHROPIC_DEFAULT_HAIKU_MODEL="$MODEL" CLAUDE_CODE_SUBAGENT_MODEL="$MODEL"
        export DISABLE_AUTOUPDATER=1 DISABLE_TELEMETRY=1 DISABLE_ERROR_REPORTING=1
        export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1
        export GIT_AUTHOR_NAME="$GIT_NAME" GIT_AUTHOR_EMAIL="$GIT_EMAIL"
        export GIT_COMMITTER_NAME="$GIT_NAME" GIT_COMMITTER_EMAIL="$GIT_EMAIL"
        case "$LAUNCHER" in
            ollama)
                exec ollama launch claude --model "$MODEL" --yes -- --dangerously-skip-permissions --settings "$settings" ;;
            direct)
                export ANTHROPIC_BASE_URL="http://$OLLAMA_HOST" ANTHROPIC_AUTH_TOKEN=ollama ANTHROPIC_API_KEY=""
                exec claude --model "$MODEL" --dangerously-skip-permissions --settings "$settings" ;;
            *)
                echo "unknown LAUNCHER '$LAUNCHER'" >&2; exit 2 ;;
        esac
    )
    MODEL_RC=$?
    say "Claude exited ($MODEL_RC); collecting the session's work"
}

# /claude-docker: what was left uncommitted, new files included, as one patch
# against HEAD -- built in a separate index, after nothing else runs any more.
collect_uncommitted() {
    local idx=/tmp/claude-docker.index
    HEAD_BRANCH=$(g symbolic-ref -q --short HEAD || echo "(detached)")
    rm -f "$idx"
    GIT_INDEX_FILE="$idx" g read-tree HEAD \
        && GIT_INDEX_FILE="$idx" g add -A \
        && GIT_INDEX_FILE="$idx" g diff --cached --binary HEAD > /out/uncommitted.patch \
        || { ERROR="cannot collect the uncommitted changes"; return 1; }
    UNCOMMITTED_FILES=$(GIT_INDEX_FILE="$idx" g diff --cached --name-only HEAD | grep -c . || true)
    rm -f "$idx"
}

check_build_and_tests() {
    [ "$TESTS" = none ] && return 0
    say "building"
    (cd /work && timeout 90m bash ./scripts/build_linux_on_linux.sh) > /out/build.log 2>&1
    BUILD_RC=$?
    [ "$BUILD_RC" = 0 ] || { say "build failed ($BUILD_RC)"; return 0; }
    say "running tests ($TESTS)"
    if [ "$TESTS" = ci ]; then
        (cd /work && timeout 90m bash ./.github/scripts/run-tests-linux.sh) > /out/test.log 2>&1
    else
        (cd /work && timeout 90m bash ./scripts/test_linux.sh L "$TESTS") > /out/test.log 2>&1
    fi
    TEST_RC=$?
    read -r PASSED FAILED < <(grep -Eo 'Tests summary: [0-9]+ passed, [0-9]+ failed' /out/test.log \
        | awk '{p += $3; f += $5} END {print p + 0, f + 0}')
    if grep -q '^No tests found\.' /out/test.log && [ "$PASSED" = 0 ]; then
        NO_TESTS=true
    fi
    say "tests: $PASSED passed, $FAILED failed (exit $TEST_RC)"
}

make_bundle() {
    kill_others
    reset_git_meta
    fetch_in || { ERROR="cannot re-read /in/in.bundle"; return 1; }
    local not=refs/in/base
    [ "$MODE" = fix ] && not=refs/in/branch
    HEAD_SHA=$(g rev-parse -q --verify "refs/heads/$TASK_BRANCH" || true)
    [ -n "$HEAD_SHA" ] || { ERROR="$TASK_BRANCH is gone"; return 1; }
    COMMITS=$(g rev-list --count "$not..refs/heads/$TASK_BRANCH" 2>/dev/null || echo 0)
    if [ "$COMMITS" -gt 0 ]; then
        g bundle create /out/out.bundle "refs/heads/$TASK_BRANCH" "^$not" 2>/dev/null && BUNDLE=true
        [ "$BUNDLE" = true ] || ERROR="cannot create /out/out.bundle"
    fi
}

write_result() {
    local usage
    usage=$(jq -c -R 'fromjson? | select(type == "object" and .type == "result")
            | {num_turns, is_error, subtype, duration_ms,
               input_tokens: (.usage.input_tokens // 0),
               output_tokens: (.usage.output_tokens // 0),
               cache_read_input_tokens: (.usage.cache_read_input_tokens // 0)}' \
            /out/claude.jsonl 2>/dev/null | tail -n 1)
    [ -n "$usage" ] || usage='{}'
    jq -n \
        --arg mode "$MODE" --arg task "$TASK_ID" --arg branch "$TASK_BRANCH" \
        --arg model "$MODEL" --arg launcher "$LAUNCHER" --arg tests "$TESTS" \
        --arg start "$START_SHA" --arg base "$BASE_SHA" --arg head "$HEAD_SHA" \
        --arg error "$ERROR" \
        --argjson commits "${COMMITS:-0}" --argjson leftover "$LEFTOVER" \
        --argjson wrong_branch "$WRONG_BRANCH" --argjson model_exit "$MODEL_RC" \
        --argjson timed_out "$TIMED_OUT" --argjson build_exit "$BUILD_RC" \
        --argjson test_exit "$TEST_RC" --argjson passed "${PASSED:-0}" \
        --argjson failed "${FAILED:-0}" --argjson no_tests "$NO_TESTS" \
        --argjson bundle "$BUNDLE" --argjson seconds "$(( $(date +%s) - STARTED ))" \
        --argjson usage "$usage" \
        --arg head_branch "$HEAD_BRANCH" --argjson uncommitted_files "${UNCOMMITTED_FILES:-0}" \
        '{mode: $mode, task: $task, branch: $branch, model: $model, launcher: $launcher,
          start_sha: $start, base_sha: $base, head_sha: $head, commits: $commits,
          leftover_autocommit: $leftover, wrong_branch: $wrong_branch,
          model_exit: $model_exit, timed_out: $timed_out,
          build_exit: $build_exit,
          tests: {selector: $tests, exit: $test_exit, passed: $passed, failed: $failed, none_found: $no_tests},
          bundle: $bundle, seconds: $seconds, usage: $usage, error: $error,
          head_branch: $head_branch, uncommitted_files: $uncommitted_files}' \
        > /out/result.json.tmp && mv -f /out/result.json.tmp /out/result.json
}

main() {
    rm -f /out/result.json /out/out.bundle /out/uncommitted.patch
    if [ "$MODE" = interactive ]; then
        if prepare; then
            run_interactive
            kill_others
            reset_git_meta
            collect_uncommitted
            make_bundle
        fi
        write_result
        say "done: $(jq -c '{commits, uncommitted_files, head_branch, error}' /out/result.json 2>/dev/null)"
        return
    fi
    if prepare; then
        if [ "$MODE" != test ]; then
            run_model
            collect_leftovers
        fi
        check_build_and_tests
        if [ "$MODE" != test ]; then
            make_bundle
        else
            kill_others
            HEAD_SHA=$START_SHA
        fi
    fi
    write_result
    say "done: $(jq -c '{commits, build_exit, tests, timed_out, error}' /out/result.json 2>/dev/null)"
}

main
exit 0

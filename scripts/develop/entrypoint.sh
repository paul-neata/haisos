#!/bin/bash
# The task container's entrypoint (see scripts/develop/Dockerfile) -- the one
# trusted program in the container. Everything else it runs (the model, and the
# branch's own build scripts and tests) is untrusted: before collecting results
# it kills every other process in the container and resets the repository's git
# metadata, so nothing left behind can alter what it hands back.
#
# Mounts: /work, the repository's subrepo/ folder (the working tree), and
# /gitdir, its git metadata (kept off the host's repository; /work/.git is a
# file pointing there). /in read-only, /out.
#
# Environment:
#   MODE           implement | fix | test | interactive (/claude-docker: Claude
#                  on the terminal, on the base plus /in/staged.patch and
#                  /in/unstaged.patch)
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
#   USER_GIT_NAME, USER_GIT_EMAIL   the user's identity, for every commit made
#                  here (also written to ~/.gitconfig)
# In (read-only): /in/in.bundle holding refs/develop-in/base (and, for fix and
#   test, refs/develop-in/branch), /in/task.md, /in/feedback.md (optional),
#   /in/staged.patch and /in/unstaged.patch (interactive: the host's index
#   against HEAD, and its working tree against the index, new files included).
# Out: /out/result.json (always, written last); /out/out.bundle (implement,
#   fix and interactive, when there are new commits); /out/staged.patch and
#   /out/unstaged.patch (interactive: the same two, as the session left them);
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
GIT_NAME="${USER_GIT_NAME:-haisos-task (${MODEL:-no model})}"
GIT_EMAIL="${USER_GIT_EMAIL:-task@haisos.invalid}"

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
STAGED_FILES=0
IN_PROGRESS=""

say() { echo "[entrypoint $(date +%H:%M:%S)] $*"; }

# git as the entrypoint runs it: no global or system config, no hooks, no
# fsmonitor, no pager, no signing -- whatever the untrusted programs left --
# and the metadata in /gitdir, whatever /work/.git says.
g() {
    env -i PATH="$SAFE_PATH" HOME=/nonexistent LANG=C.UTF-8 ${GIT_INDEX_FILE:+GIT_INDEX_FILE="$GIT_INDEX_FILE"} \
        GIT_DIR=/gitdir GIT_WORK_TREE=/work \
        GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_TERMINAL_PROMPT=0 \
        GIT_AUTHOR_NAME="$GIT_NAME" GIT_AUTHOR_EMAIL="$GIT_EMAIL" \
        GIT_COMMITTER_NAME="$GIT_NAME" GIT_COMMITTER_EMAIL="$GIT_EMAIL" \
        git -c core.hooksPath=/dev/null -c core.fsmonitor=false -c core.pager=cat \
            -c commit.gpgSign=false -c core.untrackedCache=false -C /work "$@"
}

# Undoes what an earlier program may have planted in the repository's metadata,
# and creates it on the first run. fileMode is off: /work may be on a Windows
# drive, where every file shows as executable. (scripts/develop/lib.sh's
# sanitize_subrepo does the same on the host.)
reset_git_meta() {
    if [ ! -f /gitdir/HEAD ] || [ -L /gitdir/HEAD ] || [ -L /gitdir/objects ] || [ -L /gitdir/refs ]; then
        find /gitdir -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        g init -q
    fi
    rm -rf /gitdir/hooks /gitdir/info /gitdir/commondir /gitdir/config \
        /gitdir/objects/info/alternates /gitdir/objects/info/http-alternates
    mkdir -p /gitdir/hooks
    printf '[core]\n\trepositoryformatversion = 0\n\tfilemode = false\n\tbare = false\n\tlogallrefupdates = true\n' \
        > /gitdir/config
    rm -rf /work/.git
    printf 'gitdir: /gitdir\n' > /work/.git
}

# The user's identity for the git the model runs (the entrypoint's own git
# ignores it, see g).
write_gitconfig() {
    rm -f "$HOME/.gitconfig"
    (cd / && git config --file "$HOME/.gitconfig" user.name "$GIT_NAME" \
          && git config --file "$HOME/.gitconfig" user.email "$GIT_EMAIL") \
        || say "cannot write $HOME/.gitconfig"
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
    if [ "$MODE" = interactive ]; then
        if [ -s /in/staged.patch ]; then
            g apply --index --binary --whitespace=nowarn /in/staged.patch \
                || { ERROR="the staged changes do not apply"; return 1; }
        fi
        if [ -s /in/unstaged.patch ]; then
            g apply --binary --whitespace=nowarn /in/unstaged.patch \
                || { ERROR="the unstaged changes do not apply"; return 1; }
        fi
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

# /claude-docker: the state the session left, after nothing else runs any
# more -- its branch and HEAD, the index against HEAD (staged.patch) and the
# working tree against the index, new files included (unstaged.patch, built in
# a copy of the index, so the index itself is untouched).
collect_state() {
    local idx=/tmp/claude-docker.index tree s
    HEAD_BRANCH=$(g symbolic-ref -q --short HEAD || echo "(detached)")
    HEAD_SHA=$(g rev-parse -q --verify HEAD || true)
    for s in MERGE_HEAD REBASE_HEAD CHERRY_PICK_HEAD REVERT_HEAD rebase-merge rebase-apply; do
        [ ! -e "/gitdir/$s" ] || IN_PROGRESS="$IN_PROGRESS $s"
    done
    IN_PROGRESS="${IN_PROGRESS# }"
    [ -n "$HEAD_SHA" ] || { ERROR="the session left no HEAD commit"; return 1; }
    tree=$(g write-tree) || { ERROR="the index has unresolved conflicts"; return 1; }
    g diff --cached --binary HEAD > /out/staged.patch \
        || { ERROR="cannot collect the staged changes"; return 1; }
    STAGED_FILES=$(g diff --cached --name-only HEAD | grep -c . || true)
    rm -f "$idx"
    if [ -f /gitdir/index ]; then cp /gitdir/index "$idx"; else GIT_INDEX_FILE="$idx" g read-tree HEAD; fi
    GIT_INDEX_FILE="$idx" g add -A \
        && GIT_INDEX_FILE="$idx" g diff --cached --binary "$tree" > /out/unstaged.patch \
        || { ERROR="cannot collect the unstaged changes"; return 1; }
    UNCOMMITTED_FILES=$(GIT_INDEX_FILE="$idx" g diff --cached --name-only "$tree" | grep -c . || true)
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
    local not=refs/in/base branch="$TASK_BRANCH"
    [ "$MODE" = fix ] && not=refs/in/branch
    # interactive: the branch the session ended on, whatever its name
    [ "$MODE" = interactive ] && branch="$HEAD_BRANCH"
    HEAD_SHA=$(g rev-parse -q --verify "refs/heads/$branch" || true)
    [ -n "$HEAD_SHA" ] || { ERROR="$branch is gone"; return 1; }
    COMMITS=$(g rev-list --count "$not..refs/heads/$branch" 2>/dev/null || echo 0)
    if [ "$COMMITS" -gt 0 ]; then
        g bundle create /out/out.bundle "refs/heads/$branch" "^$not" 2>/dev/null && BUNDLE=true
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
        --argjson staged_files "${STAGED_FILES:-0}" --arg in_progress "$IN_PROGRESS" \
        '{mode: $mode, task: $task, branch: $branch, model: $model, launcher: $launcher,
          start_sha: $start, base_sha: $base, head_sha: $head, commits: $commits,
          leftover_autocommit: $leftover, wrong_branch: $wrong_branch,
          model_exit: $model_exit, timed_out: $timed_out,
          build_exit: $build_exit,
          tests: {selector: $tests, exit: $test_exit, passed: $passed, failed: $failed, none_found: $no_tests},
          bundle: $bundle, seconds: $seconds, usage: $usage, error: $error,
          head_branch: $head_branch, staged_files: $staged_files,
          uncommitted_files: $uncommitted_files, in_progress: $in_progress}' \
        > /out/result.json.tmp && mv -f /out/result.json.tmp /out/result.json
}

main() {
    rm -f /out/result.json /out/out.bundle /out/staged.patch /out/unstaged.patch
    write_gitconfig
    if [ "$MODE" = interactive ]; then
        if prepare; then
            run_interactive
            kill_others
            reset_git_meta
            if collect_state && [ "$HEAD_BRANCH" != "(detached)" ]; then
                make_bundle
            elif [ -z "$ERROR" ]; then
                ERROR="the session ended on a detached HEAD: switch to a branch before exiting"
            fi
        fi
        reset_git_meta
        write_result
        say "done: $(jq -c '{head_branch, commits, staged_files, uncommitted_files, in_progress, error}' /out/result.json 2>/dev/null)"
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
    reset_git_meta
    write_result
    say "done: $(jq -c '{commits, build_exit, tests, timed_out, error}' /out/result.json 2>/dev/null)"
}

main
exit 0

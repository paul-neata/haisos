#!/bin/bash
# /claude-docker: an interactive Claude Code on an Ollama model, in the task
# container of the develop workflow, working in the repository's subrepo/
# folder (see lib.sh). The session starts from this clone's exact state -- its
# branch and commit, its index (what is staged) and its working tree (what is
# not, new files included) -- and this clone ends in the session's exact state:
# the branch it ended on, its history (rewritten or not), its index and its
# working tree. What comes back is commits and two patches only, checked by the
# security gate before anything is applied (see .claude/skills/claude-docker/SKILL.md).
#
# Usage:
#   claude_docker.sh prepare [<model>] [--launcher ollama|direct]
#                         snapshot this clone's state, write the session;
#                         prints "SESSION: <id>" (model: glm-5.3:cloud by default)
#   claude_docker.sh open <id>     open a terminal tab running the session (or
#                                  print the command to run)
#   claude_docker.sh run <id>      the interactive container itself (in a terminal)
#   claude_docker.sh wait <id>     wait until the session has run and ended
#   claude_docker.sh collect <id>  quarantine what came back, gate it; prints the report
#   claude_docker.sh apply <id>    put this clone in the session's state (a stash
#                                  and refs keep the state before)
#   claude_docker.sh discard <id>  drop the session's refs and files
# Sessions live in $HAISOS_DEVELOP_HOME/claude-docker/<id>/; the container's
# home (claude-docker/home, so Claude Code keeps its settings and
# conversations) persists between sessions. One session runs at a time, and
# never with a task container on the same subrepo.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CD="$DEV_HOME/claude-docker"
GIT=(git -C "$DEV_REPO")
CMD="${1:-}"; shift || true

session_dir() {
    [ -n "${1:-}" ] || die "no session id"
    [[ "$1" =~ ^[A-Za-z0-9._-]+$ ]] || die "bad session id '$1'"
    [ -d "$CD/$1" ] || die "no session $1 in $CD"
    echo "$CD/$1"
}
info() { sed -n "s/^$1=//p" "$SDIR/session.info" | take 1; }

# This clone's uncommitted state as two binary patches: the index against HEAD
# (<dir>/staged.patch) and the working tree against the index, new files
# included (<dir>/unstaged.patch) -- the second built in a copy of the index,
# so the real one is untouched.
snapshot() {  # snapshot <dir>
    local idx real tree
    "${GIT[@]}" diff --cached --binary HEAD > "$1/staged.patch"
    tree=$("${GIT[@]}" write-tree) || die "the index has unresolved conflicts"
    real=$("${GIT[@]}" rev-parse --git-path index)
    [[ "$real" = /* ]] || real="$DEV_REPO/$real"
    idx=$(mktemp)
    if [ -f "$real" ]; then cp "$real" "$idx"; else rm -f "$idx"; GIT_INDEX_FILE="$idx" "${GIT[@]}" read-tree HEAD; fi
    GIT_INDEX_FILE="$idx" "${GIT[@]}" add -A
    GIT_INDEX_FILE="$idx" "${GIT[@]}" diff --cached --binary "$tree" > "$1/unstaged.patch"
    rm -f "$idx"
}
snapshot_hash() {  # snapshot_hash <dir>: one hash of branch, HEAD and both patches
    { "${GIT[@]}" symbolic-ref -q HEAD || echo detached; "${GIT[@]}" rev-parse HEAD
      cat "$1/staged.patch" "$1/unstaged.patch"; } | sha256sum | cut -c1-64
}
patch_files() { grep -c '^diff --git' "$1" 2>/dev/null || true; }

case "$CMD" in
prepare)
    MODEL="$DEV_DEFAULT_MODEL" LAUNCHER=ollama
    while [ $# -gt 0 ]; do
        case "$1" in
            --launcher) LAUNCHER="$2"; shift 2;;
            -*) die "unknown option '$1'";;
            *) MODEL="$1"; shift;;
        esac
    done
    for state in MERGE_HEAD REBASE_HEAD CHERRY_PICK_HEAD REVERT_HEAD rebase-merge rebase-apply; do
        [ ! -e "$("${GIT[@]}" rev-parse --git-path "$state")" ] || die "a merge, rebase or cherry-pick is in progress: finish it first"
    done
    BRANCH=$("${GIT[@]}" symbolic-ref -q --short HEAD) || die "HEAD is detached: switch to a branch first"
    curl -fsS -m 20 http://127.0.0.1:11434/api/show -d "{\"model\":\"$MODEL\"}" 2>/dev/null | grep -q '"tools"' \
        || die "ollama has no model '$MODEL' with tool calling (ollama pull $MODEL?)"
    mkdir -p "$CD"
    if subrepo_locked; then die "a container is running on $DEV_SUBREPO (a task or a /claude-docker session)"; fi
    IMAGE=$(bash "$DEV_SCRIPTS/image.sh")
    BASE=$("${GIT[@]}" rev-parse HEAD)
    ID="$(date +%Y%m%d-%H%M%S)-${MODEL//[^A-Za-z0-9._-]/_}"
    SDIR="$CD/$ID"
    mkdir -p "$SDIR/in" "$SDIR/out"
    "${GIT[@]}" update-ref refs/develop-in/base "$BASE"
    "${GIT[@]}" bundle create "$SDIR/in/in.bundle" refs/develop-in/base 2>/dev/null || die "cannot create the input bundle"
    "${GIT[@]}" update-ref -d refs/develop-in/base
    snapshot "$SDIR/in"
    {
        echo "model=$MODEL"; echo "launcher=$LAUNCHER"; echo "image=$IMAGE"
        echo "repo=$DEV_REPO"; echo "branch=$BRANCH"; echo "base=$BASE"
        echo "snapshot=$(snapshot_hash "$SDIR/in")"
    } > "$SDIR/session.info"
    cat > "$SDIR/run.sh" <<EOF
#!/bin/bash
cd "$DEV_REPO" && bash "$DEV_SCRIPTS/claude_docker.sh" run "$ID"
echo
read -r -p "The session has ended -- press Enter to close this tab. " _
EOF
    echo "SESSION: $ID"
    echo "BRANCH: $BRANCH at ${BASE:0:12}, $(patch_files "$SDIR/in/staged.patch") staged and $(patch_files "$SDIR/in/unstaged.patch") unstaged file(s) copied"
    echo "SUBREPO: $DEV_SUBREPO"
    echo "MODEL: $MODEL"
    echo "RUN: bash $SDIR/run.sh";;

open)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    RUN="bash $SDIR/run.sh"
    if command -v wt.exe > /dev/null 2>&1 && [ -n "${WSL_DISTRO_NAME:-}" ]; then
        (cd /mnt/c 2>/dev/null || true; wt.exe -w 0 nt --title "claude-docker $(info model)" \
            wsl.exe -d "$WSL_DISTRO_NAME" -- bash "$SDIR/run.sh") > /dev/null 2>&1 \
            && { echo "OPENED: a Windows Terminal tab runs the session"; exit 0; }
    fi
    if command -v cmd.exe > /dev/null 2>&1 && [ -n "${WSL_DISTRO_NAME:-}" ]; then
        (cd /mnt/c 2>/dev/null || true; cmd.exe /c start "claude-docker" wsl.exe -d "$WSL_DISTRO_NAME" -- bash "$SDIR/run.sh") \
            > /dev/null 2>&1 && { echo "OPENED: a console window runs the session"; exit 0; }
    fi
    echo "NOT OPENED: run this in a terminal: $RUN";;

run)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    [ -t 0 ] && [ -t 1 ] || die "run needs a terminal (use: bash $SDIR/run.sh)"
    mkdir -p "$CD/home"
    lock_subrepo
    exec 8>"$CD/lock"
    flock -n 8 || die "a /claude-docker session is running (its home is shared)"
    ensure_subrepo
    rm -rf "$SDIR/out"; mkdir -p "$SDIR/out"
    trap sanitize_subrepo EXIT
    docker run --rm -it --init --name "claude-docker-$ID" \
        --network host \
        --user "$(id -u):$(id -g)" \
        --cap-drop ALL --security-opt no-new-privileges --pids-limit 4096 \
        -v "$DEV_SUBREPO:/work" -v "$DEV_SUBREPO_GIT:/gitdir" \
        -v "$SDIR/in:/in:ro" -v "$SDIR/out:/out" -v "$CD/home:/home/dev" \
        -e MODE=interactive -e TASK_ID=claude-docker -e TASK_BRANCH="$(info branch)" \
        -e MODEL="$(info model)" -e LAUNCHER="$(info launcher)" -e OLLAMA_HOST=127.0.0.1:11434 \
        -e USER_GIT_NAME="$(user_git_name)" -e USER_GIT_EMAIL="$(user_git_email)" \
        -e TERM="${TERM:-xterm-256color}" -e COLORTERM="${COLORTERM:-}" \
        "$(info image)" || true
    [ -f "$SDIR/out/result.json" ] || echo "(no result from the container)"
    touch "$SDIR/ended";;

wait)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    START_DEADLINE=$(( $(date +%s) + 30 * 60 ))
    while ! docker ps -q --filter "name=^claude-docker-$ID\$" | grep -q . && [ ! -e "$SDIR/ended" ]; do
        [ "$(date +%s)" -lt "$START_DEADLINE" ] || { echo "WAIT: the session did not start within 30 minutes"; exit 2; }
        sleep 5
    done
    while docker ps -q --filter "name=^claude-docker-$ID\$" | grep -q .; do sleep 5; done
    for _ in $(seq 1 12); do [ -e "$SDIR/ended" ] && break; sleep 5; done
    echo "WAIT: the session has ended$([ -f "$SDIR/out/result.json" ] || echo ', but left no result')";;

collect)
    # Builds, as git objects only: $Q/head (the branch's commits), $Q/staged
    # (head plus the index) and $Q/all (plus the working tree), then gates
    # $Q/all from where the session's history meets the start.
    SDIR=$(session_dir "${1:-}"); ID="$1"
    R="$SDIR/out/result.json"
    [ -f "$R" ] || die "the session left no result.json"
    ERR=$(jq -r .error "$R")
    [ -z "$ERR" ] || die "the container reported: $ERR"
    BASE=$(info base); Q="refs/claude-docker/$ID"
    ENDED=$(jq -r .head_branch "$R")
    "${GIT[@]}" check-ref-format --branch "$ENDED" > /dev/null 2>&1 || die "the session ended on a bad branch name '$ENDED'"
    HEAD1=$(jq -r .head_sha "$R")
    [[ "$HEAD1" =~ ^[0-9a-f]{40}$ ]] || die "bad head_sha in result.json"
    if [ -f "$SDIR/out/out.bundle" ]; then
        "${GIT[@]}" bundle verify -q "$SDIR/out/out.bundle" > /dev/null 2>&1 || die "the bundle does not verify"
        "${GIT[@]}" fetch -q --no-tags "$SDIR/out/out.bundle" "+refs/heads/$ENDED:$Q/head" || die "cannot fetch the bundle"
        [ "$("${GIT[@]}" rev-parse "$Q/head")" = "$HEAD1" ] || die "the bundle's head is not head_sha"
    else
        # No new commits: the head is part of the start's history.
        "${GIT[@]}" merge-base --is-ancestor "$HEAD1" "$BASE" 2>/dev/null || die "no bundle, and ${HEAD1:0:12} is not in the start's history"
        "${GIT[@]}" update-ref "$Q/head" "$HEAD1"
    fi
    build_on() {  # build_on <parent> <patch> <message>: prints the commit
        local idx tree
        idx=$(mktemp); rm -f "$idx"
        GIT_INDEX_FILE="$idx" "${GIT[@]}" read-tree "$1"
        if [ -s "$2" ]; then
            GIT_INDEX_FILE="$idx" "${GIT[@]}" apply --cached --binary "$2" || { rm -f "$idx"; return 1; }
        fi
        tree=$(GIT_INDEX_FILE="$idx" "${GIT[@]}" write-tree); rm -f "$idx"
        "${GIT[@]}" commit-tree "$tree" -p "$1" -m "$3"
    }
    STAGED=$(build_on "$HEAD1" "$SDIR/out/staged.patch" "claude-docker $ID: staged") || die "the staged changes do not apply to ${HEAD1:0:12}"
    ALL=$(build_on "$STAGED" "$SDIR/out/unstaged.patch" "claude-docker $ID: unstaged") || die "the unstaged changes do not apply"
    "${GIT[@]}" update-ref "$Q/staged" "$STAGED"
    "${GIT[@]}" update-ref "$Q/all" "$ALL"
    MB=$("${GIT[@]}" merge-base "$BASE" "$HEAD1" 2>/dev/null) || die "the session's history shares nothing with the start"
    echo "ENDED ON: $ENDED at ${HEAD1:0:12}$([ "$ENDED" = "$(info branch)" ] || echo " (started on $(info branch))")"
    if [ "$MB" = "$BASE" ]; then
        echo "HISTORY: $("${GIT[@]}" rev-list --count "$BASE..$HEAD1") new commit(s) on top of the start"
    else
        echo "HISTORY: REWRITTEN -- $("${GIT[@]}" rev-list --count "$MB..$BASE") commit(s) of the start replaced by $("${GIT[@]}" rev-list --count "$MB..$HEAD1") (from ${MB:0:12})"
    fi
    IP=$(jq -r .in_progress "$R"); [ -z "$IP" ] || echo "NOTE: ended with an operation in progress ($IP); it is not carried over"
    echo "STAGED: $(jq -r .staged_files "$R") file(s)   UNSTAGED: $(jq -r .uncommitted_files "$R") file(s)"
    "${GIT[@]}" diff --stat=100,60 "$MB" "$ALL" | tail -n 25 | sed 's/^/  /'
    rc=0
    bash "$DEV_SCRIPTS/gate.sh" "$MB" "$ALL" --out "$SDIR/gate.txt" > /dev/null || rc=$?
    head -n 1 "$SDIR/gate.txt"
    echo "GATE REPORT: $SDIR/gate.txt   (diff to read: git diff $MB $Q/all -- <path>)"
    exit "$rc";;

apply)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    BRANCH=$(info branch); BASE=$(info base); Q="refs/claude-docker/$ID"
    "${GIT[@]}" rev-parse -q --verify "$Q/all" > /dev/null || die "run collect first"
    ENDED=$(jq -r .head_branch "$SDIR/out/result.json")
    HEAD1=$("${GIT[@]}" rev-parse "$Q/head")
    NOW=$(mktemp -d); snapshot "$NOW"
    [ "$(snapshot_hash "$NOW")" = "$(info snapshot)" ] \
        || { rm -rf "$NOW"; echo "CHANGED: this clone is no longer as the session started (branch, commit, staged or unstaged changes); nothing applied"; exit 4; }
    rm -rf "$NOW"
    cd "$DEV_REPO" || exit 1
    git update-ref "$Q/before" "$BASE"
    if [ -s "$SDIR/in/staged.patch" ] || [ -s "$SDIR/in/unstaged.patch" ]; then
        git stash push -q --include-untracked -m "claude-docker $ID: the state before" \
            && echo "BACKUP: git stash list -> \"claude-docker $ID: the state before\" (staged and unstaged changes)"
    fi
    echo "BACKUP: $Q/before = $BRANCH at ${BASE:0:12}"
    if [ "$ENDED" = "$BRANCH" ]; then
        git reset -q --hard "$HEAD1" || { echo "FAILED: cannot move $BRANCH to ${HEAD1:0:12}; the state before is in the stash"; exit 5; }
    else
        if OLD=$(git rev-parse -q --verify "refs/heads/$ENDED"); then
            git update-ref "$Q/before-$ENDED" "$OLD"
            echo "BACKUP: $Q/before-$ENDED = $ENDED at ${OLD:0:12}"
        fi
        git checkout -q -B "$ENDED" "$HEAD1" || { echo "FAILED: cannot check out $ENDED at ${HEAD1:0:12}; the state before is in the stash"; exit 5; }
    fi
    echo "BRANCH: $ENDED at ${HEAD1:0:12}"
    if [ -s "$SDIR/out/staged.patch" ]; then
        git apply --index --binary --whitespace=nowarn "$SDIR/out/staged.patch" \
            || { echo "FAILED: the staged changes do not apply; the state before is in the stash"; exit 5; }
        echo "STAGED: $(patch_files "$SDIR/out/staged.patch") file(s)"
    fi
    if [ -s "$SDIR/out/unstaged.patch" ]; then
        git apply --binary --whitespace=nowarn "$SDIR/out/unstaged.patch" \
            || { echo "FAILED: the unstaged changes do not apply; the state before is in the stash"; exit 5; }
        echo "UNSTAGED: $(patch_files "$SDIR/out/unstaged.patch") file(s)"
    fi
    echo "APPLIED: session $ID";;

discard)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    "${GIT[@]}" for-each-ref --format='%(refname)' "refs/claude-docker/$ID/" | while read -r r; do
        "${GIT[@]}" update-ref -d "$r"
    done
    rm -rf "$SDIR"
    echo "DISCARDED: session $ID";;

*)
    die "usage: claude_docker.sh prepare|open|run|wait|collect|apply|discard ...";;
esac

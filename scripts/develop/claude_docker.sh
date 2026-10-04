#!/bin/bash
# /claude-docker: an interactive Claude Code on an Ollama model, in the task
# container of the develop workflow, on a copy of this repository -- the
# current branch plus its uncommitted changes. The host takes back only
# commits and a patch, runs the security gate on them, and applies them to
# the working tree only when told to (see .claude/skills/claude-docker/SKILL.md).
#
# Usage:
#   claude_docker.sh prepare [<model>] [--launcher ollama|direct]
#                         snapshot the branch and its uncommitted changes, write
#                         the session; prints "SESSION: <id>"
#   claude_docker.sh open <id>     open a terminal tab running the session (or
#                                  print the command to run)
#   claude_docker.sh run <id>      the interactive container itself (in a terminal)
#   claude_docker.sh wait <id>     wait until the session has run and ended
#   claude_docker.sh collect <id>  quarantine what came back, gate it; prints the report
#   claude_docker.sh apply <id>    bring it into the working tree (a stash keeps
#                                  the state before)
#   claude_docker.sh discard <id>  drop the session's refs and files
# Sessions live in $HAISOS_DEVELOP_HOME/claude-docker/<id>/; the container's
# workspace (claude-docker/work) and home (claude-docker/home, so Claude Code
# keeps its settings and conversations) persist between sessions. One session
# runs at a time.
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

# The working tree's uncommitted state as one binary patch against HEAD, new
# files included -- built in a separate index, so the real one is untouched.
snapshot_patch() {  # snapshot_patch <out-file>
    local idx
    idx=$(mktemp)
    rm -f "$idx"
    GIT_INDEX_FILE="$idx" "${GIT[@]}" read-tree HEAD
    GIT_INDEX_FILE="$idx" "${GIT[@]}" add -A
    GIT_INDEX_FILE="$idx" "${GIT[@]}" diff --cached --binary HEAD > "$1"
    rm -f "$idx"
}

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
    for state in MERGE_HEAD REBASE_HEAD CHERRY_PICK_HEAD rebase-merge rebase-apply; do
        [ ! -e "$("${GIT[@]}" rev-parse --git-path "$state")" ] || die "a merge, rebase or cherry-pick is in progress: finish it first"
    done
    curl -fsS -m 20 http://127.0.0.1:11434/api/show -d "{\"model\":\"$MODEL\"}" 2>/dev/null | grep -q '"tools"' \
        || die "ollama has no model '$MODEL' with tool calling (ollama pull $MODEL?)"
    mkdir -p "$CD"
    if [ -e "$CD/lock" ] && ! flock -n "$CD/lock" true; then die "a /claude-docker session is running"; fi
    IMAGE=$(bash "$DEV_SCRIPTS/image.sh")
    BRANCH=$("${GIT[@]}" symbolic-ref -q --short HEAD || echo claude-docker)
    BASE=$("${GIT[@]}" rev-parse HEAD)
    ID="$(date +%Y%m%d-%H%M%S)-${MODEL//[^A-Za-z0-9._-]/_}"
    SDIR="$CD/$ID"
    mkdir -p "$SDIR/in" "$SDIR/out"
    "${GIT[@]}" update-ref refs/develop-in/base "$BASE"
    "${GIT[@]}" bundle create "$SDIR/in/in.bundle" refs/develop-in/base 2>/dev/null || die "cannot create the input bundle"
    "${GIT[@]}" update-ref -d refs/develop-in/base
    snapshot_patch "$SDIR/in/uncommitted.patch"
    {
        echo "model=$MODEL"; echo "launcher=$LAUNCHER"; echo "image=$IMAGE"
        echo "repo=$DEV_REPO"; echo "branch=$BRANCH"; echo "base=$BASE"
        echo "snapshot=$(sha256sum < "$SDIR/in/uncommitted.patch" | cut -c1-64)"
    } > "$SDIR/session.info"
    cat > "$SDIR/run.sh" <<EOF
#!/bin/bash
cd "$DEV_REPO" && bash "$DEV_SCRIPTS/claude_docker.sh" run "$ID"
echo
read -r -p "The session has ended -- press Enter to close this tab. " _
EOF
    FILES=$(grep -c '^diff --git' "$SDIR/in/uncommitted.patch" || true)
    echo "SESSION: $ID"
    echo "BRANCH: $BRANCH at ${BASE:0:12}, $FILES uncommitted file(s) copied"
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
    mkdir -p "$CD/work" "$CD/home"
    exec 9>"$CD/lock"
    flock -n 9 || die "a /claude-docker session is running"
    rm -rf "$SDIR/out"; mkdir -p "$SDIR/out"
    docker run --rm -it --init --name "claude-docker-$ID" \
        --network host \
        --user "$(id -u):$(id -g)" \
        --cap-drop ALL --security-opt no-new-privileges --pids-limit 4096 \
        -v "$CD/work:/work" -v "$SDIR/in:/in:ro" -v "$SDIR/out:/out" -v "$CD/home:/home/dev" \
        -e MODE=interactive -e TASK_ID=claude-docker -e TASK_BRANCH="$(info branch)" \
        -e MODEL="$(info model)" -e LAUNCHER="$(info launcher)" -e OLLAMA_HOST=127.0.0.1:11434 \
        -e USER_GIT_NAME="$("${GIT[@]}" config user.name || true)" \
        -e USER_GIT_EMAIL="$("${GIT[@]}" config user.email || true)" \
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
    SDIR=$(session_dir "${1:-}"); ID="$1"
    R="$SDIR/out/result.json"
    [ -f "$R" ] || die "the session left no result.json"
    ERR=$(jq -r .error "$R"); [ -z "$ERR" ] || echo "CONTAINER ERROR: $ERR"
    BRANCH=$(info branch); BASE=$(info base)
    Q="refs/claude-docker/$ID"
    HEAD1="$BASE"
    if [ -f "$SDIR/out/out.bundle" ]; then
        "${GIT[@]}" bundle verify -q "$SDIR/out/out.bundle" > /dev/null 2>&1 || die "the bundle does not verify"
        "${GIT[@]}" fetch -q --no-tags "$SDIR/out/out.bundle" "+refs/heads/$BRANCH:$Q/commits" || die "cannot fetch the bundle"
        HEAD1=$("${GIT[@]}" rev-parse "$Q/commits")
    fi
    ENDED_ON=$(jq -r .head_branch "$R")
    PATCH="$SDIR/out/uncommitted.patch"
    ALL="$HEAD1"
    if [ -s "$PATCH" ]; then
        if [ "$ENDED_ON" != "$BRANCH" ]; then
            echo "NOTE: the session ended on branch '$ENDED_ON', not '$BRANCH': its uncommitted changes are not taken"
        else
            IDX=$(mktemp); rm -f "$IDX"
            GIT_INDEX_FILE="$IDX" "${GIT[@]}" read-tree "$HEAD1"
            GIT_INDEX_FILE="$IDX" "${GIT[@]}" apply --cached --binary "$PATCH" || die "the uncommitted changes do not apply to ${HEAD1:0:12}"
            TREE=$(GIT_INDEX_FILE="$IDX" "${GIT[@]}" write-tree); rm -f "$IDX"
            ALL=$("${GIT[@]}" commit-tree "$TREE" -p "$HEAD1" -m "claude-docker $ID: uncommitted changes")
        fi
    fi
    "${GIT[@]}" update-ref "$Q/all" "$ALL"
    DESCENDS=yes
    "${GIT[@]}" merge-base --is-ancestor "$BASE" "$HEAD1" || DESCENDS=no
    echo "COMMITS: $("${GIT[@]}" rev-list --count "$BASE..$HEAD1" 2>/dev/null || echo '?') new on $BRANCH (descends from the start: $DESCENDS)"
    echo "UNCOMMITTED: $(jq -r .uncommitted_files "$R") file(s)"
    "${GIT[@]}" diff --stat=100,60 "$BASE" "$ALL" | tail -n 25 | sed 's/^/  /'
    rc=0
    bash "$DEV_SCRIPTS/gate.sh" "$BASE" "$ALL" --out "$SDIR/gate.txt" > /dev/null || rc=$?
    head -n 1 "$SDIR/gate.txt"
    echo "GATE REPORT: $SDIR/gate.txt   (diff to read: git diff $BASE $Q/all -- <path>)"
    exit "$rc";;

apply)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    BRANCH=$(info branch); BASE=$(info base); Q="refs/claude-docker/$ID"
    "${GIT[@]}" rev-parse -q --verify "$Q/all" > /dev/null || die "run collect first"
    CUR_BRANCH=$("${GIT[@]}" symbolic-ref -q --short HEAD || echo claude-docker)
    [ "$CUR_BRANCH" = "$BRANCH" ] && [ "$("${GIT[@]}" rev-parse HEAD)" = "$BASE" ] \
        || { echo "CHANGED: this clone is no longer on $BRANCH at ${BASE:0:12}; nothing applied"; exit 4; }
    NOW=$(mktemp); snapshot_patch "$NOW"
    [ "$(sha256sum < "$NOW" | cut -c1-64)" = "$(info snapshot)" ] \
        || { rm -f "$NOW"; echo "CHANGED: the working tree changed since the session started; nothing applied"; exit 4; }
    rm -f "$NOW"
    HEAD1=$("${GIT[@]}" rev-parse -q --verify "$Q/commits" || echo "$BASE")
    "${GIT[@]}" merge-base --is-ancestor "$BASE" "$HEAD1" \
        || { echo "REWRITTEN: the session rewrote $BRANCH's history; nothing applied (see $Q/commits)"; exit 4; }
    cd "$DEV_REPO" || exit 1
    if [ -s "$SDIR/in/uncommitted.patch" ]; then
        git stash push -q --include-untracked -m "claude-docker $ID: the state before" \
            && echo "BACKUP: git stash list -> \"claude-docker $ID: the state before\""
    fi
    if [ "$HEAD1" != "$BASE" ]; then
        git merge -q --ff-only "$HEAD1" \
            || { echo "FAILED: cannot fast-forward $BRANCH to ${HEAD1:0:12}; the state before is in the stash"; exit 5; }
        echo "COMMITS: $BRANCH fast-forwarded to ${HEAD1:0:12}"
    fi
    PATCH="$SDIR/out/uncommitted.patch"
    if [ -s "$PATCH" ] && [ "$(jq -r .head_branch "$SDIR/out/result.json")" = "$BRANCH" ]; then
        git apply --binary --whitespace=nowarn "$PATCH" \
            || { echo "FAILED: the uncommitted changes do not apply; the state before is in the stash"; exit 5; }
        echo "UNCOMMITTED: applied to the working tree (all unstaged)"
    fi
    echo "APPLIED: session $ID";;

discard)
    SDIR=$(session_dir "${1:-}"); ID="$1"
    for r in commits all; do "${GIT[@]}" update-ref -d "refs/claude-docker/$ID/$r" 2>/dev/null || true; done
    rm -rf "$SDIR"
    echo "DISCARDED: session $ID";;

*)
    die "usage: claude_docker.sh prepare|open|run|wait|collect|apply|discard ...";;
esac

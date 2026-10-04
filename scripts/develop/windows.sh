#!/bin/bash
# Builds and tests Windows on the host -- the one thing the task container
# cannot do -- from a worktree on a Windows drive (cmd.exe cannot build from
# the Linux filesystem). Only for code whose review has cleared it: see
# .claude/develop/WORKFLOW.md, "Windows".
#
# Usage:
#   windows.sh prepare <task-id> | develop
#                         point the Windows worktree at the local branch
#                         task/<id> (it must equal or descend from
#                         origin/task/<id>), or at origin/develop (detached)
#   windows.sh build      release build (scripts/build_windows_on_wsl.sh); prints the errors
#   windows.sh test [<filter>]
#                         Windows unit tests (scripts/test_linux.sh W U); prints
#                         the summary and the failed tests
#   windows.sh path       prints the worktree's path
# The worktree is $HAISOS_DEVELOP_WINDOWS_DIR: by default "<repo>-windows"
# next to the repository when that is on a Windows drive, otherwise
# /mnt/c/haisos-develop-windows. It is kept between tasks, so Windows builds
# are incremental. Logs: $HAISOS_DEVELOP_HOME/windows-build.log, windows-test.log.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if [[ "$DEV_REPO" =~ ^/mnt/[a-z]/ ]]; then DEFAULT_WIN="$DEV_REPO-windows"; else DEFAULT_WIN=/mnt/c/haisos-develop-windows; fi
WIN="${HAISOS_DEVELOP_WINDOWS_DIR:-$DEFAULT_WIN}"
[[ "$WIN" =~ ^/mnt/[a-z]/ ]] || die "the Windows worktree must be on a Windows drive (/mnt/<drive>/...): $WIN"
GIT=(git -C "$DEV_REPO")
mkdir -p "$DEV_HOME"

CMD="${1:-}"
case "$CMD" in
    path)
        echo "$WIN";;
    prepare)
        TARGET="${2:-}"; [ -n "$TARGET" ] || die "usage: windows.sh prepare <task-id>|develop"
        "${GIT[@]}" fetch -q origin --prune
        if [ ! -e "$WIN/.git" ]; then
            "${GIT[@]}" worktree add -q --detach "$WIN" origin/develop || die "cannot create the worktree $WIN"
        fi
        if [ "$TARGET" = develop ]; then
            git -C "$WIN" switch -q -f --detach origin/develop
        else
            check_task_id "$TARGET"
            BR="task/$TARGET"
            if ! "${GIT[@]}" rev-parse -q --verify "refs/heads/$BR" > /dev/null; then
                "${GIT[@]}" branch -q "$BR" "origin/$BR" || die "no branch $BR, locally or on origin"
            fi
            if "${GIT[@]}" rev-parse -q --verify "origin/$BR" > /dev/null; then
                "${GIT[@]}" merge-base --is-ancestor "origin/$BR" "refs/heads/$BR" \
                    || die "local $BR does not descend from origin/$BR: git branch -f $BR origin/$BR first"
            fi
            git -C "$WIN" switch -q -f "$BR" || die "cannot switch the worktree to $BR (checked out in another worktree? git worktree list)"
        fi
        git -C "$WIN" clean -q -fd
        echo "WINDOWS WORKTREE: $WIN at $(git -C "$WIN" rev-parse --short HEAD) ($(git -C "$WIN" rev-parse --abbrev-ref HEAD))";;
    build)
        command -v cmd.exe > /dev/null || die "cmd.exe is not reachable (WSL interop is off?)"
        rc=0
        (cd "$WIN" && bash scripts/build_windows_on_wsl.sh) > "$DEV_HOME/windows-build.log" 2>&1 || rc=$?
        grep -E 'error C[0-9]+|fatal error|error LNK[0-9]+|LNK[0-9]+:|: error|CMake Error' "$DEV_HOME/windows-build.log" \
            | sed 's/\r$//' | awk '!seen[$0]++' | take 40
        if [ "$rc" = 0 ] && ! grep -qE 'error C[0-9]+|fatal error|error LNK' "$DEV_HOME/windows-build.log"; then
            echo "BUILD: OK"
        else
            echo "BUILD: FAILED (exit $rc; log: $DEV_HOME/windows-build.log)"; exit 1
        fi;;
    test)
        rc=0
        (cd "$WIN" && bash scripts/test_linux.sh W U ${2:+"$2"}) > "$DEV_HOME/windows-test.log" 2>&1 || rc=$?
        sed 's/\r$//' "$DEV_HOME/windows-test.log" | awk '/^Failed test outputs:/ {f = 1} f' | take 20
        SUMMARY=$(grep -Eo 'Tests summary: [0-9]+ passed, [0-9]+ failed' "$DEV_HOME/windows-test.log" | tail -n 1)
        grep -q '^No tests found' "$DEV_HOME/windows-test.log" && SUMMARY="No tests found"
        echo "TESTS: ${SUMMARY:-no summary} (exit $rc; log: $DEV_HOME/windows-test.log)"
        [ "$rc" = 0 ] && [ "$SUMMARY" != "No tests found" ] || exit 1;;
    *)
        die "usage: windows.sh prepare <task-id>|develop | build | test [<filter>] | path";;
esac

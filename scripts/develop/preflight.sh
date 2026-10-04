#!/bin/bash
# Checks what /develop-implement needs before it starts a task: GitHub, docker
# and the task image, ollama and the configured models, the develop branch.
#
# Usage: scripts/develop/preflight.sh
# One line per check, "OK ..." or "FAIL ..."; exit status 1 if any failed.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

FAILS=0
ok()   { echo "OK   $*"; }
fail() { echo "FAIL $*"; FAILS=$((FAILS + 1)); }

GIT=(git -C "$DEV_REPO")
"${GIT[@]}" fetch -q origin --prune || fail "git fetch origin"
if "${GIT[@]}" rev-parse -q --verify origin/develop > /dev/null; then
    ok "origin/develop at $("${GIT[@]}" rev-parse --short origin/develop), version $(develop_version)"
else
    fail "no origin/develop -- run /develop-create"
fi
[ "$("${GIT[@]}" rev-parse --abbrev-ref HEAD)" = develop ] && ok "on develop" || fail "this checkout is not on develop"
if gh auth status > /dev/null 2>&1; then ok "gh is logged in"; else fail "gh is not logged in (gh auth login)"; fi

if docker info > /dev/null 2>&1; then
    ok "docker is running"
    if TAG=$(bash "$DEV_SCRIPTS/image.sh" 2>/dev/null); then ok "task image $TAG"; else fail "task image does not build: see $DEV_HOME/image-build.log"; fi
else
    fail "docker is not running"
fi

if curl -fsS -m 5 http://127.0.0.1:11434/api/version > /dev/null 2>&1; then
    ok "ollama answers on 127.0.0.1:11434"
    IFS=',' read -r -a MODELS <<< "$(setting 'Task models' "$DEV_DEFAULT_MODEL" | tr -d ' ')"
    for m in "${MODELS[@]}"; do
        if curl -fsS -m 20 http://127.0.0.1:11434/api/show -d "{\"model\":\"$m\"}" 2>/dev/null | grep -q '"tools"'; then
            ok "model $m (tool calling)"
        else
            fail "model $m is not available with tool calling (ollama pull $m?)"
        fi
    done
else
    fail "ollama does not answer on 127.0.0.1:11434"
fi

mkdir -p "$DEV_HOME" && ok "workspace $DEV_HOME"
if [ -e "$DEV_HOME/lock" ] && ! flock -n "$DEV_HOME/lock" true 2>/dev/null; then
    fail "a task container is running now (lock held)"
fi
[ "$FAILS" = 0 ] || exit 1

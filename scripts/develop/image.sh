#!/bin/bash
# Builds the task container image, or reuses it when nothing that goes into it
# changed. The tag is a hash of the Dockerfile, the entrypoint, the prompt
# template, the host's ollama binary, the host's Claude Code version and the
# user ids; the image is also tagged haisos-develop:current.
#
# Usage: scripts/develop/image.sh [--force]
# Prints the image tag. The build log is kept in $HAISOS_DEVELOP_HOME/image-build.log.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

OLLAMA_BIN=$(command -v ollama) || die "ollama is not installed on the host (the container uses the host's ollama binary as its client)"
CLAUDE_VERSION=$(claude --version 2>/dev/null | awk '{print $1}') || true
[ -n "$CLAUDE_VERSION" ] || CLAUDE_VERSION=latest

CTX=$(mktemp -d)
trap 'rm -rf "$CTX"' EXIT
cp "$DEV_SCRIPTS/Dockerfile" "$DEV_SCRIPTS/entrypoint.sh" "$DEV_SCRIPTS/task_prompt.md" "$CTX/"
cp "$OLLAMA_BIN" "$CTX/ollama"

HASH=$( { cat "$CTX/Dockerfile" "$CTX/entrypoint.sh" "$CTX/task_prompt.md"
          sha256sum < "$CTX/ollama"
          echo "claude=$CLAUDE_VERSION uid=$(id -u) gid=$(id -g)"; } | sha256sum | cut -c1-12)
TAG="$DEV_IMAGE_REPO:$HASH"

if [ "$FORCE" = 0 ] && docker image inspect "$TAG" >/dev/null 2>&1; then
    docker tag "$TAG" "$DEV_IMAGE_REPO:current"
    echo "$TAG"
    exit 0
fi

mkdir -p "$DEV_HOME"
log "building $TAG (Claude Code $CLAUDE_VERSION); log: $DEV_HOME/image-build.log"
if ! docker build \
        --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" \
        --build-arg CLAUDE_CODE_VERSION="$CLAUDE_VERSION" \
        -t "$TAG" "$CTX" > "$DEV_HOME/image-build.log" 2>&1; then
    tail -n 30 "$DEV_HOME/image-build.log" >&2
    die "docker build failed; full log: $DEV_HOME/image-build.log"
fi
docker tag "$TAG" "$DEV_IMAGE_REPO:current"
echo "$TAG"

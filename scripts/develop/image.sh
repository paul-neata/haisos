#!/bin/bash
# Builds the task container's two images, or reuses them:
#
# - The base (Dockerfile.base): the tools. Its inputs are the Ubuntu release,
#   the host's Claude Code and ollama versions and Dockerfile.base itself; it
#   is rebuilt only when one of them changes, and named after the versions it
#   holds:
#     haisos-devtask-base:git2.43.0-cmake3.28.3-gcc13.3.0-node18.19.1-claude2.1.281-ollama0.21.0-ubuntu24.04-<file hash>
#   (git, cmake, gcc and node are what Ubuntu installs; they change only when
#   the base is rebuilt -- --refresh rebuilds it without the cache, taking in
#   Ubuntu's updates).
# - The top (Dockerfile): the user, the entrypoint and the prompt template, on
#   the base; seconds to build, tagged haisos-devtask:<hash of its inputs> and
#   haisos-devtask:current.
#
# Usage: scripts/develop/image.sh [--force] [--refresh]
#   --force    rebuild the top image
#   --refresh  rebuild the base without the cache (and the top on it)
# Prints the top image's tag. Build logs: $HAISOS_DEVELOP_HOME/image-build*.log.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

FORCE=0 REFRESH=0
for a in "$@"; do
    case "$a" in
        --force) FORCE=1;;
        --refresh) REFRESH=1; FORCE=1;;
        *) die "unknown option '$a'";;
    esac
done

OLLAMA_BIN=$(command -v ollama) || die "ollama is not installed on the host (the container uses the host's ollama binary as its client)"
OLLAMA_VERSION=$("$OLLAMA_BIN" --version 2>/dev/null | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+[^ ]*' | tail -n 1) || true
[ -n "$OLLAMA_VERSION" ] || die "cannot read the ollama version (ollama --version)"
CLAUDE_VERSION=$(claude --version 2>/dev/null | awk '{print $1}') || true
[ -n "$CLAUDE_VERSION" ] || die "cannot read the Claude Code version (claude --version)"
UBUNTU=$(sed -n 's/^FROM ubuntu:\([0-9.]*\).*/\1/p' "$DEV_SCRIPTS/Dockerfile.base" | take 1)
[ -n "$UBUNTU" ] || die "Dockerfile.base does not start FROM ubuntu:<release>"
mkdir -p "$DEV_HOME"

# --- The base ----------------------------------------------------------------
FILE_HASH=$(sha256sum < "$DEV_SCRIPTS/Dockerfile.base" | cut -c1-8)
KEY="ubuntu$UBUNTU-claude$CLAUDE_VERSION-ollama$OLLAMA_VERSION-$FILE_HASH"
BASE=""
[ "$REFRESH" = 1 ] || BASE=$(docker images --filter "label=haisos.devtask.key=$KEY" \
    --format '{{.Repository}}:{{.Tag}}' | grep "^$DEV_BASE_IMAGE_REPO:" | take 1 || true)

if [ -z "$BASE" ]; then
    CTX=$(mktemp -d)
    trap 'rm -rf "$CTX"' EXIT
    cp "$DEV_SCRIPTS/Dockerfile.base" "$CTX/Dockerfile"
    cp "$OLLAMA_BIN" "$CTX/ollama"
    TMP_TAG="$DEV_BASE_IMAGE_REPO:building"
    NOCACHE=()
    [ "$REFRESH" = 0 ] || NOCACHE=(--pull --no-cache)
    log "building the base image (Claude Code $CLAUDE_VERSION, ollama $OLLAMA_VERSION); log: $DEV_HOME/image-build-base.log"
    if ! docker build "${NOCACHE[@]}" \
            --build-arg CLAUDE_CODE_VERSION="$CLAUDE_VERSION" \
            --label "haisos.devtask.key=$KEY" \
            -t "$TMP_TAG" "$CTX" > "$DEV_HOME/image-build-base.log" 2>&1; then
        tail -n 30 "$DEV_HOME/image-build-base.log" >&2
        die "docker build of the base failed; full log: $DEV_HOME/image-build-base.log"
    fi
    VERSIONS=$(docker run --rm --entrypoint bash "$TMP_TAG" -c '
        git --version | grep -Eo "[0-9][0-9.]*" | head -n 1
        cmake --version | grep -Eo "[0-9][0-9.]*" | head -n 1
        g++ -dumpfullversion
        node --version | tr -d v' | tr '\n' ' ') || die "cannot read the versions in the base image"
    read -r GIT_V CMAKE_V GCC_V NODE_V <<< "$VERSIONS"
    BASE="$DEV_BASE_IMAGE_REPO:git$GIT_V-cmake$CMAKE_V-gcc$GCC_V-node$NODE_V-claude$CLAUDE_VERSION-ollama$OLLAMA_VERSION-ubuntu$UBUNTU-$FILE_HASH"
    BASE="${BASE//[^A-Za-z0-9_.:-]/_}"
    docker tag "$TMP_TAG" "$BASE"
    docker rmi "$TMP_TAG" > /dev/null 2>&1 || true
    log "base image: $BASE"
fi

# --- The top -----------------------------------------------------------------
BASE_ID=$(docker image inspect --format '{{.Id}}' "$BASE")
HASH=$( { echo "base=$BASE_ID uid=$(id -u) gid=$(id -g)"
          cat "$DEV_SCRIPTS/Dockerfile" "$DEV_SCRIPTS/entrypoint.sh" "$DEV_SCRIPTS/task_prompt.md"; } | sha256sum | cut -c1-12)
TAG="$DEV_IMAGE_REPO:$HASH"

if [ "$FORCE" = 0 ] && docker image inspect "$TAG" > /dev/null 2>&1; then
    docker tag "$TAG" "$DEV_IMAGE_REPO:current"
    echo "$TAG"
    exit 0
fi

CTX2=$(mktemp -d)
trap 'rm -rf "${CTX:-}" "$CTX2"' EXIT
cp "$DEV_SCRIPTS/Dockerfile" "$DEV_SCRIPTS/entrypoint.sh" "$DEV_SCRIPTS/task_prompt.md" "$CTX2/"
log "building $TAG on $BASE; log: $DEV_HOME/image-build.log"
if ! docker build \
        --build-arg BASE_IMAGE="$BASE" \
        --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" \
        -t "$TAG" "$CTX2" > "$DEV_HOME/image-build.log" 2>&1; then
    tail -n 30 "$DEV_HOME/image-build.log" >&2
    die "docker build failed; full log: $DEV_HOME/image-build.log"
fi
docker tag "$TAG" "$DEV_IMAGE_REPO:current"
echo "$TAG"

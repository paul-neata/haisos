#!/bin/bash
set -e

CACHE_DB="tests/tool/llm_cache_proxy_database"
PROXY_LOG="proxy.log"
PROXY_PORT=11435

# Always run unit tests
echo "Running unit tests..."
./scripts/test_linux.sh L U

# Check if cache database has recordings
HAS_RECORDINGS=0
if [ -d "$CACHE_DB" ] && [ "$(ls -A "$CACHE_DB"/*.response.json 2>/dev/null | wc -l)" -gt 0 ]; then
    HAS_RECORDINGS=1
fi

# Run integration and haisos tests only if recordings exist
if [ "$HAS_RECORDINGS" -eq 1 ]; then
    echo "Cache database found. Starting LLM cache proxy in serve mode..."
    node tests/tools/llm_cache_proxy/src/index.js \
        --serve --folder "$CACHE_DB" --port "$PROXY_PORT" --log-file "$PROXY_LOG" &
    PROXY_PID=$!

    # Wait for proxy to be ready
    for i in {1..30}; do
        if curl -s http://localhost:${PROXY_PORT}/ >/dev/null 2>&1; then
            echo "Proxy ready"
            break
        fi
        sleep 1
    done

    export HAISOS_ENDPOINT="http://localhost:${PROXY_PORT}/api/chat"

    echo "Running integration tests..."
    ./scripts/test_linux.sh L I

    echo "Running haisos tests..."
    ./scripts/test_linux.sh L H

    # Stop proxy
    if [ -n "$PROXY_PID" ]; then
        kill $PROXY_PID 2>/dev/null || true
        wait $PROXY_PID 2>/dev/null || true
    fi
else
    echo "No cache database recordings found. Skipping integration and haisos tests."
fi

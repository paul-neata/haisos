# llm_cache_proxy

A Node.js HTTP proxy for LLM API traffic used by the Haisos test suite.

## Purpose

`llm_cache_proxy` sits between Haisos (or any HTTP client) and a real LLM provider (e.g., Ollama). It enables three workflows:

1. **Transparent proxy** — verify the proxy layer works.
2. **Record** — forward requests to the real provider and save every request/response pair to disk.
3. **Serve from cache** — replay previously-recorded responses without hitting the real provider.

By recording real LLM interactions once and serving them in CI, integration and haisos tests can run offline and deterministically.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `HAISOS_ENDPOINT` | **Upstream** LLM API URL (e.g., `http://localhost:11434/api/chat`) |
| `HAISOS_MODEL` | Model name (printed for reference, forwarded in request body) |
| `HAISOS_API_KEY` | API key (printed as `(present)` or `(not set)`, forwarded in headers) |

## Modes

### `--proxy`

Forwards every HTTP request to the real endpoint and returns the response unmodified. Use this to verify the proxy itself works end-to-end.

```bash
node src/index.js --proxy --port 11435 --log-file proxy.log
```

### `--record --folder <dir>`

Forwards requests to the real endpoint **and** persists every request/response pair under `<dir>`.

Each pair is stored as two files named by the SHA-256 hash of the request body:
- `<hash>.request.json`
- `<hash>.response.json`

```bash
node src/index.js --record --folder ./recordings --port 11435 --log-file record.log
```

### `--serve --folder <dir>`

Replays previously-recorded responses from `<dir>`. If a request hash is not found, returns HTTP 404 and logs the miss.

```bash
node src/index.js --serve --folder ./recordings --port 11435 --log-file serve.log
```

## Client Configuration

When the proxy starts it prints the **new** values clients should use:

```
Proxy listening on http://localhost:11435
New HAISOS_ENDPOINT=http://localhost:11435/api/chat
New HAISOS_MODEL=llama3
New HAISOS_API_KEY=(present)
```

Point Haisos (or CI secrets) at the printed `HAISOS_ENDPOINT` to route traffic through the proxy.

## Logging

`--log-file <path>` is **mandatory**. Every proxy event (startup, request, response, errors, cache hits/misses) is written with an ISO-8601 timestamp.

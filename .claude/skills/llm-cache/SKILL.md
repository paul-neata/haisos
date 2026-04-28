---
name: llm-cache
description: Manage the LLM cache database for Haisos integration and haisos tests
---

Manage the LLM cache proxy database used to run integration and haisos tests offline.

**Arguments:** `<command> [action|subcommand]`

- **command**: One of:
  - `clear` — Delete all cached recordings from `tests/tool/llm_cache_proxy_database/`
  - `record` — Manage the LLM cache proxy in record mode
  - `serve` — Manage the LLM cache proxy in serve mode
  - `record_command` — Start proxy in record mode, run a shell command, then stop recording
  - `serve_command` — Start proxy in serve mode, run a shell command, then stop serving

- **lifecycle subcommands for `record` and `serve`:**
  - `start` — Start the proxy (detached). Paired with `stop`.
  - `stop` — Kill the running proxy process tracked in `/tmp/haisos_llm_cache_proxy.pid`

- **test actions for `record` and `serve`:** Start the proxy, run tests, then stop the proxy automatically:
  - `integration-tests` — Run integration tests against the proxy
  - `haisos-tests` — Run haisos tests against the proxy
  - `tests` — Shorthand for both `integration-tests` and `haisos-tests`

**Default behavior:**
- `clear` requires no additional arguments.
- `record start` and `serve start` start the proxy without running tests.

**Environment variables used:**
- `HAISOS_ENDPOINT` — Upstream LLM API URL (e.g., `http://localhost:11434/api/chat`)
- `HAISOS_MODEL` — Model name forwarded in requests
- `HAISOS_API_KEY` — API key forwarded in headers

**Cache database location:** `tests/tool/llm_cache_proxy_database/`

**Proxy default port:** `11435`

**Proxy log file:** `tests/tool/llm_cache_proxy_database/proxy.log`

**Process tracking:** The PID of the started proxy is stored in `/tmp/haisos_llm_cache_proxy.pid` so `stop` can terminate it reliably.

---

Examples:

Lifecycle:
- `/llm-cache record start` — Start proxy in record mode (runs until `stop`)
- `/llm-cache record stop` — Kill the running record proxy
- `/llm-cache serve start` — Start proxy in serve mode (runs until `stop`)
- `/llm-cache serve stop` — Kill the running serve proxy

Record with tests (start, run, stop automatically):
- `/llm-cache record integration-tests` — Record while running integration tests
- `/llm-cache record haisos-tests` — Record while running haisos tests
- `/llm-cache record tests` — Record while running both integration and haisos tests

Serve with tests (start, run, stop automatically):
- `/llm-cache serve integration-tests` — Serve cache while running integration tests
- `/llm-cache serve haisos-tests` — Serve cache while running haisos tests
- `/llm-cache serve tests` — Serve cache while running both test types

Command wrappers (start proxy, run command, stop proxy automatically):
- `/llm-cache record_command ./scripts/run_tests_linux.sh L I` — Record cache, run command, then stop
- `/llm-cache serve_command ./scripts/run_tests_linux.sh L I` — Serve cache, run command, then stop

Other:
- `/llm-cache clear` — Delete all cached recordings

---

**Implementation notes:**
- Always construct and execute the `node tests/tools/llm_cache_proxy/src/index.js` command on a single line.
- For `record_command`, `serve_command`, and test actions (`integration-tests`, `haisos-tests`, `tests`): start the proxy, prepend `HAISOS_ENDPOINT=http://localhost:11435/api/chat` to the command environment so it routes through the proxy, run the command/tests, then kill the proxy PID when finished.
- For lifecycle subcommands (`start`/`stop`), store the proxy PID in `/tmp/haisos_llm_cache_proxy.pid` on start and read it back on stop.

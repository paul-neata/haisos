# Explore tool schema across protocols

Each tool declares its parameters as JSON via `ITool::GetParametersSchema()`
(`interfaces/ILLMService.h`). Today there is exactly one place that decides what shape that
schema reaches the wire in: `LLMCommunicator` builds the request in the OpenAI/Ollama
tools format -- `{"type": "function", "function": {"name", "description", "parameters"}}`
(`src/components/LLMCommunicator/LLMCommunicator.cpp`). The *response* side is already
multi-format (Ollama `message.tool_calls[]`, Anthropic `content` blocks of type
`tool_use`, a direct format), but the request side speaks one protocol only.

Idea to explore: what it takes to send the same tools over other protocols:

- **Schema dialects differ**: OpenAI's function calling accepts a JSON-schema *subset*
  (strict mode rejects e.g. `oneOf`, arbitrary `additionalProperties`), Anthropic wants the
  same fields as `input_schema`, Gemini renames to `parameters` with its own subset. Do we
  restrict what `GetParametersSchema()` may express (and where is that enforced -- a test
  over every registered tool?), or convert per protocol at send time?
- **Where conversion lives**: presumably `LLMCommunicator`, selected per endpoint or per
  configured protocol -- how is the protocol chosen (`HAISOS_ENDPOINT` convention? an env
  var? sniffing?), and how does that interact with the llm_cache_proxy recordings used by
  the integration/haisos tests?
- **The xdiff log**: `tools` entries in `--log-agent-to-file` currently print name +
  description; if schemas are converted per protocol, what does the log show -- the
  canonical schema or the wire one?
- **Which protocols matter**: Ollama (current default), OpenAI-compatible, Anthropic --
  check what the available-skills' `claude-api` reference and
  `code-review-llm-protocol` skill already assume.

Seed for `/explore` or `/todo`.

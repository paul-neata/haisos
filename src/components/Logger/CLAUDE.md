# Logger

Thread-safe logging with configurable receivers.

## Responsibilities

- Provides log macros for different severity levels
- Supports registering multiple message receivers
- Formats log messages with file, line, and timestamp
- Thread-safe log output
- Reports agent LLM traffic (`LogAgentSend`/`LogAgentReceive`) to one callback
  per direction, apart from the leveled log

## Key Components

- `LogImpl` - Core logging function
- `LogRegisterMessageReceiver` / `LogUnregisterMessageReceiver` - Receiver management
- Convenience macros: `LogError`, `LogWarning`, `LogInfo`, `LogTrace`, `LogDebug`, `LogVerboseDebug`
- `LogAgentSend` / `LogAgentReceive` - report each JSON request an agent sends
  to its LLM, and each response (or failure) it gets back, with the agent's
  path: its ancestors' names, top-most first, then its own (so the path's
  length minus one is the agent's depth). `LLMCommunicator::Call` is the one
  caller; the path is the communicator's `agentPath`, which `LLMService`
  builds from the agent's parent chain when it creates the agent.
- `FormatAgentPath` - an agent path as text, names joined by `>`.
- `RegisterLogAgentSendCallback` / `RegisterLogAgentReceiveCallback` - set (or,
  with `nullptr`, clear) the one callback per direction. With none set, a report
  costs an atomic check. Callbacks run on the reporting agent's thread,
  concurrently, so must be thread-safe. `haisos --log-agent-to-file` registers
  them (see `AgentTrafficLog` in `src/haisos/`).

## Notes

- Agent traffic is deliberately not a leveled message: it carries whole JSON
  documents and should reach a file of its own without turning on
  `verbose_debug` for everything else. (The `[JSON_REQUEST]`/`[JSON_RESPONSE]`
  `VerboseDebug` lines, used by `--log-json-in-temp`, are still logged too.)

# Logger

Thread-safe logging with configurable receivers.

## Responsibilities

- Provides log macros for different severity levels
- Supports registering multiple message receivers
- Formats log messages with file, line, and timestamp
- Thread-safe log output

## Key Components

- `LogImpl` - Core logging function
- `LogRegisterMessageReceiver` / `LogUnregisterMessageReceiver` - Receiver management
- Convenience macros: `LogError`, `LogWarning`, `LogInfo`, `LogTrace`, `LogDebug`, `LogVerboseDebug`

# LLMCommunicator

Handles LLM API communication, request/response formatting, and tool call parsing.

## Responsibilities

- Builds JSON request payloads for the LLM API
- Sends HTTP requests via `IHTTPClient`
- Parses LLM responses including text content and tool calls
- Supports tool schema registration so the LLM knows available functions

## Key Classes

- `LLMCommunicator` - Main implementation of `ILLMCommunicator`

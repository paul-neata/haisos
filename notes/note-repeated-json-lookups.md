# Repeated JSON lookups: a[b][c], then a[b] again and again

The LLM response parsing (currently in `LLMCommunicator.cpp`, in the
LLMCommunicator component) walks the nlohmann::json response by looking the
same keys up again and again. It checks
`parsed.contains("message") && parsed["message"].contains("role")`, then reads
`parsed["message"]["role"]`, then goes through `parsed["message"]` once more
for `content`, `thinking` and `tool_calls`: "message" is searched about ten
times, and a tool call's "id" three times in one condition. Every `contains`
and `operator[]` on an object searches its keys.

Besides the waste:

- `operator[]` on a non-const json inserts a null member when the key is
  missing. Only the `contains` in front keeps these lookups from changing the
  document.
- Values are converted without a type check
  (`response.message.role = parsed["message"]["role"]`). A `null` or a number
  there throws, and the catch around the whole parse turns the entire response
  into "Error: Failed to parse LLM response"
  (`notes/note-review-low-findings.md`, section 5: one null field discards the
  whole response).

To do: look each node up once, with `find()`, keep the iterator (or a
reference), and check its type before reading it -- as `Agent` already does
when it reads tool calls (`toolCall.find("function")`). A small helper returning
the child or null (`const json* Child(const json& node, const char* key)`)
keeps it short; JSON pointers (`value(json::json_pointer("/message/role"), ...)`)
do too, but `value()` still throws when the member has another type. Checked
field by field, one bad field falls back to its default instead of discarding
the response.

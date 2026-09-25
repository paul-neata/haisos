# self_close

Closes the calling agent: it stops taking commands and finishes once the
current turn is over. It is how an **interactive** agent (see
`StartProcessOptions::interactiveAgent` and `RUN -i` in the haisosfile) ends
its own session -- such an agent otherwise keeps waiting for the next line
typed on its console forever.

Calling it is `IAgent::TriggerStop()` on the caller, from the caller's own
thread, so the tool cannot (and does not) wait for the agent to finish. Any
further tool calls in the same LLM response are refused rather than run, and
the conversation round ends without another LLM call. For an interactive
process, the process itself finishes once the input loop notices, which is when
the next line arrives (see `AgentInputLoop`).

Any agent may call it, a subagent included; a non-interactive one simply ends
early.

## Arguments

None.

## Output format

On success, returns a short confirmation string. On error (no calling agent),
sets `is_error=true`.

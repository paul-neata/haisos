# Environment

The in-memory `IEnvironment`: what an OS or a process is given to run with.
Created through `IFactory::CreateEnvironment()`, duplicated with `Clone()`.

## Responsibilities

- Holds three independent sets of named entries, each with the same operations
  (`Add` fails if the name is taken, `Set` overrides, `Remove`, `Has`,
  enumerate names in no particular order, and `Get` where reading is allowed):
  - **variables** - plain key/value strings, populated from the haisosfile's
    `ENV` directives. The LLM configuration lives here, under
    `HAISOS_ENDPOINT` / `HAISOS_MODEL` / `HAISOS_API_KEY`.
  - **secrets** - credentials that live outside the OS (passwords, API
    tokens). There is deliberately no `GetSecret()`: code inside the OS names
    a secret, it never reads one. `AddSecretFrom`/`SetSecretFrom` copy one out
    of another environment, so a secret can be handed on without anyone in
    between learning its value; they are the only callers of the protected
    `ReadSecretValue()`, which is what keeps that guarantee.
  - **LLM identifiers** - endpoint, model name, and either an API token or the
    name of a secret holding it (`LLMIdentifier`). Model options (effort,
    reasoning, ...) will be added to that struct.
- Nothing is inherited implicitly. An environment is passed explicitly to
  `IFactory::CreateHaisosOS`, to `IHaisosOS::StartProcess` and to
  `IHaisosOS::CreateSubOS`; the usual thing to pass is
  `GetOsEnvironment()->Clone()`, after which the two are independent.
- Every accessor is mutex-guarded: one environment is shared by an OS and
  whatever it hands it to, and those run on different threads.

## Key Classes

- `Environment` - Main implementation of `IEnvironment`
- `CreateEnvironment()` - Free function creating an empty environment

# Explore release testing workflow

What exists today, scattered: CI (`.github/workflows/ci.yml`) builds+tests Linux and
Windows on every push and packs a `haisos.<branch>.<version>.<min>.<hash>.zip`
(`scripts/pack.sh`); locally, `scripts/test_linux.sh L|W|N U|I|H|* [--debug|--both|--smoke]`
runs test selections incl. the smoke list (`scripts/internal/smoke_tests.txt`), and
`test_windows.bat` does the same on Windows. WASM is **not** in CI. There is no written
answer to "what must pass before this is a release".

Idea to explore: a defined release testing workflow. Open questions:

- **Coverage matrix**: which platforms x configurations x test types must pass -- e.g.
  Linux+Windows all tests release (CI already), plus WASM (`N`) and `--both`
  (release+debug) run locally before a release? Where is that recorded -- a checklist in
  CLAUDE.md, a script, a CI "release" job?
- **LLM dependence**: integration/haisos tests lean on the llm_cache_proxy recordings; do
  they run in CI as-is, and does a release need any run against a live Ollama, or is
  recorded traffic enough?
- **Version gate**: release means `HAISOS_VERSION` has been bumped one minor over the
  base (the `/rebase` skill does this on version conflicts) -- should a workflow step
  verify the version is ahead of master's and matches the tag being cut?
- **Tag/release mechanics**: is a release just a merge to master + tag, or a GitHub
  Release built from `pack.sh`'s zip? Who runs `pack.sh` -- CI on tags only?
- **Smoke the artifact**: after packing, unpack the zip and run `haisos --init` /
  `haisos --version` / a demo haisosfile on each OS -- scripted or manual?
- **Where WASM fits**: build+test via `build_wasm_on_linux.sh` + test.js locally, or add
  an emscripten CI job?

Seed for `/explore` or `/todo`.

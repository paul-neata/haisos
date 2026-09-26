# Explore faster unit test runs

Unit tests are per-component gtest binaries (`<name>.unittests` under
`tests/unit/components|tools|haisos`), discovered from the output dir by
`scripts/internal/test.js` (`discoverUnitTests`). The runner then loops
`for (const test of allTests) { await runTest(...) }` -- **strictly sequential**, one
binary at a time, each with its own output file in a temp dir.

Idea to explore: make this faster. Directions, most promising first:

- **Run binaries in parallel**: the binary-level granularity is already perfect
  (independent executables, separate output files, per-test timeouts) -- swap the serial
  loop for a Promise pool of size N. First check for shared state that would break:
  fixed ports/named pipes, shared temp dirs, the llm_cache_proxy (probably
  integration/haisos-only), tests writing to the same log path. Maybe parallelize `U`
  only and keep `I`/`H` serial if they share infrastructure.
- **Measure first**: which binaries dominate? Suspects are HaisosOS/Agent tests with real
  sleeps and 5 s stop timeouts -- a few slow tests inside one binary won't be helped by
  binary-level parallelism; gtest has no in-binary parallelism, only `--gtest_filter`
  sharding.
- **WSL I/O**: the repo lives on `/mnt/c` (DrvFs); process spawn and file I/O are notably
  slower than in native Linux filesystem. Worth timing the same suite from a WSL-native
  checkout vs /mnt/c before optimizing the runner.
- **Build vs run**: if build time dominates, that's a different problem (incremental
  builds, ccache, unity builds) -- measure the split first.
- **Alternative runner**: tests are also registered with ctest (`cd build/temp_linux &&
  ctest`) -- `ctest -j` parallelizes out of the box, but loses test.js's per-test output
  files/timeouts/filter/smoke logic; extending test.js keeps one entry point.

Seed for `/explore` or `/todo`.

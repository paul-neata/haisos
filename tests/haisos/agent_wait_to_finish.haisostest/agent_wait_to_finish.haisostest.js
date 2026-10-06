#!/usr/bin/env node

const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'What is 3+3?' using agent_start without wait. Then use agent_wait_to_finish with timeout 30000. Return the result.";

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'agent_wait_to_finish-haisostest-'));
fs.writeFileSync(path.join(tmpDir, 'agent.md'), prompt);
// The OS environment is not inherited from the host: HAISOS_* must be
// imported explicitly with ENV, or the agent falls back to the defaults and
// this test silently exercises nothing.
fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
    "ENV HAISOS_ENDPOINT\nENV HAISOS_MODEL\nENV HAISOS_API_KEY\nROOT .\nRUN /agent.md\n");

try {
    // spawnSync, not execSync: an agent's failures now go to stderr, which
    // execSync never returns. (The exit status is part of streams--exit-codes.)
    const result = spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 120000, cwd: tmpDir });
    if (result.error) {
        console.error("agent_wait_to_finish haisos test failed:", result.error.message);
        process.exit(1);
    }
    console.log(result.stdout);
    // haisos exits 0 even when the agent itself fails, so assert on the output
    // -- both streams now.
    if (/Error:/.test(result.stdout) || /Error:/.test(result.stderr)) {
        console.error("agent_wait_to_finish haisos test failed: the agent reported an error");
        process.exit(1);
    }
    console.log("agent_wait_to_finish haisos test passed");
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

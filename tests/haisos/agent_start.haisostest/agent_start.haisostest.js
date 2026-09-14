#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Use agent_start with user_prompt 'What is 2+2?' and wait_to_finish true. Return the result.";

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'agent_start-haisostest-'));
fs.writeFileSync(path.join(tmpDir, 'agent.md'), prompt);
// The OS environment is not inherited from the host: HAISOS_* must be
// imported explicitly with ENV, or the agent falls back to the defaults and
// this test silently exercises nothing.
fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
    "ENV HAISOS_ENDPOINT\nENV HAISOS_MODEL\nENV HAISOS_API_KEY\nROOT .\nRUN agent.md\n");

try {
    const result = execSync(`${haisosPath} haisosfile`, { encoding: 'utf8', timeout: 120000, cwd: tmpDir });
    console.log(result);
    // haisos exits 0 even when the agent itself fails, so assert on the output.
    if (/Error:/.test(result)) {
        console.error("agent_start haisos test failed: the agent reported an error");
        process.exit(1);
    }
    console.log("agent_start haisos test passed");
} catch (e) {
    console.error("agent_start haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

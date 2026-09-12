#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'What is 3+3?' using agent_start without wait. Then use agent_wait_to_finish with timeout 30000. Return the result.";

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'agent_wait_to_finish-haisostest-'));
fs.writeFileSync(path.join(tmpDir, 'agent.md'), prompt);
fs.writeFileSync(path.join(tmpDir, 'haisosfile'), "ROOT .\nRUN agent.md\n");

try {
    const result = execSync(`${haisosPath} haisosfile`, { encoding: 'utf8', timeout: 120000, cwd: tmpDir });
    console.log(result);
    console.log("agent_wait_to_finish haisos test passed");
} catch (e) {
    console.error("agent_wait_to_finish haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

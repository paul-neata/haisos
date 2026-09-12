#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'What is 5+5?' using agent_start without wait. Then use agent_list_running to list agents. Then stop it. Return the result.";

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'agent_list_running-haisostest-'));
fs.writeFileSync(path.join(tmpDir, 'agent.md'), prompt);
fs.writeFileSync(path.join(tmpDir, 'haisosfile'), "ROOT .\nRUN agent.md\n");

try {
    const result = execSync(`${haisosPath} haisosfile`, { encoding: 'utf8', timeout: 120000, cwd: tmpDir });
    console.log(result);
    console.log("agent_list_running haisos test passed");
} catch (e) {
    console.error("agent_list_running haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

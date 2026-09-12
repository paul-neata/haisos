#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Use agent_start with user_prompt 'What is 2+2?' and wait_to_finish true. Return the result.";

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'agent_start-haisostest-'));
fs.writeFileSync(path.join(tmpDir, 'agent.md'), prompt);
fs.writeFileSync(path.join(tmpDir, 'haisosfile'), "ROOT .\nRUN agent.md\n");

try {
    const result = execSync(`${haisosPath} haisosfile`, { encoding: 'utf8', timeout: 120000, cwd: tmpDir });
    console.log(result);
    console.log("agent_start haisos test passed");
} catch (e) {
    console.error("agent_start haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

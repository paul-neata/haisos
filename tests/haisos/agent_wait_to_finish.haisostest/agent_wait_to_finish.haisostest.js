#!/usr/bin/env node

const { execSync } = require('child_process');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'What is 3+3?' using agent_start without wait. Then use agent_wait_to_finish with timeout 30000. Return the result.";

try {
    const result = execSync(`${haisosPath} --prompt "${prompt}"`, { encoding: 'utf8', timeout: 120000 });
    console.log(result);
    console.log("agent_wait_to_finish haisos test passed");
} catch (e) {
    console.error("agent_wait_to_finish haisos test failed:", e.message);
    process.exit(1);
}

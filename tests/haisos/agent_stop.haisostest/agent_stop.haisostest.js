#!/usr/bin/env node

const { execSync } = require('child_process');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'Count to 100 slowly' using agent_start without wait. Then use agent_stop to stop it. Return the result.";

try {
    const result = execSync(`${haisosPath} --prompt "${prompt}"`, { encoding: 'utf8', timeout: 120000 });
    console.log(result);
    console.log("agent_stop haisos test passed");
} catch (e) {
    console.error("agent_stop haisos test failed:", e.message);
    process.exit(1);
}

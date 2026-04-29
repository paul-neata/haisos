#!/usr/bin/env node

const { execSync } = require('child_process');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'What is 4+4?' using agent_start without wait. Then use agent_query to check its status. Return the result.";

try {
    const result = execSync(`${haisosPath} --prompt "${prompt}"`, { encoding: 'utf8', timeout: 120000 });
    console.log(result);
    console.log("agent_query haisos test passed");
} catch (e) {
    console.error("agent_query haisos test failed:", e.message);
    process.exit(1);
}

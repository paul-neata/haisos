#!/usr/bin/env node

const { execSync } = require('child_process');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Start a subagent with prompt 'What is 5+5?' using agent_start without wait. Then use agent_list_running to list agents. Then stop it. Return the result.";

try {
    const result = execSync(`${haisosPath} --prompt "${prompt}"`, { encoding: 'utf8', timeout: 60000 });
    console.log(result);
    console.log("agent_list_running haisos test passed");
} catch (e) {
    console.error("agent_list_running haisos test failed:", e.message);
    process.exit(1);
}

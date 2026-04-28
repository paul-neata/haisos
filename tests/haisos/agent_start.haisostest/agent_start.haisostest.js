#!/usr/bin/env node

const { execSync } = require('child_process');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');
const prompt = "Use agent_start with user_prompt 'What is 2+2?' and wait_to_finish true. Return the result.";

try {
    const result = execSync(`${haisosPath} --prompt "${prompt}"`, { encoding: 'utf8', timeout: 60000 });
    console.log(result);
    console.log("agent_start haisos test passed");
} catch (e) {
    console.error("agent_start haisos test failed:", e.message);
    process.exit(1);
}

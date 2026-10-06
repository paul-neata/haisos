#!/usr/bin/env node

// Exit codes, end to end: haisos's exit status is the exit code of the first
// RUN (in file order) that did not exit 0 -- and 127 for a RUN that never
// started. No LLM is involved, so this needs no endpoint.

const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'exit_codes-haisostest-'));

function runHaisos(runLines) {
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
        "FS rootfs MEM\n" +
        "ROOT rootfs\n" +
        "CREATE_DIR /bin\n" +
        "BUILTIN rootfs echo /bin/echo\n" +
        "BUILTIN rootfs ls /bin/ls\n" +
        "CREATE /p.lua multiline END\n" +
        'print("to stdout")\n' +
        'error("boom")\n' +
        "END\n" +
        "CREATE /e.lua multiline END\n" +
        "exit(3)\n" +
        "END\n" +
        runLines);
    return spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
}

function expect(actual, expected, what) {
    if (actual !== expected) {
        throw new Error(`${what}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
}

try {
    // A failing builtin: ls's status 2 is haisos's.
    let r = runHaisos("RUN /bin/ls /nope\n");
    expect(r.status, 2, "ls /nope status");
    expect(r.signal, null, "ls /nope signal");
    expect(r.stdout, "", "ls /nope stdout");
    if (!r.stderr.includes("ls: cannot access '/nope':")) {
        throw new Error(`ls /nope stderr: ${r.stderr}`);
    }

    // A Lua script that fails: prints, then errors -- status 1, lua-style stderr.
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
        "FS rootfs MEM\nROOT rootfs\n" +
        "CREATE /p.lua multiline END\n" +
        'print("to stdout")\n' +
        'error("boom")\n' +
        "END\n" +
        "RUN /p.lua\n");
    r = spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
    expect(r.status, 1, "p.lua status");
    expect(r.stdout, "to stdout\n", "p.lua stdout");
    expect(r.stderr, "lua: /p.lua:2: boom\n", "p.lua stderr");

    // exit(3): the code is the script's.
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
        "FS rootfs MEM\nROOT rootfs\n" +
        "CREATE /e.lua multiline END\nexit(3)\nEND\n" +
        "RUN /e.lua\n");
    r = spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
    expect(r.status, 3, "e.lua status");
    expect(r.stdout, "", "e.lua stdout");

    // A RUN that never starts: 127.
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
        "FS rootfs MEM\nROOT rootfs\nRUN /nope.lua\n");
    r = spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
    expect(r.status, 127, "missing RUN status");
    if (!r.stderr.includes("Error: Failed to start process: /nope.lua")) {
        throw new Error(`missing RUN stderr: ${r.stderr}`);
    }

    // First non-zero in file order wins: echo exits 0, e.lua 3.
    r = runHaisos("RUN /bin/echo ok\nRUN /e.lua\n");
    expect(r.status, 3, "two RUNs status");
    if (!r.stdout.includes("ok\n")) {
        throw new Error(`two RUNs stdout: ${r.stdout}`);
    }

    // A run where everything exits 0.
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
        "FS rootfs MEM\nROOT rootfs\nCREATE_DIR /bin\nBUILTIN rootfs echo /bin/echo\nRUN /bin/echo ok\n");
    r = spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
    expect(r.status, 0, "all-ok status");
    expect(r.stdout, "ok\n", "all-ok stdout");

    console.log("exit_codes haisos test passed");
} catch (e) {
    console.error("exit_codes haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

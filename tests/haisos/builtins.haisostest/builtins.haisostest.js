#!/usr/bin/env node

// Runs the builtin commands through the real haisos binary: placed with
// CREATE_DIR/BUILTIN, started by RUN, output read off the console. No LLM is
// involved, so this needs no endpoint.

const { execSync, spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'builtins-haisostest-'));

function writeHaisosfile(runLines) {
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'),
        "FS rootfs MEM\n" +
        "ROOT rootfs\n" +
        "CREATE_DIR /bin\n" +
        "BUILTIN rootfs cat /bin/cat\n" +
        "BUILTIN rootfs echo /bin/echo\n" +
        "BUILTIN rootfs ls /bin/ls\n" +
        "BUILTIN rootfs mkdir /bin/mkdir\n" +
        "BUILTIN rootfs pwd /bin/pwd\n" +
        "CREATE /notes/hello.txt 'Hello, builtins'\n" +
        runLines);
}

function runHaisos(runLines) {
    writeHaisosfile(runLines);
    return execSync(`${haisosPath} haisosfile`, { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
}

function expectContains(output, text, what) {
    if (!output.includes(text)) {
        throw new Error(`${what}: expected the output to contain ${JSON.stringify(text)}, got:\n${output}`);
    }
}

function expectEquals(output, expected, what) {
    if (output !== expected) {
        throw new Error(`${what}: expected exactly ${JSON.stringify(expected)}, got:\n${JSON.stringify(output)}`);
    }
}

try {
    // One RUN per haisos run: RUN processes run concurrently, so their lines
    // could interleave.
    expectContains(runHaisos("RUN /bin/echo hello from echo\n"), "hello from echo", "echo");
    expectContains(runHaisos("RUN /bin/cat /notes/hello.txt\n"), "Hello, builtins", "cat");
    expectContains(runHaisos("RUN /bin/cat /bin/ls\n"), "This is the HaisosOS builtin command ls.", "cat of a builtin");
    // Untagged now: the whole stdout is the command's output, byte for byte.
    expectEquals(runHaisos("RUN /bin/pwd\n"), "/\n", "pwd");
    expectEquals(runHaisos("RUN /bin/echo -n abc\n"), "abc", "echo -n");
    const ls = runHaisos("RUN /bin/ls -l /bin\n");
    expectContains(ls, "-rwxrwxrwx 1", "ls -l");
    expectContains(ls, " mkdir", "ls -l");
    expectContains(runHaisos("RUN /bin/mkdir -v /made\n"), "mkdir: created directory '/made'", "mkdir -v");
    expectContains(runHaisos("RUN /bin/ls --version\n"), "ls (HaisosOS builtin)", "ls --version");

    // Errors go to stderr, and to stderr only.
    writeHaisosfile("RUN /bin/ls /nope\n");
    const failing = spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
    if (failing.error) {
        throw new Error(`ls /nope: ${failing.error.message}`);
    }
    expectEquals(failing.stdout, "", "ls /nope stdout");
    expectContains(failing.stderr, "ls: cannot access '/nope': No such file or directory", "ls /nope stderr");

    console.log("builtins haisos test passed");
} catch (e) {
    console.error("builtins haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

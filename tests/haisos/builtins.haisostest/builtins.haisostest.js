#!/usr/bin/env node

// Runs the builtin commands through the real haisos binary: placed with
// CREATE_DIR/BUILTIN, started by RUN, output read off the console. No LLM is
// involved, so this needs no endpoint.

const { execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'builtins-haisostest-'));

function runHaisos(runLines) {
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
    return execSync(`${haisosPath} haisosfile`, { encoding: 'utf8', timeout: 60000, cwd: tmpDir });
}

function expectContains(output, text, what) {
    if (!output.includes(text)) {
        throw new Error(`${what}: expected the output to contain ${JSON.stringify(text)}, got:\n${output}`);
    }
}

try {
    // One RUN per haisos run: RUN processes run concurrently, so their lines
    // could interleave.
    expectContains(runHaisos("RUN /bin/echo hello from echo\n"), "hello from echo", "echo");
    expectContains(runHaisos("RUN /bin/cat /notes/hello.txt\n"), "Hello, builtins", "cat");
    expectContains(runHaisos("RUN /bin/cat /bin/ls\n"), "This is the HaisosOS builtin command ls.", "cat of a builtin");
    expectContains(runHaisos("RUN /bin/pwd\n"), "] /", "pwd");
    const ls = runHaisos("RUN /bin/ls -l /bin\n");
    expectContains(ls, "-rwxrwxrwx 1", "ls -l");
    expectContains(ls, " mkdir", "ls -l");
    expectContains(runHaisos("RUN /bin/mkdir -v /made\n"), "mkdir: created directory '/made'", "mkdir -v");
    expectContains(runHaisos("RUN /bin/ls --version\n"), "ls (HaisosOS builtin)", "ls --version");
    console.log("builtins haisos test passed");
} catch (e) {
    console.error("builtins haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

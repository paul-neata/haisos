#!/usr/bin/env node

// hsh end to end through the real haisos binary: the develop's acceptance
// scenarios, one haisos run per check. No LLM is
// involved, so this needs no endpoint.

const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const haisosPath = path.join(__dirname, '..', '..', '..', 'output', 'linux', 'haisos');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hsh-haisostest-'));

const preamble =
    "FS rootfs MEM\n" +
    "ROOT rootfs\n" +
    "ENV PATH=/bin\n" +
    "CREATE_DIR /bin\n" +
    "BUILTIN rootfs hsh /bin/hsh\n" +
    "BUILTIN rootfs cat /bin/cat\n" +
    "BUILTIN rootfs echo /bin/echo\n" +
    "BUILTIN rootfs ls /bin/ls\n" +
    "BUILTIN rootfs man /bin/man\n" +
    "BUILTIN rootfs wc /bin/wc\n" +
    // The empty line before the marker keeps abc.txt's trailing newline.
    "CREATE /abc.txt multiline END\n" +
    "one\n" +
    "two\n" +
    "three\n" +
    "\n" +
    "END\n";

function runHaisos(body, input) {
    fs.writeFileSync(path.join(tmpDir, 'haisosfile'), preamble + body);
    const options = { encoding: 'utf8', timeout: 60000, cwd: tmpDir };
    if (input !== undefined) {
        options.input = input;
    }
    return spawnSync(haisosPath, ['haisosfile'], options);
}

function expect(actual, expected, what) {
    if (actual !== expected) {
        throw new Error(`${what}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
}

function expectContains(haystack, needle, what) {
    if (!haystack.includes(needle)) {
        throw new Error(`${what}: expected it to contain ${JSON.stringify(needle)}, got ${JSON.stringify(haystack)}`);
    }
}

function expectNotContains(haystack, needle, what) {
    if (haystack.includes(needle)) {
        throw new Error(`${what}: expected it not to contain ${JSON.stringify(needle)}, got ${JSON.stringify(haystack)}`);
    }
}

try {
    // 1. A pipeline and a redirection.
    let r = runHaisos(
        "RUN /bin/hsh -c 'cat /abc.txt | wc -l; ls /bin | wc -l > /n.txt; cat /n.txt'\n");
    expect(r.stdout, "3\n6\n", "scenario 1 stdout");
    expect(r.status, 0, "scenario 1 status");

    // 2. stderr, || and exit codes.
    r = runHaisos(
        "RUN /bin/hsh -c 'ls /nope 2>/err.txt || echo \"failed: $?\"; cat /err.txt; cat < /abc.txt >> /out.txt 2>&1 && wc -c /out.txt'\n");
    expect(r.stdout,
        "failed: 2\n" +
        "ls: cannot access '/nope': No such file or directory\n" +
        "14 /out.txt\n",
        "scenario 2 stdout");
    expect(r.status, 0, "scenario 2 status");

    r = runHaisos("RUN /bin/ls /nope\n");
    expect(r.stdout, "", "scenario 2 bare ls stdout");
    expectContains(r.stderr, "ls: cannot access '/nope': No such file or directory", "scenario 2 bare ls stderr");
    expect(r.status, 2, "scenario 2 bare ls status");

    r = runHaisos("RUN /bin/hsh -c 'exit 3'\n");
    expect(r.status, 3, "scenario 2 exit-3 status");

    // 3. A script with control flow, a heredoc and $(...). The scenario runs
    // it with `hello`, but `hello` does not match `hi*` -- dash prints `plain`
    // for it. `hi` exercises the `greeted` branch.
    r = runHaisos(
        "CREATE /count.sh multiline END\n" +
        "n=0\n" +
        "for f in a b c; do n=$((n + 1)); done\n" +
        "lines=$(wc -l <<EOF\n" +
        "x\n" +
        "y\n" +
        "EOF\n" +
        ")\n" +
        'if [ "$n" -eq 3 ] && [ "$lines" -eq 2 ]; then echo "ok $n $lines"; else echo "bad"; exit 1; fi\n' +
        'case "$1" in hi*) echo "greeted";; *) echo "plain";; esac\n' +
        "END\n" +
        "RUN /bin/hsh /count.sh hi\n");
    expect(r.stdout, "ok 3 2\ngreeted\n", "scenario 3 stdout");
    expect(r.status, 0, "scenario 3 status");

    // 4. An interactive shell on the console.
    r = runHaisos("RUN -i /bin/hsh\n",
        "echo hi | wc -c\ncd /bin; ls | wc -l\nnosuch\nexit 4\n");
    expect(r.stdout, "3\n6\n", "scenario 4 stdout");
    expectContains(r.stderr, "hsh: 3: nosuch: not found", "scenario 4 stderr error");
    expect(r.stderr.split("$ ").length - 1, 4, "scenario 4 prompt count");
    expect(r.stderr.startsWith("$ "), true, "scenario 4 first prompt at start");
    expectNotContains(r.stderr, "> ", "scenario 4 shows no PS2");
    expect(r.status, 4, "scenario 4 status");

    // 5. Lua through stdio.
    r = runHaisos(
        "CREATE /p.lua multiline END\n" +
        'print("to stdout")\n' +
        'error("boom")\n' +
        "END\n" +
        "RUN /bin/hsh -c '/p.lua 2>/e.txt | wc -l; cat /e.txt'\n");
    expect(r.stdout, "1\nlua: /p.lua:2: boom\n", "scenario 5 stdout");
    expect(r.status, 0, "scenario 5 status");

    r = runHaisos(
        "CREATE /p.lua multiline END\n" +
        'print("to stdout")\n' +
        'error("boom")\n' +
        "END\n" +
        "RUN /p.lua\n");
    expect(r.status, 1, "scenario 5 bare lua status");

    // 6. Manual pages.
    const manWc = runHaisos("RUN /bin/man wc\n");
    const wcHelp = runHaisos("RUN /bin/wc --help\n");
    expect(manWc.status, 0, "scenario 6 man wc status");
    expect(wcHelp.status, 0, "scenario 6 wc --help status");
    if (wcHelp.stdout.length === 0) {
        throw new Error("scenario 6: wc --help output is empty");
    }
    expect(manWc.stdout, wcHelp.stdout, "scenario 6 man wc == wc --help");

    const hshHelp = runHaisos("RUN /bin/hsh --help\n");
    r = runHaisos("RUN /bin/hsh -c 'man hsh | wc -l'\n");
    expect(r.status, 0, "scenario 6 man hsh line count status");
    const manLines = parseInt(r.stdout, 10);
    if (!(manLines > 2 * hshHelp.stdout.split("\n").length)) {
        throw new Error(`scenario 6: man hsh has ${manLines} lines, help has ${hshHelp.stdout.split("\n").length}`);
    }

    r = runHaisos("RUN /bin/man hsh\n");
    expect(r.status, 0, "scenario 6 man hsh status");
    for (const heading of ["QUOTING AND ESCAPING", "PARAMETERS AND EXPANSIONS", "PIPELINES",
                           "REDIRECTIONS", "HERE-DOCUMENTS (HEREDOCS)", "LISTS", "IF",
                           "WHILE AND UNTIL", "FOR", "CASE", "FUNCTIONS"]) {
        if (!r.stdout.split("\n").includes(heading)) {
            throw new Error(`scenario 6: man hsh misses the heading line ${JSON.stringify(heading)}`);
        }
    }

    r = runHaisos("RUN /bin/man nosuch\n");
    expectContains(r.stderr, "No manual entry for nosuch", "scenario 6 man nosuch stderr");
    expect(r.status, 16, "scenario 6 man nosuch status");

    console.log("hsh haisos test passed");
} catch (e) {
    console.error("hsh haisos test failed:", e.message);
    process.exit(1);
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}

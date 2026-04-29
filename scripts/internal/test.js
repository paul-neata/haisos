#!/usr/bin/env node

const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

const SCRIPT_DIR = __dirname;
const PROJECT_DIR = path.resolve(SCRIPT_DIR, '..', '..');
const SMOKE_TESTS_FILE = path.join(SCRIPT_DIR, 'smoke_tests.txt');

function parseArgs(argv) {
    let platformArg = null;
    let selectorArg = null;
    let configs = ['release'];
    let filter = null;
    let smoke = false;

    const args = argv.slice(2);
    let i = 0;
    while (i < args.length) {
        const arg = args[i];
        if (arg === '--debug') {
            configs = ['debug'];
            i++;
        } else if (arg === '--both') {
            configs = ['release', 'debug'];
            i++;
        } else if (arg === '--smoke') {
            smoke = true;
            i++;
        } else if (platformArg === null) {
            platformArg = arg;
            i++;
        } else if (selectorArg === null) {
            selectorArg = arg;
            i++;
        } else {
            if (filter === null) {
                filter = arg;
            } else {
                filter += ' ' + arg;
            }
            i++;
        }
    }

    if (!platformArg) {
        if (process.platform === 'win32') {
            platformArg = 'W';
        } else {
            platformArg = 'L';
        }
    }
    if (!selectorArg) {
        selectorArg = '*';
    }

    return { platformArg, selectorArg, configs, filter, smoke };
}

function getPlatforms(platformArg) {
    const upper = platformArg.toUpperCase();
    if (upper === '*') {
        return ['L', 'W', 'N'];
    }
    const platforms = [];
    if (upper.includes('L')) platforms.push('L');
    if (upper.includes('W')) platforms.push('W');
    if (upper.includes('N')) platforms.push('N');
    return platforms;
}

function getTests(selectorArg) {
    const upper = selectorArg.toUpperCase();
    if (upper === '*') {
        return ['U', 'I', 'H'];
    }
    const tests = [];
    if (upper.includes('U')) tests.push('U');
    if (upper.includes('I')) tests.push('I');
    if (upper.includes('H')) tests.push('H');
    return tests;
}

function getOutputDir(platform, config) {
    const suffix = config === 'debug' ? '_debug' : '';
    switch (platform) {
        case 'L': return path.join(PROJECT_DIR, 'output', `linux${suffix}`);
        case 'W': return path.join(PROJECT_DIR, 'output', `windows${suffix}`);
        case 'N': return path.join(PROJECT_DIR, 'output', `wasm${suffix}`);
        default: return '';
    }
}

function matchesFilter(name, filter) {
    if (!filter) return true;
    return name.toLowerCase().includes(filter.toLowerCase());
}

function getSmokeTests() {
    const tests = new Set();
    if (fs.existsSync(SMOKE_TESTS_FILE)) {
        const content = fs.readFileSync(SMOKE_TESTS_FILE, 'utf8');
        for (const line of content.split('\n')) {
            const trimmed = line.trim();
            if (trimmed && !trimmed.startsWith('#')) {
                tests.add(trimmed);
            }
        }
    }
    return tests;
}

function getRandomString(length) {
    const chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
    let result = '';
    for (let i = 0; i < length; i++) {
        result += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return result;
}

function formatDuration(ms) {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    if (minutes > 0) {
        return `${minutes}m ${seconds}s`;
    }
    return `${seconds}s`;
}

function runTest(cmd, args, timeoutMs, outputFile) {
    return new Promise((resolve) => {
        const startTime = Date.now();
        const outStream = fs.createWriteStream(outputFile, { flags: 'a' });

        outStream.write(`Command: ${cmd} ${args.join(' ')}\n`);
        outStream.write('='.repeat(60) + '\n');

        const options = {
            stdio: ['ignore', 'pipe', 'pipe'],
        };

        let proc;
        try {
            proc = spawn(cmd, args, options);
        } catch (err) {
            outStream.end();
            resolve({
                success: false,
                killed: false,
                elapsed: Date.now() - startTime,
                error: err.message,
                code: -1,
            });
            return;
        }

        let killed = false;
        let killTimer = null;
        let termTimer = null;

        proc.stdout.on('data', (data) => {
            outStream.write(data);
        });

        proc.stderr.on('data', (data) => {
            outStream.write(data);
        });

        killTimer = setTimeout(() => {
            killed = true;
            proc.kill('SIGTERM');
            // Give 5s grace period, then SIGKILL
            termTimer = setTimeout(() => {
                if (!proc.killed) {
                    try {
                        proc.kill('SIGKILL');
                    } catch (_) {
                        // Ignore
                    }
                }
            }, 5000);
        }, timeoutMs);

        proc.on('close', (code, signal) => {
            clearTimeout(killTimer);
            clearTimeout(termTimer);
            outStream.end();
            const elapsed = Date.now() - startTime;
            const success = !killed && code === 0;
            resolve({ success, killed, elapsed, code, signal });
        });

        proc.on('error', (err) => {
            clearTimeout(killTimer);
            clearTimeout(termTimer);
            outStream.end();
            const elapsed = Date.now() - startTime;
            resolve({
                success: false,
                killed: false,
                elapsed,
                error: err.message,
                code: -1,
            });
        });
    });
}

async function discoverUnitTests(platform, outputDir, filter, smokeTests, smoke) {
    const tests = [];
    const suffix = platform === 'W' ? '.unittests.exe' : (platform === 'N' ? '.unittests.js' : '.unittests');
    if (!fs.existsSync(outputDir)) return tests;

    const entries = fs.readdirSync(outputDir);
    for (const entry of entries) {
        if (!entry.endsWith(suffix)) continue;
        if (!matchesFilter(entry, filter)) continue;
        // Unit tests are never subject to smoke filtering
        const fullPath = path.join(outputDir, entry);
        tests.push({ name: entry, path: fullPath, type: 'U', platform });
    }
    return tests;
}

async function discoverIntegrationTests(platform, outputDir, filter, smokeTests, smoke) {
    const tests = [];
    const suffix = platform === 'W' ? '.integrationtest.exe' : (platform === 'N' ? '.integrationtest.js' : '.integrationtest');
    if (!fs.existsSync(outputDir)) return tests;

    const entries = fs.readdirSync(outputDir);
    for (const entry of entries) {
        if (!entry.endsWith(suffix)) continue;
        if (!matchesFilter(entry, filter)) continue;
        if (smoke && !smokeTests.has(entry)) continue;
        const fullPath = path.join(outputDir, entry);
        tests.push({ name: entry, path: fullPath, type: 'I', platform });
    }
    return tests;
}

async function discoverHaisosTests(filter, smokeTests, smoke) {
    const tests = [];
    const haisosDir = path.join(PROJECT_DIR, 'tests', 'haisos');
    if (!fs.existsSync(haisosDir)) return tests;

    const entries = fs.readdirSync(haisosDir);
    for (const entry of entries) {
        if (!entry.endsWith('.haisostest')) continue;
        const subDir = path.join(haisosDir, entry);
        if (!fs.statSync(subDir).isDirectory()) continue;

        const files = fs.readdirSync(subDir);
        for (const file of files) {
            if (!file.endsWith('.haisostest.js')) continue;
            if (!matchesFilter(file, filter)) continue;
            if (smoke && !smokeTests.has(file)) continue;
            const fullPath = path.join(subDir, file);
            tests.push({ name: file, path: fullPath, type: 'H', platform: null });
        }
    }
    return tests;
}

function getTimeout(test) {
    switch (test.type) {
        case 'U': return 30000;
        case 'I': return 120000;
        case 'H': return 120000;
        default: return 30000;
    }
}

async function main() {
    const args = parseArgs(process.argv);
    const platforms = getPlatforms(args.platformArg);
    const testTypes = getTests(args.selectorArg);
    const smokeTests = getSmokeTests();

    const now = new Date();
    const day = String(now.getDate()).padStart(2, '0');
    const time = String(now.getHours()).padStart(2, '0') +
                 String(now.getMinutes()).padStart(2, '0') +
                 String(now.getSeconds()).padStart(2, '0');
    const rand = getRandomString(3);
    const tempFolderName = `${day}_${time}_${rand}`;
    const tempDir = path.join(os.tmpdir(), tempFolderName);
    fs.mkdirSync(tempDir, { recursive: true });

    const allTests = [];

    for (const platform of platforms) {
        for (const config of args.configs) {
            const outputDir = getOutputDir(platform, config);
            if (testTypes.includes('U')) {
                const tests = await discoverUnitTests(platform, outputDir, args.filter, smokeTests, args.smoke);
                allTests.push(...tests);
            }
            if (testTypes.includes('I')) {
                const tests = await discoverIntegrationTests(platform, outputDir, args.filter, smokeTests, args.smoke);
                allTests.push(...tests);
            }
        }
    }

    if (testTypes.includes('H')) {
        const tests = await discoverHaisosTests(args.filter, smokeTests, args.smoke);
        allTests.push(...tests);
    }

    if (allTests.length === 0) {
        console.log('No tests found.');
        process.exit(0);
    }

    console.log(`Test output directory: ${tempDir}`);
    console.log('');

    let passed = 0;
    let failed = 0;
    const failedTests = [];

    for (const test of allTests) {
        const outputFile = path.join(tempDir, test.name + '.txt');
        console.log(test.name);
        console.log(`  Output: ${outputFile}`);

        let cmd;
        let cmdArgs = [];
        if (test.type === 'H') {
            cmd = 'node';
            cmdArgs = [test.path];
        } else if (test.platform === 'N') {
            cmd = 'node';
            cmdArgs = [test.path];
        } else {
            cmd = test.path;
            if (test.type === 'U' && args.filter) {
                cmdArgs.push(`--gtest_filter=*${args.filter}*`);
            }
        }

        const timeoutMs = getTimeout(test);
        const result = await runTest(cmd, cmdArgs, timeoutMs, outputFile);

        if (result.killed) {
            console.log(`  killed after ${formatDuration(result.elapsed)}`);
            failed++;
            failedTests.push({ name: test.name, outputFile });
        } else if (result.success) {
            console.log(`  ${formatDuration(result.elapsed)}`);
            passed++;
        } else {
            console.log(`  ${formatDuration(result.elapsed)}`);
            failed++;
            failedTests.push({ name: test.name, outputFile });
        }
    }

    console.log('');
    console.log('========================================');
    console.log(`Tests summary: ${passed} passed, ${failed} failed`);
    console.log('========================================');

    if (failedTests.length > 0) {
        console.log('');
        console.log('Failed test outputs:');
        for (const ft of failedTests) {
            console.log(`- cat ${ft.outputFile.replace(/\\/g, '/')}`);
        }
        console.log('');
        process.exit(1);
    }

    process.exit(0);
}

main().catch((err) => {
    console.error('Error:', err);
    process.exit(1);
});

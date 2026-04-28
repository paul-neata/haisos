const http = require('http');
const https = require('https');
const url = require('url');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { Logger } = require('./logger');

function hashBody(body) {
    return crypto.createHash('sha256').update(body).digest('hex');
}

function parseArgs(argv) {
    const args = {
        mode: null,
        folder: null,
        logFile: null,
        port: 11435,
    };

    for (let i = 2; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '--proxy') args.mode = 'proxy';
        else if (arg === '--record') args.mode = 'record';
        else if (arg === '--serve') args.mode = 'serve';
        else if (arg === '--folder') args.folder = argv[++i];
        else if (arg === '--log-file') args.logFile = argv[++i];
        else if (arg === '--port') args.port = parseInt(argv[++i], 10);
    }

    return args;
}

function forwardRequest(reqBody, targetUrlStr, incomingHeaders, logger) {
    return new Promise((resolve, reject) => {
        const parsed = url.parse(targetUrlStr);
        const isHttps = parsed.protocol === 'https:';
        const client = isHttps ? https : http;

        const options = {
            hostname: parsed.hostname,
            port: parsed.port || (isHttps ? 443 : 80),
            path: parsed.path,
            method: 'POST',
            headers: {
                ...incomingHeaders,
                'host': parsed.host,
                'content-length': Buffer.byteLength(reqBody),
            },
        };

        const upstreamReq = client.request(options, (upstreamRes) => {
            const chunks = [];
            upstreamRes.on('data', (chunk) => chunks.push(chunk));
            upstreamRes.on('end', () => {
                const responseBody = Buffer.concat(chunks);
                resolve({
                    statusCode: upstreamRes.statusCode,
                    headers: upstreamRes.headers,
                    body: responseBody,
                });
            });
        });

        upstreamReq.on('error', (err) => {
            logger.error(`Upstream request error: ${err.message}`);
            reject(err);
        });

        upstreamReq.write(reqBody);
        upstreamReq.end();
    });
}

function main() {
    const args = parseArgs(process.argv);

    if (!args.mode) {
        console.error('Error: mode is required. Use --proxy, --record, or --serve');
        process.exit(1);
    }
    if (!args.logFile) {
        console.error('Error: --log-file is required');
        process.exit(1);
    }
    if ((args.mode === 'record' || args.mode === 'serve') && !args.folder) {
        console.error(`Error: --folder is required for --${args.mode} mode`);
        process.exit(1);
    }

    const logger = new Logger(args.logFile);

    const upstreamEndpoint = process.env.HAISOS_ENDPOINT || 'http://localhost:11434/api/chat';
    const model = process.env.HAISOS_MODEL || 'llama3';
    const apiKey = process.env.HAISOS_API_KEY || '';

    logger.info(`Starting llm_cache_proxy in ${args.mode} mode`);
    logger.info(`Upstream endpoint: ${upstreamEndpoint}`);
    logger.info(`Folder: ${args.folder || '(none)'}`);

    if (args.mode === 'record' || args.mode === 'serve') {
        if (!fs.existsSync(args.folder)) {
            fs.mkdirSync(args.folder, { recursive: true });
            logger.info(`Created folder: ${args.folder}`);
        }
    }

    const server = http.createServer(async (req, res) => {
        const chunks = [];
        req.on('data', (chunk) => chunks.push(chunk));
        req.on('end', async () => {
            const reqBody = Buffer.concat(chunks);
            const bodyHash = hashBody(reqBody);
            const reqPath = req.url || '/';

            logger.info(`Incoming request: ${req.method} ${reqPath} hash=${bodyHash}`);

            if (args.mode === 'proxy' || args.mode === 'record') {
                try {
                    const upstreamUrl = upstreamEndpoint.replace(/\/$/, '') + reqPath;
                    const response = await forwardRequest(reqBody, upstreamUrl, req.headers, logger);
                    logger.info(`Upstream responded: ${response.statusCode}`);

                    if (args.mode === 'record') {
                        const reqFile = path.join(args.folder, `${bodyHash}.request.json`);
                        const resFile = path.join(args.folder, `${bodyHash}.response.json`);
                        fs.writeFileSync(reqFile, reqBody);
                        fs.writeFileSync(resFile, response.body);
                        logger.info(`Recorded: ${reqFile}, ${resFile}`);
                    }

                    res.writeHead(response.statusCode, response.headers);
                    res.end(response.body);
                } catch (err) {
                    logger.error(`Proxy error: ${err.message}`);
                    res.writeHead(502, { 'content-type': 'application/json' });
                    res.end(JSON.stringify({ error: 'Bad Gateway', message: err.message }));
                }
            } else if (args.mode === 'serve') {
                const resFile = path.join(args.folder, `${bodyHash}.response.json`);
                if (fs.existsSync(resFile)) {
                    const cachedBody = fs.readFileSync(resFile);
                    logger.info(`Cache hit: ${resFile}`);
                    res.writeHead(200, { 'content-type': 'application/json' });
                    res.end(cachedBody);
                } else {
                    logger.error(`Cache miss: ${resFile} not found`);
                    res.writeHead(404, { 'content-type': 'application/json' });
                    res.end(JSON.stringify({ error: 'Not found', message: 'No cached response for this request' }));
                }
            }
        });
    });

    server.listen(args.port, () => {
        const proxyUrl = `http://localhost:${args.port}`;
        const proxyEndpoint = `${proxyUrl}${url.parse(upstreamEndpoint).pathname}`;

        logger.info(`Proxy listening on ${proxyUrl}`);
        console.log('');
        console.log(`Proxy listening on ${proxyUrl}`);
        console.log(`New HAISOS_ENDPOINT=${proxyEndpoint}`);
        console.log(`New HAISOS_MODEL=${model}`);
        console.log(`New HAISOS_API_KEY=${apiKey ? '(present)' : '(not set)'}`);
        console.log('');
    });

    process.on('SIGINT', () => {
        logger.info('Shutting down...');
        logger.close();
        server.close(() => process.exit(0));
    });
}

main();

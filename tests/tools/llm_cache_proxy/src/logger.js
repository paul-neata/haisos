const fs = require('fs');

class Logger {
    constructor(logFilePath) {
        this.logFilePath = logFilePath;
        this.stream = fs.createWriteStream(logFilePath, { flags: 'a' });
    }

    log(level, message) {
        const timestamp = new Date().toISOString();
        const line = `[${timestamp}] [${level}] ${message}\n`;
        this.stream.write(line);
        // Also echo to console for visibility
        console.log(line.trim());
    }

    info(message) { this.log('INFO', message); }
    warn(message) { this.log('WARN', message); }
    error(message) { this.log('ERROR', message); }
    debug(message) { this.log('DEBUG', message); }

    close() {
        this.stream.end();
    }
}

module.exports = { Logger };

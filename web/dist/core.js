"use strict";
// Client of pap-core's command port (TCP, text protocol = protocol.md): one connection,
// one command in flight, records collected until "ok" / "error ...", "!" lines are events.
// Reconnects by itself; the same logic as the ESP32's serial link (esp32-ui/src/link).
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.CoreLink = void 0;
exports.defaultTimeout = defaultTimeout;
const node_events_1 = require("node:events");
const net = __importStar(require("node:net"));
const record_1 = require("./record");
function defaultTimeout(line) {
    const w = line.trim().split(/\s+/)[0].toUpperCase();
    if (w === 'HOME')
        return 330_000;
    if (w === 'CAL')
        return 120_000;
    if (['RUN', 'CHECK', 'LS', 'FEEDERS', 'CAT', 'SD', 'SCAN'].includes(w))
        return 30_000;
    if (w === 'WAIT')
        return 600_000;
    return 10_000;
}
class CoreLink extends node_events_1.EventEmitter {
    host;
    port;
    reportMs;
    sock = null;
    buf = '';
    queue = [];
    cur = null;
    records = [];
    timer = null;
    lastRx = 0;
    closed = false;
    wasConnected = false;
    tick;
    constructor(host, port, reportMs = 250) {
        super();
        this.host = host;
        this.port = port;
        this.reportMs = reportMs;
        this.open();
        // connected() follows the reports; tell listeners when it changes
        this.tick = setInterval(() => {
            const c = this.connected();
            if (c !== this.wasConnected) {
                this.wasConnected = c;
                this.emit('link', c);
            }
        }, 500);
        this.tick.unref();
    }
    // Heard from the core in the last 2 s (it reports every reportMs)
    connected() {
        return this.sock !== null && Date.now() - this.lastRx < 2000;
    }
    queued() {
        return this.queue.length + (this.cur ? 1 : 0);
    }
    close() {
        this.closed = true;
        clearInterval(this.tick);
        this.sock?.destroy();
        this.failAll('closed');
    }
    send(line, timeoutMs = 0) {
        return new Promise((resolve) => {
            this.queue.push({ line, timeoutMs: timeoutMs || defaultTimeout(line), resolve });
            this.next();
        });
    }
    // Upload a text file line by line with CRC (UPLOAD <path> <bytes>, |line ..., END <crc>)
    upload(path, text, progress) {
        const lines = (0, record_1.uploadLines)(text);
        const total = lines.reduce((n, l) => n + Buffer.byteLength(l) + 1, 0);
        const p = /[\s"=]/.test(path) ? `"${path}"` : path;
        return new Promise((resolve) => {
            this.queue.push({
                line: `UPLOAD ${p} ${total}`,
                timeoutMs: 10_000,
                resolve,
                upload: { lines, pos: 0, crc: 0xffffffff, done: false, progress, bytes: 0, total },
            });
            this.next();
        });
    }
    open() {
        if (this.closed)
            return;
        const s = net.createConnection({ host: this.host, port: this.port });
        s.setNoDelay(true);
        s.setEncoding('utf8');
        s.on('connect', () => {
            this.sock = s;
            this.buf = '';
            // Status reports first (each TCP connection is its own port on the core)
            this.queue.unshift({ line: `REPORT ${this.reportMs}`, timeoutMs: 5000, resolve: () => { } });
            this.next();
        });
        s.on('data', (d) => this.onData(d));
        s.on('error', () => { });
        s.on('close', () => {
            if (this.sock === s)
                this.sock = null;
            this.lastRx = 0;
            this.failAll('core offline');
            if (!this.closed)
                setTimeout(() => this.open(), 1000);
        });
    }
    failAll(error) {
        const all = this.cur ? [this.cur, ...this.queue] : [...this.queue];
        this.cur = null;
        this.queue = [];
        if (this.timer)
            clearTimeout(this.timer);
        for (const r of all)
            r.resolve({ ok: false, error, records: [] });
    }
    write(line) {
        this.sock?.write(line + '\n');
    }
    arm() {
        if (this.timer)
            clearTimeout(this.timer);
        const cur = this.cur;
        if (!cur)
            return;
        this.timer = setTimeout(() => {
            if (this.cur !== cur)
                return;
            if (cur.upload)
                this.write('ABORT');
            this.finish({ ok: false, error: 'no answer from the controller', timeout: true, records: this.records });
        }, cur.timeoutMs);
    }
    next() {
        if (this.cur || !this.sock || !this.queue.length)
            return;
        this.cur = this.queue.shift();
        this.records = [];
        this.write(this.cur.line);
        this.arm();
    }
    finish(r) {
        const cur = this.cur;
        this.cur = null;
        if (this.timer)
            clearTimeout(this.timer);
        cur?.resolve(r);
        this.next();
    }
    uploadStep() {
        const u = this.cur.upload;
        if (u.pos >= u.lines.length) {
            u.done = true;
            this.write('END ' + ((u.crc ^ 0xffffffff) >>> 0).toString(16).toUpperCase().padStart(8, '0'));
            this.arm();
            return;
        }
        const line = u.lines[u.pos++];
        u.crc = (0, record_1.crc32)(line + '\n', u.crc);
        u.bytes += Buffer.byteLength(line) + 1;
        this.write('|' + line);
        this.arm();
        u.progress?.(u.bytes, u.total);
    }
    onData(d) {
        this.buf += d;
        let i;
        while ((i = this.buf.indexOf('\n')) >= 0) {
            let line = this.buf.slice(0, i);
            this.buf = this.buf.slice(i + 1);
            if (line.endsWith('\r'))
                line = line.slice(0, -1);
            this.lastRx = Date.now();
            if (line)
                this.onLine(line);
        }
    }
    onLine(line) {
        if (line[0] === '!') {
            const rec = (0, record_1.parseRecord)(line);
            this.emit('event', rec);
            return;
        }
        const cur = this.cur;
        if (!cur)
            return; // e.g. the "ok" of REPORT
        const isOk = line === 'ok' || line.startsWith('ok ');
        if (line.startsWith('error')) {
            this.finish({ ok: false, error: line.length > 6 ? line.slice(6) : 'error', records: this.records });
            return;
        }
        if (cur.upload && !cur.upload.done) {
            if (isOk)
                this.uploadStep();
            return;
        }
        if (isOk)
            this.finish({ ok: true, error: '', records: this.records });
        else
            this.records.push((0, record_1.parseRecord)(line));
    }
}
exports.CoreLink = CoreLink;
//# sourceMappingURL=core.js.map
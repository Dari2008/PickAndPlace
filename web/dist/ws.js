"use strict";
// Minimal WebSocket server side (RFC 6455) on node:http's upgrade event: text frames,
// ping / pong, close; fragmented messages are joined. No extensions.
Object.defineProperty(exports, "__esModule", { value: true });
exports.WsClient = void 0;
exports.acceptKey = acceptKey;
exports.encodeFrame = encodeFrame;
exports.decodeFrame = decodeFrame;
exports.upgrade = upgrade;
const node_crypto_1 = require("node:crypto");
const node_events_1 = require("node:events");
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const MAX_MESSAGE = 4 * 1024 * 1024;
function acceptKey(key) {
    return (0, node_crypto_1.createHash)('sha1').update(key + GUID).digest('base64');
}
// One frame from the server (never masked)
function encodeFrame(opcode, payload) {
    const n = payload.length;
    let head;
    if (n < 126)
        head = Buffer.from([0x80 | opcode, n]);
    else if (n < 65536) {
        head = Buffer.alloc(4);
        head[0] = 0x80 | opcode;
        head[1] = 126;
        head.writeUInt16BE(n, 2);
    }
    else {
        head = Buffer.alloc(10);
        head[0] = 0x80 | opcode;
        head[1] = 127;
        head.writeBigUInt64BE(BigInt(n), 2);
    }
    return Buffer.concat([head, payload]);
}
// Next complete frame at the start of buf (client frames are masked), or null
function decodeFrame(buf) {
    if (buf.length < 2)
        return null;
    const fin = (buf[0] & 0x80) !== 0;
    const opcode = buf[0] & 0x0f;
    const masked = (buf[1] & 0x80) !== 0;
    let len = buf[1] & 0x7f;
    let pos = 2;
    if (len === 126) {
        if (buf.length < 4)
            return null;
        len = buf.readUInt16BE(2);
        pos = 4;
    }
    else if (len === 127) {
        if (buf.length < 10)
            return null;
        const big = buf.readBigUInt64BE(2);
        if (big > BigInt(MAX_MESSAGE))
            throw new Error('frame too large');
        len = Number(big);
        pos = 10;
    }
    const maskAt = pos;
    if (masked)
        pos += 4;
    if (buf.length < pos + len)
        return null;
    const payload = Buffer.from(buf.subarray(pos, pos + len));
    if (masked)
        for (let i = 0; i < len; i++)
            payload[i] ^= buf[maskAt + (i & 3)];
    return { fin, opcode, payload, size: pos + len };
}
class WsClient extends node_events_1.EventEmitter {
    sock;
    id;
    buf = Buffer.alloc(0);
    parts = [];
    partOpcode = 0;
    open = true;
    constructor(sock, id) {
        super();
        this.sock = sock;
        this.id = id;
        sock.on('data', (d) => this.onData(d));
        sock.on('close', () => this.gone());
        sock.on('error', () => this.gone());
    }
    send(text) {
        if (this.open)
            this.sock.write(encodeFrame(0x1, Buffer.from(text, 'utf8')));
    }
    close() {
        if (!this.open)
            return;
        this.sock.write(encodeFrame(0x8, Buffer.alloc(0)));
        this.sock.end();
        this.gone();
    }
    gone() {
        if (!this.open)
            return;
        this.open = false;
        this.emit('close');
    }
    onData(d) {
        this.buf = Buffer.concat([this.buf, d]);
        for (;;) {
            let f;
            try {
                f = decodeFrame(this.buf);
            }
            catch {
                this.sock.destroy();
                return;
            }
            if (!f)
                return;
            this.buf = this.buf.subarray(f.size);
            if (f.opcode === 0x8) {
                this.close();
                return;
            }
            if (f.opcode === 0x9) {
                this.sock.write(encodeFrame(0xa, f.payload));
                continue;
            }
            if (f.opcode === 0xa)
                continue;
            if (f.opcode !== 0) {
                this.partOpcode = f.opcode;
                this.parts = [];
            }
            this.parts.push(f.payload);
            if (this.parts.reduce((n, p) => n + p.length, 0) > MAX_MESSAGE) {
                this.sock.destroy();
                return;
            }
            if (f.fin) {
                const msg = Buffer.concat(this.parts);
                this.parts = [];
                if (this.partOpcode === 0x1)
                    this.emit('message', msg.toString('utf8'));
            }
        }
    }
}
exports.WsClient = WsClient;
let nextId = 1;
// Answer the upgrade request; null (and 400) when it isn't a WebSocket handshake
function upgrade(req, sock) {
    const key = req.headers['sec-websocket-key'];
    if (typeof key !== 'string' || (req.headers.upgrade || '').toLowerCase() !== 'websocket') {
        sock.end('HTTP/1.1 400 Bad Request\r\n\r\n');
        return null;
    }
    sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
        `Sec-WebSocket-Accept: ${acceptKey(key)}\r\n\r\n`);
    return new WsClient(sock, nextId++);
}
//# sourceMappingURL=ws.js.map
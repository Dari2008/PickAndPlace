"use strict";
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
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
// node --test dist/test/  (no pap-core needed: a fake core speaks the text protocol)
const strict_1 = __importDefault(require("node:assert/strict"));
const fs = __importStar(require("node:fs"));
const net = __importStar(require("node:net"));
const os = __importStar(require("node:os"));
const path = __importStar(require("node:path"));
const node_test_1 = require("node:test");
const auth_1 = require("../auth");
const core_1 = require("../core");
const record_1 = require("../record");
const server_1 = require("../server");
const ws_1 = require("../ws");
(0, node_test_1.test)('records parse like the C++ parser', () => {
    strict_1.default.deepEqual((0, record_1.parseRecord)('feeder num=3 name="Tape 8 mm" pos=12.5'), { type: 'feeder', f: { num: '3', name: 'Tape 8 mm', pos: '12.5' } });
    strict_1.default.deepEqual((0, record_1.parseRecord)('!status x=1 homed=1'), { type: 'status', f: { x: '1', homed: '1' } });
    strict_1.default.deepEqual((0, record_1.parseRecord)('|[board] x=1'), { type: '|', text: '[board] x=1' });
    strict_1.default.deepEqual((0, record_1.parseRecord)('set key= flag'), { type: 'set', f: { key: '', flag: '' } });
});
(0, node_test_1.test)('crc32 and upload lines', () => {
    strict_1.default.equal(((0, record_1.crc32)('123456789') ^ 0xffffffff) >>> 0, 0xcbf43926); // running value, final XOR by the caller
    strict_1.default.deepEqual((0, record_1.uploadLines)('a\r\nb\n'), ['a', 'b']);
    strict_1.default.deepEqual((0, record_1.uploadLines)('a\n\n'), ['a', '']);
    strict_1.default.deepEqual((0, record_1.uploadLines)('a'), ['a']);
    strict_1.default.deepEqual((0, record_1.uploadLines)(''), []);
    strict_1.default.equal((0, core_1.defaultTimeout)('home'), 330_000);
    strict_1.default.equal((0, core_1.defaultTimeout)('CAL BOTTOM'), 120_000);
});
(0, node_test_1.test)('websocket frames: masked in, plain out, long lengths', () => {
    for (const n of [5, 300, 70000]) {
        const payload = Buffer.alloc(n, 0x61);
        const out = (0, ws_1.decodeFrame)((0, ws_1.encodeFrame)(1, payload));
        strict_1.default.equal(out.payload.length, n);
        // client frame: masked
        const mask = Buffer.from([1, 2, 3, 4]);
        const plain = (0, ws_1.encodeFrame)(1, payload);
        const hlen = n < 126 ? 2 : n < 65536 ? 4 : 10;
        const head = Buffer.from(plain.subarray(0, hlen));
        head[1] |= 0x80;
        const body = Buffer.from(payload.map((b, i) => b ^ mask[i & 3]));
        const f = (0, ws_1.decodeFrame)(Buffer.concat([head, mask, body]));
        strict_1.default.ok(f.payload.equals(payload));
        strict_1.default.equal(f.size, hlen + 4 + n);
        strict_1.default.equal((0, ws_1.decodeFrame)(Buffer.concat([head, mask, body.subarray(0, n - 1)])), null);
    }
});
(0, node_test_1.test)('login file, token and cookie', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'papweb-'));
    const a = (0, auth_1.readLogin)(dir);
    strict_1.default.equal(a.password.length, 8);
    strict_1.default.deepEqual((0, auth_1.readLogin)(dir), a);
    (0, auth_1.setPassword)(dir, 'hello');
    const b = (0, auth_1.readLogin)(dir);
    strict_1.default.equal(b.password, 'hello');
    strict_1.default.notEqual(b.secret, a.secret);
    strict_1.default.equal((0, auth_1.cookieToken)(`x=1; pap=${(0, auth_1.token)(b)}; y=2`), (0, auth_1.token)(b));
});
// A fake pap-core: answers STATUS / REPORT / JOB / SET, checks uploads, sends events
function fakeCore() {
    const saved = new Map();
    const socks = new Set();
    const server = net.createServer((s) => {
        socks.add(s);
        s.on('close', () => socks.delete(s));
        let buf = '';
        let up = null;
        let timer = null;
        s.on('data', (d) => {
            buf += d.toString();
            let i;
            while ((i = buf.indexOf('\n')) >= 0) {
                const line = buf.slice(0, i);
                buf = buf.slice(i + 1);
                if (up) {
                    if (line.startsWith('|')) {
                        up.text += line.slice(1) + '\n';
                        up.crc = (0, record_1.crc32)(line.slice(1) + '\n', up.crc);
                        s.write('ok\n');
                    }
                    else if (line.startsWith('END ')) {
                        const want = ((up.crc ^ 0xffffffff) >>> 0).toString(16).toUpperCase().padStart(8, '0');
                        if (line.slice(4) === want) {
                            saved.set(up.path, up.text);
                            s.write(`saved path=${up.path}\nok\n`);
                        }
                        else
                            s.write('error CRC mismatch\n');
                        up = null;
                    }
                    continue;
                }
                const w = line.split(' ');
                if (w[0] === 'REPORT') {
                    if (timer)
                        clearInterval(timer);
                    timer = setInterval(() => !s.destroyed && s.write('!status state=idle x=1.00\n'), 100);
                    s.on('close', () => timer && clearInterval(timer));
                    s.write('ok\n');
                }
                else if (w[0] === 'STATUS')
                    s.write('status state=idle x=1.00 y=2.00\nok\n');
                else if (w[0] === 'JOB')
                    s.write('job state=idle\nok\n');
                else if (w[0] === 'SET')
                    s.write(`set key=${w[1]} value=${w[2]}\n!changed what=settings\nok\n`);
                else if (w[0] === 'UPLOAD') {
                    up = { path: w[1], text: '', crc: 0xffffffff };
                    s.write('ok\n');
                }
                else
                    s.write(`error unknown command ${w[0]}\n`);
            }
        });
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({
        port: server.address().port,
        server,
        saved,
        kick: () => socks.forEach((s) => s.destroy()),
        stop: () => {
            socks.forEach((s) => s.destroy());
            server.close();
        },
    })));
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
(0, node_test_1.test)('core link: replies, events, upload with CRC, reconnect', async () => {
    const fc = await fakeCore();
    const link = new core_1.CoreLink('127.0.0.1', fc.port, 100);
    const events = [];
    link.on('event', (r) => events.push(r.type));
    const st = await link.send('STATUS');
    strict_1.default.equal(st.ok, true);
    strict_1.default.equal(st.records[0].f.y, '2.00');
    const bad = await link.send('NOPE');
    strict_1.default.equal(bad.ok, false);
    strict_1.default.match(bad.error, /unknown command/);
    const set = await link.send('SET beep 0');
    strict_1.default.equal(set.records.length, 1); // the event is not part of the reply
    const text = '[board]\r\nname = test\n\n[parts]\n';
    let progress = 0;
    const up = await link.upload('/jobs/a.pap', text, (d) => (progress = d));
    strict_1.default.equal(up.ok, true, up.error);
    strict_1.default.equal(fc.saved.get('/jobs/a.pap'), '[board]\nname = test\n\n[parts]\n');
    strict_1.default.equal(progress, 29); // bytes of the sent lines, each with its newline
    await sleep(300);
    strict_1.default.ok(events.includes('status'));
    strict_1.default.ok(events.includes('changed'));
    strict_1.default.equal(link.connected(), true);
    fc.kick(); // core restart
    await sleep(50);
    const lost = await link.send('STATUS');
    strict_1.default.equal(lost.ok, true, 'reconnected and answered'); // queued until the reconnect
    link.close();
    fc.stop();
});
(0, node_test_1.test)('server: login, session, websocket command, upload', { skip: typeof globalThis.WebSocket !== 'function' }, async () => {
    const fc = await fakeCore();
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'papweb-'));
    (0, auth_1.setPassword)(dir, 'secret1');
    const pub = fs.mkdtempSync(path.join(os.tmpdir(), 'papweb-pub-'));
    fs.writeFileSync(path.join(pub, 'index.html'), '<h1>hi</h1>');
    const { server, core } = (0, server_1.startServer)({ port: 0, bind: '127.0.0.1', coreHost: '127.0.0.1', corePort: fc.port, data: dir, publicDir: pub, maxUpload: 1 << 20 });
    await new Promise((r) => server.once('listening', r));
    const base = `http://127.0.0.1:${server.address().port}`;
    strict_1.default.equal((await fetch(base + '/api/session')).status, 401);
    strict_1.default.equal((await fetch(base + '/api/login', { method: 'POST', body: new URLSearchParams({ password: 'wrong' }) })).status, 401);
    const ok = await fetch(base + '/api/login', { method: 'POST', body: new URLSearchParams({ password: 'secret1' }) });
    strict_1.default.equal(ok.status, 200);
    const cookie = (ok.headers.get('set-cookie') || '').split(';')[0];
    strict_1.default.equal((await fetch(base + '/api/session', { headers: { cookie } })).status, 200);
    strict_1.default.equal(await (await fetch(base + '/')).text(), '<h1>hi</h1>');
    strict_1.default.equal((await fetch(base + '/../web.txt')).status !== 200, true);
    const WS = globalThis.WebSocket;
    const ws = new WS(base.replace('http', 'ws') + '/ws', { headers: { cookie } });
    const msgs = [];
    ws.onmessage = (m) => msgs.push(JSON.parse(m.data));
    await new Promise((r, j) => ((ws.onopen = r), (ws.onerror = j)));
    ws.send(JSON.stringify({ t: 'cmd', id: 7, line: 'STATUS' }));
    ws.send(JSON.stringify({ t: 'sys', id: 8, op: 'info' }));
    const up = await fetch(base + '/api/upload?path=/jobs/b.pap', { method: 'POST', headers: { cookie }, body: 'x = 1\n' });
    strict_1.default.equal(up.status, 202);
    await sleep(400);
    const reply = msgs.find((m) => m.t === 'reply' && m.id === 7);
    strict_1.default.equal(reply.ok, true);
    strict_1.default.equal(reply.records[0].f.x, '1.00');
    strict_1.default.ok(msgs.find((m) => m.t === 'reply' && m.id === 8).sys.hostname);
    strict_1.default.ok(msgs.some((m) => m.t === 'ev' && m.type === 'job'));
    strict_1.default.ok(msgs.some((m) => m.t === 'upload' && m.state === 'done'));
    strict_1.default.equal(fc.saved.get('/jobs/b.pap'), 'x = 1\n');
    ws.close();
    core.close();
    server.closeAllConnections();
    server.close();
    fc.stop();
});
//# sourceMappingURL=web.test.js.map
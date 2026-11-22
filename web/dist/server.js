"use strict";
// pap-web: the pick & place website on the Pi.
//   node dist/server.js            (config: config.json next to package.json, or env, see loadConfig)
// Serves public/ and passes the browser's protocol commands to pap-core (TCP, protocol.md)
// over one WebSocket per browser - the same messages the ESP32 website used:
//   browser -> {t:"cmd", id, line}                 server -> {t:"reply", id, ok, error, records:[{type, f}|{type:"|", text}]}
//   browser -> {t:"sys", id, op:"info"|"password"}  server -> {t:"reply", id, ok, sys:{...}}
//   server  -> {t:"ev", type, f} (controller events), {t:"link", connected}, {t:"upload", path, done, total, state}
// HTTP: POST /api/login (password=...), /api/logout, GET /api/session, POST /api/upload?path=/jobs/x.pap (body = text),
//       GET /api/cam?cam=top|bottom (one JPEG of the camera now: pap-core saves it into its data folder, CAM ... SAVE)
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
exports.loadConfig = loadConfig;
exports.startServer = startServer;
const fs = __importStar(require("node:fs"));
const http = __importStar(require("node:http"));
const os = __importStar(require("node:os"));
const path = __importStar(require("node:path"));
const auth_1 = require("./auth");
const core_1 = require("./core");
const ws_1 = require("./ws");
function loadConfig() {
    const root = path.join(__dirname, '..');
    let file = {};
    try {
        file = JSON.parse(fs.readFileSync(process.env.PAP_WEB_CONFIG || path.join(root, 'config.json'), 'utf8'));
    }
    catch {
        /* defaults */
    }
    const env = process.env;
    return {
        port: +(env.PAP_WEB_PORT || file.port || 8080),
        bind: env.PAP_WEB_BIND || file.bind || '0.0.0.0',
        coreHost: env.PAP_CORE_HOST || file.coreHost || '127.0.0.1',
        corePort: +(env.PAP_CORE_PORT || file.corePort || 7070),
        data: env.PAP_DATA || file.data || path.join(os.homedir(), 'pap-data'),
        publicDir: file.publicDir ? path.resolve(root, file.publicDir) : path.join(root, 'public'),
        maxUpload: file.maxUpload || 8 * 1024 * 1024,
    };
}
const TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.ico': 'image/x-icon',
    '.json': 'application/json',
};
function json(res, code, body, headers = {}) {
    res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...headers });
    res.end(JSON.stringify(body));
}
function readBody(req, max) {
    return new Promise((resolve) => {
        const parts = [];
        let n = 0;
        req.on('data', (d) => {
            n += d.length;
            if (n > max) {
                resolve(null);
                req.destroy();
            }
            else
                parts.push(d);
        });
        req.on('end', () => resolve(Buffer.concat(parts)));
        req.on('error', () => resolve(null));
    });
}
function sysInfo(core) {
    const addrs = [];
    for (const list of Object.values(os.networkInterfaces()))
        for (const a of list || [])
            if (a.family === 'IPv4' && !a.internal)
                addrs.push(a.address);
    return {
        hostname: os.hostname(),
        ip: addrs.join(', '),
        uptime: Math.round(os.uptime()),
        load: os.loadavg()[0].toFixed(2),
        memFree: Math.round(os.freemem() / 1048576),
        memTotal: Math.round(os.totalmem() / 1048576),
        link: core.connected(),
    };
}
function startServer(cfg, core = new core_1.CoreLink(cfg.coreHost, cfg.corePort)) {
    const clients = new Set();
    const broadcast = (m) => {
        const s = JSON.stringify(m);
        for (const c of clients)
            c.send(s);
    };
    const authed = (req) => {
        const t = (0, auth_1.cookieToken)(req.headers.cookie);
        return t !== '' && t === (0, auth_1.token)((0, auth_1.readLogin)(cfg.data));
    };
    core.on('event', (r) => broadcast({ t: 'ev', type: r.type, f: r.f || {} }));
    core.on('link', (connected) => broadcast({ t: 'link', connected }));
    const replyMsg = (id, r) => ({ t: 'reply', id, ok: r.ok, error: r.error, records: r.records });
    async function onMessage(c, text) {
        let m;
        try {
            m = JSON.parse(text);
        }
        catch {
            return;
        }
        const id = m.id || 0;
        if (m.t === 'cmd' && m.line) {
            const line = String(m.line).replace(/[\r\n]/g, ' ').trim();
            if (!line)
                return;
            c.send(JSON.stringify(replyMsg(id, await core.send(line))));
        }
        else if (m.t === 'sys') {
            const out = { t: 'reply', id, ok: true };
            if (m.op === 'password') {
                const p = String(m.value || '');
                if (p.length < 4 || /\s/.test(p)) {
                    out.ok = false;
                    out.error = 'at least 4 characters, no spaces';
                }
                else
                    (0, auth_1.setPassword)(cfg.data, p);
            }
            out.sys = sysInfo(core);
            c.send(JSON.stringify(out));
        }
    }
    async function upload(req, res, url) {
        const p = url.searchParams.get('path') || '';
        if (!p.startsWith('/') || !p.endsWith('.pap') || p.includes('..'))
            return json(res, 400, { ok: false, error: 'need ?path=/folder/name.pap and a body' });
        const body = await readBody(req, cfg.maxUpload);
        if (!body)
            return json(res, 413, { ok: false, error: 'file too large' });
        json(res, 202, { ok: true });
        const total = body.length;
        let last = 0;
        broadcast({ t: 'upload', path: p, done: 0, total, state: 'running' });
        const r = await core.upload(p, body.toString('utf8'), (done, t) => {
            if (Date.now() - last < 200 && done < t)
                return;
            last = Date.now();
            broadcast({ t: 'upload', path: p, done, total: t, state: 'running' });
        });
        broadcast({ t: 'upload', path: p, done: r.ok ? total : 0, total, state: r.ok ? 'done' : 'error', error: r.error });
    }
    // Live picture: one frame per request and camera at a time (the browser asks for the next when one arrived)
    const camBusy = {};
    async function camFrame(res, url) {
        const cam = url.searchParams.get('cam') === 'bottom' ? 'bottom' : 'top';
        const prev = camBusy[cam] || Promise.resolve();
        let done;
        camBusy[cam] = new Promise((r) => (done = r));
        await prev;
        try {
            const name = `.live_${cam}.jpg`;
            const r = await core.send(`CAM ${cam} SAVE /${name}`, 5000);
            if (!r.ok)
                return json(res, 503, { ok: false, error: r.error || 'no picture' });
            const data = await fs.promises.readFile(path.join(cfg.data, name));
            res.writeHead(200, { 'Content-Type': 'image/jpeg', 'Cache-Control': 'no-store' });
            res.end(data);
        }
        catch (e) {
            json(res, 503, { ok: false, error: e.message });
        }
        finally {
            done();
        }
    }
    function serveStatic(res, url) {
        let rel = decodeURIComponent(url.pathname);
        if (rel.endsWith('/'))
            rel += 'index.html';
        const file = path.resolve(cfg.publicDir, '.' + rel);
        if (!file.startsWith(path.resolve(cfg.publicDir) + path.sep)) {
            res.writeHead(403).end();
            return;
        }
        fs.readFile(file, (err, data) => {
            if (err) {
                res.writeHead(404, { 'Content-Type': 'text/plain' }).end('not found');
                return;
            }
            res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
            res.end(data);
        });
    }
    const server = http.createServer(async (req, res) => {
        const url = new URL(req.url || '/', 'http://localhost');
        const route = `${req.method} ${url.pathname}`;
        if (route === 'POST /api/login') {
            const body = (await readBody(req, 4096)) || Buffer.alloc(0);
            const pass = new URLSearchParams(body.toString('utf8')).get('password') || '';
            const login = (0, auth_1.readLogin)(cfg.data);
            if (pass !== login.password) {
                setTimeout(() => json(res, 401, { ok: false }), 400); // slow down guessing
                return;
            }
            return json(res, 200, { ok: true }, { 'Set-Cookie': `pap=${(0, auth_1.token)(login)}; Path=/; Max-Age=31536000; SameSite=Strict; HttpOnly` });
        }
        if (route === 'POST /api/logout')
            return json(res, 200, { ok: true }, { 'Set-Cookie': 'pap=; Path=/; Max-Age=0' });
        if (route === 'GET /api/session')
            return authed(req) ? json(res, 200, { ok: true }) : json(res, 401, { ok: false });
        if (route === 'POST /api/upload') {
            if (!authed(req))
                return json(res, 401, { ok: false });
            return upload(req, res, url);
        }
        if (route === 'GET /api/cam') {
            if (!authed(req))
                return json(res, 401, { ok: false });
            return camFrame(res, url);
        }
        if (req.method === 'GET')
            return serveStatic(res, url);
        res.writeHead(405).end();
    });
    server.on('upgrade', (req, sock) => {
        if (new URL(req.url || '/', 'http://localhost').pathname !== '/ws' || !authed(req)) {
            sock.end('HTTP/1.1 401 Unauthorized\r\n\r\n');
            return;
        }
        const c = (0, ws_1.upgrade)(req, sock);
        if (!c)
            return;
        clients.add(c);
        c.on('close', () => clients.delete(c));
        c.on('message', (t) => void onMessage(c, t));
        c.send(JSON.stringify({ t: 'link', connected: core.connected() }));
        // Start the page with the current job state
        void core.send('JOB').then((r) => {
            if (r.ok && r.records.length)
                c.send(JSON.stringify({ t: 'ev', type: 'job', f: r.records[0].f || {} }));
        });
    });
    server.listen(cfg.port, cfg.bind);
    return { server, core, clients };
}
if (require.main === module) {
    const cfg = loadConfig();
    const { server } = startServer(cfg);
    server.on('listening', () => console.log(`[web] http://${cfg.bind}:${cfg.port}  core ${cfg.coreHost}:${cfg.corePort}  data ${cfg.data}`));
    server.on('error', (e) => {
        console.error('[web]', e.message);
        process.exit(1);
    });
}
//# sourceMappingURL=server.js.map
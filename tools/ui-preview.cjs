// UI preview on Windows: the real display code (pap-core) runs in simulation mode inside WSL, this script shows
// its screen in the browser and starts the website (pi/web, on Windows Node) against the same simulated machine.
// Click = touch (drag scrolls), wheel / arrow keys / the on-screen stick = joystick,
// Enter = joystick button, Esc = back. Simulation only: no Octopus, no GPIO, no SPI is touched.
//   double-click ..\ui-preview.cmd     or     node pi/tools/ui-preview.cjs [--port 8090] [--web-port 8080] [--no-web]
//                                                                         [--distro Ubuntu-24.04] [--no-browser]
//   display: http://127.0.0.1:8090     website: http://localhost:8080 (its password is printed in the console)
// Needs WSL with g++ and make (sudo apt install build-essential); the first start builds pap-core (~2 min).
'use strict';
const { spawn, execFileSync } = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf('--' + name);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
};
const WEB_PORT = +opt('port', 8090);
const CORE_PORT = +opt('core-port', 7071);
const NO_BROWSER = args.includes('--no-browser');
const SITE_PORT = +opt('web-port', 8080);
const NO_WEB = args.includes('--no-web');

// ---------------------------------------------------------------- WSL: build + start pap-core --sim
function pickDistro() {
  const want = opt('distro', process.env.PAP_WSL_DISTRO || '');
  if (want) return want;
  try {
    const list = execFileSync('wsl.exe', ['-l', '-q']).toString('utf16le').replace(/\0/g, '').split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
    return list.find((d) => /ubuntu/i.test(d)) || list.find((d) => !/docker/i.test(d)) || '';
  } catch {
    return '';
  }
}

function toWsl(p) {
  const m = /^([A-Za-z]):[\\/](.*)$/.exec(p);
  return m ? `/mnt/${m[1].toLowerCase()}/${m[2].replace(/\\/g, '/')}` : p.replace(/\\/g, '/');
}

const repo = path.resolve(__dirname, '..', '..');
const core = toWsl(path.join(repo, 'pi', 'core'));
const examples = toWsl(path.join(repo, 'pap-syntax'));
const distro = pickDistro();
const sh = [
  'set -e',
  'command -v g++ >/dev/null && command -v make >/dev/null || { echo "[preview] WSL needs g++ and make: sudo apt install build-essential"; exit 1; }',
  'D=$HOME/.pap-preview; mkdir -p $D/data/jobs',
  `cp -n '${examples}'/*.pap $D/data/jobs/ 2>/dev/null || true`,
  `cd '${core}'`,
  'echo "[preview] building pap-core in WSL (the first time takes ~2 minutes) ..."',
  'make -j$(nproc) BUILD=$D/build > $D/build.log 2>&1 || { tail -30 $D/build.log; exit 1; }',
  'echo "[preview] starting the simulated machine"',
  `PAP_SIM_SPEED=5 PAP_SIM_FEEDERS=40,110,180,250 $D/build/pap-core --sim --data $D/data --api-port ${CORE_PORT} > $D/core.log 2>&1 & P=$!`,
  // pap-core stops when this script ends (its stdin pipe closes)
  'read -r _ || true; kill $P 2>/dev/null || true',
].join('\n');
const wslArgs = (distro ? ['-d', distro] : []).concat(['--exec', 'bash', '-lc', sh]); // --exec: no extra shell expanding $ first
console.log(`[preview] WSL ${distro || '(default distro)'}`);
const wsl = spawn('wsl.exe', wslArgs, { stdio: ['pipe', 'inherit', 'inherit'] });
wsl.on('exit', (code) => {
  if (code) console.log(`[preview] WSL stopped (code ${code})`);
  process.exit(code || 0);
});
let site = null; // the website process
const quit = () => {
  try {
    if (site) site.kill();
  } catch {}
  try {
    wsl.stdin.end();
  } catch {}
  setTimeout(() => process.exit(0), 800);
};
process.on('SIGINT', quit);
process.on('SIGTERM', quit);

// ---------------------------------------------------------------- link to pap-core (text protocol, one command at a time)
let sock = null;
let buf = '';
let records = [];
const queue = [];
let busy = null;

function connect() {
  const s = net.createConnection({ host: '127.0.0.1', port: CORE_PORT });
  s.setEncoding('utf8');
  s.on('connect', () => {
    sock = s;
    console.log('[preview] connected to the simulated machine');
    pump();
    startWebsite();
  });
  s.on('data', (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i).replace(/\r$/, '');
      buf = buf.slice(i + 1);
      if (!line || line[0] === '!' || !busy) continue;
      if (line === 'ok' || line.startsWith('ok ') || line.startsWith('error')) {
        const b = busy;
        busy = null;
        b.resolve({ ok: !line.startsWith('error'), error: line.slice(6), records });
        records = [];
        pump();
      } else records.push(line);
    }
  });
  s.on('error', () => {});
  s.on('close', () => {
    sock = null;
    if (busy) busy.resolve({ ok: false, error: 'offline', records: [] });
    busy = null;
    setTimeout(connect, 1000);
  });
}

function pump() {
  if (busy || !sock || !queue.length) return;
  busy = queue.shift();
  records = [];
  sock.write(busy.line + '\n');
}

function send(line) {
  return new Promise((resolve) => {
    if (!sock) return resolve({ ok: false, error: 'offline', records: [] });
    queue.push({ line, resolve });
    pump();
  });
}

// ---------------------------------------------------------------- the website (pi/web) against the simulated machine
const open = (link) => !NO_BROWSER && spawn('cmd.exe', ['/c', 'start', '', link], { stdio: 'ignore', detached: true }).unref();

function startWebsite() {
  if (NO_WEB || site) return;
  const web = path.join(repo, 'pi', 'web');
  const main = path.join(web, 'dist', 'server.js');
  if (!fs.existsSync(main)) {
    console.log('[preview] website not built: cd pi\\web && npm install && npm run build   (the display preview runs anyway)');
    return;
  }
  // The simulated machine's data folder (in WSL): the website password is the same one the display shows
  let data = path.join(os.tmpdir(), 'pap-preview-web');
  try {
    const home = execFileSync('wsl.exe', (distro ? ['-d', distro] : []).concat(['--exec', 'sh', '-c', 'echo $HOME'])).toString().trim();
    const unc = path.win32.join('\\\\wsl.localhost', distro, ...home.split('/').filter(Boolean), '.pap-preview', 'data');
    if (distro && home && fs.existsSync(unc)) data = unc;
  } catch {}
  fs.mkdirSync(data, { recursive: true });
  site = spawn(process.execPath, [main], {
    cwd: web,
    stdio: ['ignore', 'inherit', 'inherit'],
    env: {
      ...process.env,
      PAP_WEB_CONFIG: path.join(web, 'preview-none.json'), // not the Pi's config.json (port 80)
      PAP_WEB_PORT: String(SITE_PORT),
      PAP_WEB_BIND: '127.0.0.1',
      PAP_CORE_HOST: '127.0.0.1',
      PAP_CORE_PORT: String(CORE_PORT),
      PAP_DATA: data,
    },
  });
  site.on('exit', (code) => {
    console.log(`[preview] website stopped (code ${code}); port ${SITE_PORT} in use? try --web-port 8081`);
  });
  setTimeout(() => {
    let pw = '';
    try {
      pw = (/^password\s*=\s*(.*)$/m.exec(fs.readFileSync(path.join(data, 'web.txt'), 'utf8')) || [])[1] || '';
    } catch {}
    console.log(`[preview] website: http://localhost:${SITE_PORT}/   password: ${pw || '(on the display: Settings > Network & website)'}`);
    open(`http://localhost:${SITE_PORT}/`);
  }, 1500);
}

// ---------------------------------------------------------------- web page
const PAGE = `<!doctype html><html><head><meta charset="utf-8"><title>Pick &amp; Place - display preview</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; background: #16181d; color: #ddd; font: 14px Segoe UI, sans-serif; display: flex; gap: 28px; padding: 24px; flex-wrap: wrap; align-items: flex-start; }
  #frame { background: #2a2d34; padding: 18px; border-radius: 18px; box-shadow: 0 10px 40px #0008; }
  #scr { display: block; width: 960px; height: 640px; background: #000; border-radius: 4px; cursor: pointer; image-rendering: auto; touch-action: none; user-select: none; }
  #side { width: 260px; display: grid; gap: 16px; }
  h1 { font-size: 18px; margin: 0; }
  .dim { color: #8a8f99; line-height: 1.5; }
  #stick { width: 180px; height: 180px; border-radius: 50%; background: #23262d; border: 2px solid #3a3e47; position: relative; touch-action: none; cursor: grab; }
  #knob { width: 64px; height: 64px; border-radius: 50%; background: #2dd4bf; position: absolute; left: 58px; top: 58px; box-shadow: 0 4px 12px #0007; }
  button { background: #2a2d34; color: #eee; border: 1px solid #3a3e47; border-radius: 8px; padding: 8px 12px; font: inherit; cursor: pointer; }
  button:hover { border-color: #2dd4bf; }
  .row { display: flex; gap: 8px; flex-wrap: wrap; }
  #state { font-size: 12px; }
  kbd { background: #2a2d34; border: 1px solid #3a3e47; border-radius: 4px; padding: 0 5px; }
</style></head><body>
<div id="frame"><img id="scr" alt="display" draggable="false"></div>
<div id="side">
  <h1>Display preview</h1>
  <div class="dim">The real Pi display code, simulated machine.<br>
    Click = touch, drag = scroll<br><kbd>↑</kbd> <kbd>↓</kbd> or wheel = joystick up / down<br>
    <kbd>←</kbd> = joystick left (back)<br><kbd>Enter</kbd> = joystick button (hold = hold)<br><kbd>Esc</kbd> = back</div>
  <div><div class="dim" style="margin-bottom:6px">Joystick (drag; on Move it jogs)</div><div id="stick"><div id="knob"></div></div></div>
  <div class="row"><button id="b1">1 : 1</button><button id="b2">2 ×</button><button id="shot">Save picture</button></div>
  <div id="state" class="dim">connecting …</div>
</div>
<script>
const img = document.getElementById('scr'), state = document.getElementById('state');
const post = (line) => fetch('/input', { method: 'POST', body: line }).catch(() => {});
let frames = 0, last = performance.now();
async function loop() {
  try {
    const r = await fetch('/frame', { cache: 'no-store' });
    if (r.ok) {
      const url = URL.createObjectURL(await r.blob());
      img.onload = () => URL.revokeObjectURL(url);
      img.src = url;
      frames++;
    } else state.textContent = 'waiting for the simulated machine …';
  } catch { state.textContent = 'preview server stopped'; }
  const now = performance.now();
  if (now - last > 1000) { state.textContent = frames + ' frames / s'; frames = 0; last = now; }
  setTimeout(loop, 40);
}
loop();
// touch
const pos = (e) => { const b = img.getBoundingClientRect(); return [Math.round((e.clientX - b.left) / b.width * 480), Math.round((e.clientY - b.top) / b.height * 320)]; };
let down = false, lastMove = 0;
img.addEventListener('pointerdown', (e) => { down = true; img.setPointerCapture(e.pointerId); post('SCREEN TOUCH ' + pos(e).join(' ')); });
img.addEventListener('pointermove', (e) => { if (!down || performance.now() - lastMove < 30) return; lastMove = performance.now(); const [x, y] = pos(e); post('SCREEN TOUCH ' + Math.max(0, Math.min(479, x)) + ' ' + Math.max(0, Math.min(319, y))); });
const up = () => { if (down) { down = false; post('SCREEN RELEASE'); } };
img.addEventListener('pointerup', up); img.addEventListener('pointercancel', up);
img.addEventListener('wheel', (e) => { e.preventDefault(); post('SCREEN KEY ' + (e.deltaY > 0 ? 'down' : 'up')); }, { passive: false });
// joystick
let sx = 0, sy = 0, sb = 0;
const stickSend = () => post('SCREEN STICK ' + sx.toFixed(2) + ' ' + sy.toFixed(2) + ' ' + sb);
const stick = document.getElementById('stick'), knob = document.getElementById('knob');
const knobAt = () => { knob.style.left = (58 + sx * 58) + 'px'; knob.style.top = (58 - sy * 58) + 'px'; };
let dragging = false;
const fromPointer = (e) => { const b = stick.getBoundingClientRect(); let x = (e.clientX - b.left - 90) / 70, y = -(e.clientY - b.top - 90) / 70; const m = Math.hypot(x, y); if (m > 1) { x /= m; y /= m; } sx = x; sy = y; knobAt(); stickSend(); };
stick.addEventListener('pointerdown', (e) => { dragging = true; stick.setPointerCapture(e.pointerId); fromPointer(e); });
stick.addEventListener('pointermove', (e) => { if (dragging) fromPointer(e); });
const release = () => { if (!dragging) return; dragging = false; sx = sy = 0; knobAt(); stickSend(); };
stick.addEventListener('pointerup', release); stick.addEventListener('pointercancel', release);
const keys = new Set();
const keyStick = () => { sx = (keys.has('ArrowRight') ? 1 : 0) - (keys.has('ArrowLeft') ? 1 : 0); sy = (keys.has('ArrowUp') ? 1 : 0) - (keys.has('ArrowDown') ? 1 : 0); knobAt(); stickSend(); };
addEventListener('keydown', (e) => {
  if (e.repeat) { if (e.key.startsWith('Arrow') || e.key === 'Enter') e.preventDefault(); return; }
  if (e.key.startsWith('Arrow')) { keys.add(e.key); keyStick(); e.preventDefault(); }
  else if (e.key === 'Enter' || e.key === ' ') { sb = 1; stickSend(); e.preventDefault(); }
  else if (e.key === 'Escape' || e.key === 'Backspace') { post('SCREEN KEY hold'); e.preventDefault(); }
});
addEventListener('keyup', (e) => {
  if (e.key.startsWith('Arrow')) { keys.delete(e.key); keyStick(); }
  else if (e.key === 'Enter' || e.key === ' ') { sb = 0; stickSend(); }
});
// size, picture
document.getElementById('b1').onclick = () => { img.style.width = '480px'; img.style.height = '320px'; };
document.getElementById('b2').onclick = () => { img.style.width = '960px'; img.style.height = '640px'; };
document.getElementById('shot').onclick = () => { const a = document.createElement('a'); a.href = '/frame?q=98'; a.download = 'display-' + Date.now() + '.jpg'; a.click(); };
</script></body></html>`;

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (req.method === 'GET' && url.pathname === '/') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    return res.end(PAGE);
  }
  if (req.method === 'GET' && url.pathname === '/frame') {
    const q = Math.max(30, Math.min(100, +(url.searchParams.get('q') || 92)));
    const r = await send('SCREEN q=' + q);
    const rec = r.ok && r.records.find((l) => l.startsWith('screen '));
    const m = rec && /jpeg=(\S+)/.exec(rec);
    if (!m) {
      res.writeHead(503, { 'Content-Type': 'text/plain' });
      return res.end(r.error || 'no picture yet');
    }
    res.writeHead(200, { 'Content-Type': 'image/jpeg', 'Cache-Control': 'no-store' });
    return res.end(Buffer.from(m[1], 'base64'));
  }
  if (req.method === 'POST' && url.pathname === '/input') {
    let body = '';
    req.on('data', (d) => (body += d));
    req.on('end', async () => {
      const line = body.trim();
      // only display input: the preview never sends machine commands
      if (!/^SCREEN (TOUCH -?\d+ -?\d+|RELEASE|KEY (up|down|press|hold)|STICK -?[\d.]+ -?[\d.]+ [01])$/.test(line)) {
        res.writeHead(400).end();
        return;
      }
      const r = await send(line);
      res.writeHead(r.ok ? 204 : 503).end();
    });
    return;
  }
  res.writeHead(404).end();
});

server.on('error', (e) => {
  console.log('[preview] ' + e.message + ' (another preview running? try --port 8091)');
  quit();
});
server.listen(WEB_PORT, '127.0.0.1', () => {
  const link = `http://127.0.0.1:${WEB_PORT}/`;
  console.log(`[preview] open ${link}   (Ctrl+C here stops everything)`);
  connect();
  open(link);
});

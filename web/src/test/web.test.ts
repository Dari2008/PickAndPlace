// node --test dist/test/  (no pap-core needed: a fake core speaks the text protocol)
import assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as net from 'node:net';
import * as os from 'node:os';
import * as path from 'node:path';
import { test } from 'node:test';
import { cookieToken, readLogin, setPassword, token } from '../auth';
import { CoreLink, defaultTimeout } from '../core';
import { crc32, parseRecord, uploadLines } from '../record';
import { startServer } from '../server';
import { decodeFrame, encodeFrame } from '../ws';

test('records parse like the C++ parser', () => {
  assert.deepEqual(parseRecord('feeder num=3 name="Tape 8 mm" pos=12.5'), { type: 'feeder', f: { num: '3', name: 'Tape 8 mm', pos: '12.5' } });
  assert.deepEqual(parseRecord('!status x=1 homed=1'), { type: 'status', f: { x: '1', homed: '1' } });
  assert.deepEqual(parseRecord('|[board] x=1'), { type: '|', text: '[board] x=1' });
  assert.deepEqual(parseRecord('set key= flag'), { type: 'set', f: { key: '', flag: '' } });
});

test('crc32 and upload lines', () => {
  assert.equal((crc32('123456789') ^ 0xffffffff) >>> 0, 0xcbf43926); // running value, final XOR by the caller
  assert.deepEqual(uploadLines('a\r\nb\n'), ['a', 'b']);
  assert.deepEqual(uploadLines('a\n\n'), ['a', '']);
  assert.deepEqual(uploadLines('a'), ['a']);
  assert.deepEqual(uploadLines(''), []);
  assert.equal(defaultTimeout('home'), 330_000);
  assert.equal(defaultTimeout('CAL BOTTOM'), 120_000);
});

test('websocket frames: masked in, plain out, long lengths', () => {
  for (const n of [5, 300, 70000]) {
    const payload = Buffer.alloc(n, 0x61);
    const out = decodeFrame(encodeFrame(1, payload))!;
    assert.equal(out.payload.length, n);
    // client frame: masked
    const mask = Buffer.from([1, 2, 3, 4]);
    const plain = encodeFrame(1, payload);
    const hlen = n < 126 ? 2 : n < 65536 ? 4 : 10;
    const head = Buffer.from(plain.subarray(0, hlen));
    head[1] |= 0x80;
    const body = Buffer.from(payload.map((b, i) => b ^ mask[i & 3]));
    const f = decodeFrame(Buffer.concat([head, mask, body]))!;
    assert.ok(f.payload.equals(payload));
    assert.equal(f.size, hlen + 4 + n);
    assert.equal(decodeFrame(Buffer.concat([head, mask, body.subarray(0, n - 1)])), null);
  }
});

test('login file, token and cookie', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'papweb-'));
  const a = readLogin(dir);
  assert.equal(a.password.length, 8);
  assert.deepEqual(readLogin(dir), a);
  setPassword(dir, 'hello');
  const b = readLogin(dir);
  assert.equal(b.password, 'hello');
  assert.notEqual(b.secret, a.secret);
  assert.equal(cookieToken(`x=1; pap=${token(b)}; y=2`), token(b));
});

// A fake pap-core: answers STATUS / REPORT / JOB / SET, checks uploads, sends events
function fakeCore(): Promise<{ port: number; server: net.Server; saved: Map<string, string>; kick: () => void; stop: () => void }> {
  const saved = new Map<string, string>();
  const socks = new Set<net.Socket>();
  const server = net.createServer((s) => {
    socks.add(s);
    s.on('close', () => socks.delete(s));
    let buf = '';
    let up: { path: string; text: string; crc: number } | null = null;
    let timer: NodeJS.Timeout | null = null;
    s.on('data', (d) => {
      buf += d.toString();
      let i: number;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i);
        buf = buf.slice(i + 1);
        if (up) {
          if (line.startsWith('|')) {
            up.text += line.slice(1) + '\n';
            up.crc = crc32(line.slice(1) + '\n', up.crc);
            s.write('ok\n');
          } else if (line.startsWith('END ')) {
            const want = ((up.crc ^ 0xffffffff) >>> 0).toString(16).toUpperCase().padStart(8, '0');
            if (line.slice(4) === want) {
              saved.set(up.path, up.text);
              s.write(`saved path=${up.path}\nok\n`);
            } else s.write('error CRC mismatch\n');
            up = null;
          }
          continue;
        }
        const w = line.split(' ');
        if (w[0] === 'REPORT') {
          if (timer) clearInterval(timer);
          timer = setInterval(() => !s.destroyed && s.write('!status state=idle x=1.00\n'), 100);
          s.on('close', () => timer && clearInterval(timer));
          s.write('ok\n');
        } else if (w[0] === 'STATUS') s.write('status state=idle x=1.00 y=2.00\nok\n');
        else if (w[0] === 'JOB') s.write('job state=idle\nok\n');
        else if (w[0] === 'SET') s.write(`set key=${w[1]} value=${w[2]}\n!changed what=settings\nok\n`);
        else if (w[0] === 'UPLOAD') {
          up = { path: w[1], text: '', crc: 0xffffffff };
          s.write('ok\n');
        } else s.write(`error unknown command ${w[0]}\n`);
      }
    });
  });
  return new Promise((resolve) =>
    server.listen(0, '127.0.0.1', () =>
      resolve({
        port: (server.address() as net.AddressInfo).port,
        server,
        saved,
        kick: () => socks.forEach((s) => s.destroy()),
        stop: () => {
          socks.forEach((s) => s.destroy());
          server.close();
        },
      }),
    ),
  );
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

test('core link: replies, events, upload with CRC, reconnect', async () => {
  const fc = await fakeCore();
  const link = new CoreLink('127.0.0.1', fc.port, 100);
  const events: string[] = [];
  link.on('event', (r) => events.push(r.type));
  const st = await link.send('STATUS');
  assert.equal(st.ok, true);
  assert.equal(st.records[0].f!.y, '2.00');
  const bad = await link.send('NOPE');
  assert.equal(bad.ok, false);
  assert.match(bad.error, /unknown command/);
  const set = await link.send('SET beep 0');
  assert.equal(set.records.length, 1); // the event is not part of the reply
  const text = '[board]\r\nname = test\n\n[parts]\n';
  let progress = 0;
  const up = await link.upload('/jobs/a.pap', text, (d) => (progress = d));
  assert.equal(up.ok, true, up.error);
  assert.equal(fc.saved.get('/jobs/a.pap'), '[board]\nname = test\n\n[parts]\n');
  assert.equal(progress, 29); // bytes of the sent lines, each with its newline
  await sleep(300);
  assert.ok(events.includes('status'));
  assert.ok(events.includes('changed'));
  assert.equal(link.connected(), true);
  fc.kick(); // core restart
  await sleep(50);
  const lost = await link.send('STATUS');
  assert.equal(lost.ok, true, 'reconnected and answered'); // queued until the reconnect
  link.close();
  fc.stop();
});

test('server: login, session, websocket command, upload', { skip: typeof (globalThis as any).WebSocket !== 'function' }, async () => {
  const fc = await fakeCore();
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'papweb-'));
  setPassword(dir, 'secret1');
  const pub = fs.mkdtempSync(path.join(os.tmpdir(), 'papweb-pub-'));
  fs.writeFileSync(path.join(pub, 'index.html'), '<h1>hi</h1>');
  const { server, core } = startServer({ port: 0, bind: '127.0.0.1', coreHost: '127.0.0.1', corePort: fc.port, data: dir, publicDir: pub, maxUpload: 1 << 20 });
  await new Promise((r) => server.once('listening', r));
  const base = `http://127.0.0.1:${(server.address() as net.AddressInfo).port}`;

  assert.equal((await fetch(base + '/api/session')).status, 401);
  assert.equal((await fetch(base + '/api/login', { method: 'POST', body: new URLSearchParams({ password: 'wrong' }) })).status, 401);
  const ok = await fetch(base + '/api/login', { method: 'POST', body: new URLSearchParams({ password: 'secret1' }) });
  assert.equal(ok.status, 200);
  const cookie = (ok.headers.get('set-cookie') || '').split(';')[0];
  assert.equal((await fetch(base + '/api/session', { headers: { cookie } })).status, 200);
  assert.equal(await (await fetch(base + '/')).text(), '<h1>hi</h1>');
  assert.equal((await fetch(base + '/../web.txt')).status !== 200, true);

  const WS = (globalThis as any).WebSocket;
  const ws = new WS(base.replace('http', 'ws') + '/ws', { headers: { cookie } });
  const msgs: any[] = [];
  ws.onmessage = (m: { data: string }) => msgs.push(JSON.parse(m.data));
  await new Promise((r, j) => ((ws.onopen = r), (ws.onerror = j)));
  ws.send(JSON.stringify({ t: 'cmd', id: 7, line: 'STATUS' }));
  ws.send(JSON.stringify({ t: 'sys', id: 8, op: 'info' }));
  const up = await fetch(base + '/api/upload?path=/jobs/b.pap', { method: 'POST', headers: { cookie }, body: 'x = 1\n' });
  assert.equal(up.status, 202);
  await sleep(400);
  const reply = msgs.find((m) => m.t === 'reply' && m.id === 7);
  assert.equal(reply.ok, true);
  assert.equal(reply.records[0].f.x, '1.00');
  assert.ok(msgs.find((m) => m.t === 'reply' && m.id === 8).sys.hostname);
  assert.ok(msgs.some((m) => m.t === 'ev' && m.type === 'job'));
  assert.ok(msgs.some((m) => m.t === 'upload' && m.state === 'done'));
  assert.equal(fc.saved.get('/jobs/b.pap'), 'x = 1\n');
  ws.close();
  core.close();
  server.closeAllConnections();
  server.close();
  fc.stop();
});

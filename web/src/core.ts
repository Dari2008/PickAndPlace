// Client of pap-core's command port (TCP, text protocol = protocol.md): one connection,
// one command in flight, records collected until "ok" / "error ...", "!" lines are events.
// Reconnects by itself; the same logic as the ESP32's serial link (esp32-ui/src/link).

import { EventEmitter } from 'node:events';
import * as net from 'node:net';
import { crc32, parseRecord, Rec, uploadLines } from './record';

export interface Reply {
  ok: boolean;
  error: string;
  timeout?: boolean;
  records: Rec[];
}

interface Request {
  line: string;
  timeoutMs: number;
  resolve: (r: Reply) => void;
  upload?: { lines: string[]; pos: number; crc: number; done: boolean; progress?: (done: number, total: number) => void; bytes: number; total: number };
}

export function defaultTimeout(line: string): number {
  const w = line.trim().split(/\s+/)[0].toUpperCase();
  if (w === 'HOME') return 330_000;
  if (w === 'CAL') return 120_000;
  if (['RUN', 'CHECK', 'LS', 'FEEDERS', 'CAT', 'SD', 'SCAN'].includes(w)) return 30_000;
  if (w === 'WAIT') return 600_000;
  return 10_000;
}

export class CoreLink extends EventEmitter {
  private sock: net.Socket | null = null;
  private buf = '';
  private queue: Request[] = [];
  private cur: Request | null = null;
  private records: Rec[] = [];
  private timer: NodeJS.Timeout | null = null;
  private lastRx = 0;
  private closed = false;
  private wasConnected = false;
  private tick: NodeJS.Timeout;

  constructor(private host: string, private port: number, private reportMs = 250) {
    super();
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
  connected(): boolean {
    return this.sock !== null && Date.now() - this.lastRx < 2000;
  }

  queued(): number {
    return this.queue.length + (this.cur ? 1 : 0);
  }

  close(): void {
    this.closed = true;
    clearInterval(this.tick);
    this.sock?.destroy();
    this.failAll('closed');
  }

  send(line: string, timeoutMs = 0): Promise<Reply> {
    return new Promise((resolve) => {
      this.queue.push({ line, timeoutMs: timeoutMs || defaultTimeout(line), resolve });
      this.next();
    });
  }

  // Upload a text file line by line with CRC (UPLOAD <path> <bytes>, |line ..., END <crc>)
  upload(path: string, text: string, progress?: (done: number, total: number) => void): Promise<Reply> {
    const lines = uploadLines(text);
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

  private open(): void {
    if (this.closed) return;
    const s = net.createConnection({ host: this.host, port: this.port });
    s.setNoDelay(true);
    s.setEncoding('utf8');
    s.on('connect', () => {
      this.sock = s;
      this.buf = '';
      // Status reports first (each TCP connection is its own port on the core)
      this.queue.unshift({ line: `REPORT ${this.reportMs}`, timeoutMs: 5000, resolve: () => {} });
      this.next();
    });
    s.on('data', (d: string) => this.onData(d));
    s.on('error', () => {});
    s.on('close', () => {
      if (this.sock === s) this.sock = null;
      this.lastRx = 0;
      this.failAll('core offline');
      if (!this.closed) setTimeout(() => this.open(), 1000);
    });
  }

  private failAll(error: string): void {
    const all = this.cur ? [this.cur, ...this.queue] : [...this.queue];
    this.cur = null;
    this.queue = [];
    if (this.timer) clearTimeout(this.timer);
    for (const r of all) r.resolve({ ok: false, error, records: [] });
  }

  private write(line: string): void {
    this.sock?.write(line + '\n');
  }

  private arm(): void {
    if (this.timer) clearTimeout(this.timer);
    const cur = this.cur;
    if (!cur) return;
    this.timer = setTimeout(() => {
      if (this.cur !== cur) return;
      if (cur.upload) this.write('ABORT');
      this.finish({ ok: false, error: 'no answer from the controller', timeout: true, records: this.records });
    }, cur.timeoutMs);
  }

  private next(): void {
    if (this.cur || !this.sock || !this.queue.length) return;
    this.cur = this.queue.shift()!;
    this.records = [];
    this.write(this.cur.line);
    this.arm();
  }

  private finish(r: Reply): void {
    const cur = this.cur;
    this.cur = null;
    if (this.timer) clearTimeout(this.timer);
    cur?.resolve(r);
    this.next();
  }

  private uploadStep(): void {
    const u = this.cur!.upload!;
    if (u.pos >= u.lines.length) {
      u.done = true;
      this.write('END ' + ((u.crc ^ 0xffffffff) >>> 0).toString(16).toUpperCase().padStart(8, '0'));
      this.arm();
      return;
    }
    const line = u.lines[u.pos++];
    u.crc = crc32(line + '\n', u.crc);
    u.bytes += Buffer.byteLength(line) + 1;
    this.write('|' + line);
    this.arm();
    u.progress?.(u.bytes, u.total);
  }

  private onData(d: string): void {
    this.buf += d;
    let i: number;
    while ((i = this.buf.indexOf('\n')) >= 0) {
      let line = this.buf.slice(0, i);
      this.buf = this.buf.slice(i + 1);
      if (line.endsWith('\r')) line = line.slice(0, -1);
      this.lastRx = Date.now();
      if (line) this.onLine(line);
    }
  }

  private onLine(line: string): void {
    if (line[0] === '!') {
      const rec = parseRecord(line);
      this.emit('event', rec);
      return;
    }
    const cur = this.cur;
    if (!cur) return; // e.g. the "ok" of REPORT
    const isOk = line === 'ok' || line.startsWith('ok ');
    if (line.startsWith('error')) {
      this.finish({ ok: false, error: line.length > 6 ? line.slice(6) : 'error', records: this.records });
      return;
    }
    if (cur.upload && !cur.upload.done) {
      if (isOk) this.uploadStep();
      return;
    }
    if (isOk) this.finish({ ok: true, error: '', records: this.records });
    else this.records.push(parseRecord(line));
  }
}

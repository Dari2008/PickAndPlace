// Website login: the password and a secret live in <data>/web.txt, shared with pap-core
// (the display shows the password, Settings -> Network). The cookie holds
// sha256(secret + ":" + password): a new password logs every browser out.

import { createHash, randomBytes } from 'node:crypto';
import * as fs from 'node:fs';
import * as path from 'node:path';

export interface WebLogin {
  password: string;
  secret: string;
}

export function webFile(dataDir: string): string {
  return path.join(dataDir, 'web.txt');
}

function write(file: string, l: WebLogin): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = file + '.tmp';
  fs.writeFileSync(tmp, `# website login (pap-web reads this file)\npassword = ${l.password}\nsecret = ${l.secret}\n`);
  fs.renameSync(tmp, file);
}

function randomLetters(n: number): string {
  return Array.from(randomBytes(n), (b) => String.fromCharCode(97 + (b % 26))).join('');
}

// Read (and create when missing) the login file
export function readLogin(dataDir: string): WebLogin {
  const file = webFile(dataDir);
  const l: WebLogin = { password: '', secret: '' };
  try {
    for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
      const eq = line.indexOf('=');
      if (eq < 0 || line.trimStart().startsWith('#')) continue;
      const k = line.slice(0, eq).trim(), v = line.slice(eq + 1).trim();
      if (k === 'password') l.password = v;
      else if (k === 'secret') l.secret = v;
    }
  } catch {
    /* created below */
  }
  if (!l.password || !l.secret) {
    if (!l.password) l.password = randomLetters(8);
    if (!l.secret) l.secret = randomBytes(16).toString('hex');
    write(file, l);
  }
  return l;
}

export function setPassword(dataDir: string, password: string): void {
  write(webFile(dataDir), { password, secret: randomBytes(16).toString('hex') });
}

export function token(l: WebLogin): string {
  return createHash('sha256').update(l.secret + ':' + l.password).digest('hex');
}

export function cookieToken(cookieHeader: string | undefined): string {
  for (const part of (cookieHeader || '').split(';')) {
    const [k, ...v] = part.trim().split('=');
    if (k === 'pap') return v.join('=');
  }
  return '';
}

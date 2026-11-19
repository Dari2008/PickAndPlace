// Records of the controller protocol (protocol.md): `type key=value key="quoted value"`,
// events start with '!', data lines with '|'. Same rules as pi/core/link/record.cpp.

export interface Rec {
  type: string;
  f?: Record<string, string>; // fields (not for '|' lines)
  text?: string; // '|' lines: the line without '|'
}

export function parseRecord(line: string): Rec {
  if (line.startsWith('|')) return { type: '|', text: line.slice(1) };
  let i = line.startsWith('!') ? 1 : 0;
  const n = line.length;
  let type = '';
  while (i < n && line[i] !== ' ') type += line[i++];
  const f: Record<string, string> = {};
  while (i < n) {
    while (i < n && line[i] === ' ') i++;
    if (i >= n) break;
    let key = '';
    while (i < n && line[i] !== '=' && line[i] !== ' ') key += line[i++];
    let value = '';
    if (i < n && line[i] === '=') {
      i++;
      if (i < n && line[i] === '"') {
        i++;
        while (i < n && line[i] !== '"') value += line[i++];
        i++; // closing quote
      } else while (i < n && line[i] !== ' ') value += line[i++];
    }
    f[key] = value;
  }
  return { type, f };
}

// CRC-32 (IEEE) of the upload text as the controller computes it: every line + '\n'
export function crc32(data: Buffer | string, crc = 0xffffffff): number {
  const b = typeof data === 'string' ? Buffer.from(data, 'utf8') : data;
  for (const c of b) {
    crc ^= c;
    for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return crc >>> 0;
}

// Lines of an uploaded file as they are sent ('\r' removed, no empty last line)
export function uploadLines(text: string): string[] {
  if (text === '') return [];
  const lines = text.split('\n').map((l) => (l.endsWith('\r') ? l.slice(0, -1) : l));
  if (lines.length && lines[lines.length - 1] === '' && text.endsWith('\n')) lines.pop();
  return lines;
}

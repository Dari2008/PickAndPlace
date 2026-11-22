// Pick & Place website: a thin client over the controller protocol (protocol.md),
// sent through pap-web's WebSocket to pap-core on the Raspberry Pi. See ../src/server.ts for the messages.
'use strict';

const $ = (s, el = document) => el.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const q = (s) => (/[\s"=]/.test(s) || s === '' ? '"' + String(s).replace(/"/g, "'") + '"' : s);

const state = {
  link: false,
  status: {},
  job: {},
  scan: null,
  message: '',
  view: 'dash',
  cwd: '/',
  feeders: [],
  uploads: {},
};

// ---------------------------------------------------------------- WebSocket
let ws = null;
let nextId = 1;
const pending = new Map();
const eventLog = []; // for the console

function connect() {
  ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
  ws.onmessage = (m) => onMessage(JSON.parse(m.data));
  ws.onclose = () => {
    setLink(false);
    for (const [, p] of pending) p.resolve({ ok: false, error: 'connection lost', records: [] });
    pending.clear();
    setTimeout(checkSession, 1500);
  };
}

function send(obj) {
  return new Promise((resolve) => {
    if (!ws || ws.readyState !== 1) return resolve({ ok: false, error: 'not connected', records: [] });
    obj.id = nextId++;
    pending.set(obj.id, { resolve });
    ws.send(JSON.stringify(obj));
  });
}

// Run a protocol command; records come back as {type, f: {...}} / {type: '|', text}.
async function cmd(line, { quiet = false } = {}) {
  logConsole('> ' + line, 'out');
  const r = await send({ t: 'cmd', line });
  if (!r.ok) {
    logConsole('error ' + r.error, 'err');
    if (!quiet) toast(r.error || 'error');
  } else {
    for (const rec of r.records) logConsole(rec.type === '|' ? '|' + rec.text : rec.type + ' ' + Object.entries(rec.f).map(([k, v]) => `${k}=${q(v)}`).join(' '));
    logConsole('ok');
  }
  return r;
}
const sys = (op, value) => send({ t: 'sys', op, value });

function onMessage(m) {
  if (m.t === 'reply') {
    const p = pending.get(m.id);
    if (p) { pending.delete(m.id); p.resolve(m); }
  } else if (m.t === 'link') {
    setLink(m.connected);
  } else if (m.t === 'ev') {
    onEvent(m.type, m.f || {});
  } else if (m.t === 'upload') {
    state.uploads[m.path] = m;
    if (m.state === 'done') { toast('Uploaded ' + m.path); if (state.view === 'jobs') renderJobs(); }
    if (m.state === 'error') toast('Upload failed: ' + m.error);
    renderUploads();
  }
}

// Calibrate -> Homing (sensorless): load values while Measure runs
let sgLive = null;
function showSgLive(f) {
  const el = document.getElementById('sgLive');
  if (!el || !sgLive) return;
  const v = [+f.v, +f.v2];
  v.forEach((x, k) => { if (x >= 0) { sgLive.min[k] = Math.min(sgLive.min[k], x); sgLive.max[k] = Math.max(sgLive.max[k], x); } });
  const side = (k, name) => `${name} <b>${v[k]}</b> <span class="dim">(min ${sgLive.min[k]} · max ${sgLive.max[k]})</span>`;
  el.innerHTML = side(0, f.axis.toUpperCase()) + (v[1] >= 0 ? ' &nbsp; ' + side(1, 'X2') : '') + ` &nbsp; <span class="dim">${f.mm} mm</span>`;
}

function onEvent(type, f) {
  if (type === 'sg') return showSgLive(f); // SGTEST live values: no console spam, no full refresh
  if (type !== 'status' && type !== 'job') logConsole('!' + type + ' ' + Object.entries(f).map(([k, v]) => `${k}=${q(v)}`).join(' '), 'ev');
  if (type === 'status') { state.status = f; setLink(true); }
  else if (type === 'job') { state.job = f; if (f.state !== 'message') state.message = ''; }
  else if (type === 'message') { state.message = f.text; showMessage(); }
  else if (type === 'scan') { state.scan = { ...(state.scan || {}), ...f, found: f.num ? [...((state.scan || {}).found || []), f] : ((state.scan || {}).found || []) }; if (f.state === 'done' || f.state === 'failed') loadFeeders(); }
  else if (type === 'alarm') showAlarm(f.text);
  else if (type === 'beep') beep(+f.ms || 100);
  else if (type === 'ready') toast('Controller restarted');
  else if (type === 'changed' && f.what === 'nozzles' && state.view === 'nozzles') renderNozzles();
  refresh();
}

function setLink(on) {
  state.link = on;
  const pill = $('#linkPill');
  const st = state.status.state || 'offline';
  pill.textContent = on ? st : 'controller offline';
  pill.className = 'pill ' + (!on ? 'bad' : st === 'alarm' ? 'bad' : st === 'idle' ? 'ok' : 'accent');
}

// ---------------------------------------------------------------- helpers
function toast(text) {
  const t = $('#toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (t.hidden = true), 3000);
}

function modal(html) {
  $('#modalBody').innerHTML = html;
  $('#modal').hidden = false;
  return $('#modalBody');
}
function closeModal() { $('#modal').hidden = true; }

// Live camera window: stays in the corner (also over pop-ups) while you jog, teach or measure.
// Each frame is one GET /api/cam; the next is asked for when the last arrived.
const liveCam = { cam: null, run: 0, url: null };
function openLive(cam) {
  let box = document.getElementById('live');
  if (!box) {
    box = document.createElement('div');
    box.id = 'live';
    box.className = 'live card';
    box.innerHTML = `<div class="row spread"><b id="liveTitle"></b><span class="row">
      <button class="small" id="liveTop">Top</button><button class="small" id="liveBottom">Bottom</button>
      <button class="small" id="liveClose" title="Close">✕</button></span></div>
      <img id="liveImg" alt=""><div id="liveMsg" class="dim"></div>`;
    document.body.appendChild(box);
    $('#liveTop', box).onclick = () => openLive('top');
    $('#liveBottom', box).onclick = () => openLive('bottom');
    $('#liveClose', box).onclick = closeLive;
  }
  box.hidden = false;
  liveCam.cam = cam;
  camLights(cam);
  $('#liveTitle', box).textContent = cam === 'top' ? 'Top camera' : 'Bottom camera';
  $('#liveTop', box).className = 'small' + (cam === 'top' ? ' primary' : '');
  $('#liveBottom', box).className = 'small' + (cam === 'bottom' ? ' primary' : '');
  const run = ++liveCam.run;
  (async () => {
    while (liveCam.run === run) {
      try {
        const r = await fetch(`/api/cam?cam=${cam}&t=${Date.now()}`, { cache: 'no-store' });
        if (liveCam.run !== run) break;
        if (!r.ok) {
          const j = await r.json().catch(() => ({}));
          $('#liveMsg', box).textContent = 'no picture' + (j.error ? ': ' + j.error : '') + ' (machine busy?)';
          await new Promise((res) => setTimeout(res, 1000));
          continue;
        }
        const url = URL.createObjectURL(await r.blob());
        if (liveCam.run !== run) { URL.revokeObjectURL(url); break; }
        $('#liveImg', box).src = url;
        if (liveCam.url) URL.revokeObjectURL(liveCam.url);
        liveCam.url = url;
        $('#liveMsg', box).textContent = '';
        await new Promise((res) => setTimeout(res, 120));
      } catch {
        $('#liveMsg', box).textContent = 'no connection';
        await new Promise((res) => setTimeout(res, 1000));
      }
    }
  })();
}
// Light of the camera being looked at: bottom ring (calibration colour; part: part colour), top light at
// light_top, or none (ring off, top light back to idle)
function camLights(cam, part = false) {
  if (cam === 'bottom') return send({ t: 'cmd', line: part ? 'LIGHT bottom PART' : 'LIGHT bottom ON' });
  send({ t: 'cmd', line: 'LIGHT bottom OFF' });
  return send({ t: 'cmd', line: cam === 'top' ? 'LIGHT top ON' : 'LIGHT top IDLE' });
}

function closeLive() {
  camLights(null);
  liveCam.run++;
  const box = document.getElementById('live');
  if (box) box.hidden = true;
}
$('#modal').addEventListener('click', (e) => { if (e.target.id === 'modal') closeModal(); });

function confirmBox(text, okLabel = 'OK', danger = false) {
  return new Promise((resolve) => {
    const m = modal(`<p>${esc(text)}</p><div class="row" style="justify-content:flex-end"><button id="cNo">Cancel</button><button id="cYes" class="${danger ? 'danger' : 'primary'}">${esc(okLabel)}</button></div>`);
    $('#cNo', m).onclick = () => { closeModal(); resolve(false); };
    $('#cYes', m).onclick = () => { closeModal(); resolve(true); };
  });
}

let audio;
function beep(ms) {
  try {
    audio = audio || new AudioContext();
    const o = audio.createOscillator();
    o.frequency.value = 2400;
    o.connect(audio.destination);
    o.start();
    o.stop(audio.currentTime + ms / 1000);
  } catch (e) { /* no audio */ }
}

const fmtTime = (s) => {
  s = +s;
  if (!(s >= 0)) return '–';
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(sec).padStart(2, '0');
};
const join = (dir, name) => (dir === '/' ? '' : dir) + '/' + name;

function showAlarm(text) {
  const a = $('#alarm');
  a.innerHTML = `<b>Alarm</b><span>${esc(text)}</span><button class="small" id="clearAlarm">Clear</button>`;
  a.hidden = false;
  $('#clearAlarm').onclick = async () => { await cmd('CLEAR'); a.hidden = true; };
}

function showMessage() {
  if (!state.message) return;
  const m = modal(`<h2>Job is waiting</h2><p class="big">${esc(state.message)}</p><div class="row" style="justify-content:flex-end"><button id="mStop" class="ghost">Stop job</button><button id="mCont" class="primary">Continue</button></div>`);
  $('#mCont', m).onclick = () => { cmd('CONTINUE'); closeModal(); };
  $('#mStop', m).onclick = async () => { if (await confirmBox('Stop the job?', 'Stop', true)) cmd('STOP'); };
}

// ---------------------------------------------------------------- views
const views = { dash: renderDash, jobs: renderJobs, feeders: renderFeeders, move: renderMove, nozzles: renderNozzles, cameras: renderCameras, calibrate: renderCalibrate, display: renderDisplay, settings: renderSettings, console: renderConsole };

function show(view) {
  state.view = view;
  document.querySelectorAll('#nav a').forEach((a) => a.classList.toggle('active', a.dataset.view === view));
  clearInterval(show.poll);
  show.gen = (show.gen || 0) + 1; // stops the picture loops of the view left
  views[view]();
}
document.querySelectorAll('#nav a').forEach((a) => (a.onclick = () => show(a.dataset.view)));

// Parts of the page that follow the live status.
function refresh() {
  if (state.view === 'dash') updateDash();
  if (state.view === 'move') updateMove();
  if (state.view === 'cameras') updateCameras();
  if (state.view === 'calibrate') updateCalibrate();
  if (state.view === 'feeders') updateScan();
}

// ---- Dashboard ----------------------------------------------------------------
function renderDash() {
  $('#view').innerHTML = `
  <div class="grid">
    <div class="card" id="jobCard"></div>
    <div class="card">
      <h2>Machine</h2>
      <div class="axisbox" id="axes"></div>
      <div class="row" style="margin-top:12px">
        <button id="dHome">Home all</button>
        <button id="dMotors">Motors off</button>
      </div>
    </div>
    <div class="card">
      <h2>Outputs</h2>
      <div class="kv" id="outputs"></div>
    </div>
  </div>`;
  $('#dHome').onclick = () => cmd('HOME');
  $('#dMotors').onclick = () => cmd('MOTORS OFF');
  updateDash();
}

function phasePill(phase) {
  const cls = { paste: 'paste', pick: 'accent', place: 'accent', wait: 'warn', home: 'warn' }[phase] || '';
  const label = { paste: 'PASTE', pick: 'PLACE · pick', place: 'PLACE', func: 'function', home: 'HOME', wait: 'WAIT' }[phase] || phase;
  return phase ? `<span class="pill ${cls}">${esc(label)}</span>` : '';
}

function updateDash() {
  const j = state.job, s = state.status;
  const active = ['running', 'paused', 'message'].includes(j.state);
  const jc = $('#jobCard');
  if (!jc) return;
  if (!active) {
    jc.innerHTML = `<h2>Job</h2>
      <p class="dim">${j.state && j.state !== 'idle' ? `Last job <b>${esc(j.name)}</b>: ${esc(j.state)} (${j.done}/${j.total})${j.error ? ' – ' + esc(j.error) : ''}` : 'No job running.'}</p>
      <button class="primary" onclick="show('jobs')">Choose a job</button>`;
  } else {
    const paused = j.state === 'paused';
    jc.innerHTML = `
      <div class="row spread"><h2>${esc(j.name)}</h2>${phasePill(j.phase)}</div>
      <div class="row spread"><span class="big">${j.percent}%</span><span class="dim">${j.done} / ${j.total} parts${+j.skipped ? ` · ${j.skipped} skipped` : ''}</span></div>
      <div class="progress"><div style="width:${j.percent}%"></div></div>
      <div class="kv" style="margin-top:12px">
        <span>Now</span><span><b>${esc(j.part || '–')}</b> ${esc(j.feeder)}</span>
        <span>Next</span><span>${esc(j.next || '–')}</span>
        <span>Time</span><span>${fmtTime(j.elapsed)} elapsed · ${+j.remaining < 0 ? 'remaining: after the first part' : fmtTime(j.remaining) + ' left'}</span>
      </div>
      <div class="row" style="margin-top:14px">
        <button id="jPause" class="${paused ? 'primary' : ''}">${paused ? 'Resume' : 'Pause'}</button>
        <button id="jSkip">Skip part</button>
        <button id="jStop" class="danger">Stop</button>
      </div>`;
    $('#jPause').onclick = () => cmd(paused ? 'RESUME' : 'PAUSE');
    $('#jSkip').onclick = async () => { if (await confirmBox(`Skip ${j.part}?`, 'Skip')) cmd('SKIP'); };
    $('#jStop').onclick = async () => { if (await confirmBox('Stop the job? The machine parks (run_after) and switches pump and valve off.', 'Stop', true)) cmd('STOP'); };
    if (j.state === 'message' && state.message && $('#modal').hidden) showMessage();
  }
  const ax = $('#axes');
  if (ax) ax.innerHTML = ['x', 'y', 'z', 'e1', 'e0'].map((a) => `<div><span class="dim">${a.toUpperCase()}</span><b>${s[a] ?? '–'}</b></div>`).join('') +
    `<div style="grid-column:1/-1">${+s.homed ? '<span class="pill ok">homed</span>' : '<span class="pill warn">not homed</span>'} ${+s.busy ? '<span class="pill accent">moving</span>' : ''} ${+s.sd ? '' : '<span class="pill bad">no SD card</span>'}</div>`;
  const out = $('#outputs');
  if (out) out.innerHTML = `
    <span>Pump</span><span>${String(s.pump_used) === '0' ? '<b>not connected</b> <span class="dim">(Settings › Vacuum: Pump connected)</span>' : `<b>${s.pump ?? 0}%</b> <button class="small" onclick="cmd('PUMP ${+s.pump ? 'OFF' : 'ON'}')">${+s.pump ? 'Off' : 'On'}</button>`}</span>
    <span>Valve</span><span><b>${+s.valve ? 'open' : 'closed'}</b> <button class="small" onclick="cmd('VALVE ${+s.valve ? 'OFF' : 'ON'}')">${+s.valve ? 'Close' : 'Open'}</button></span>
    <span>Head camera fan</span><span><b>${s.cam_fan ?? '–'}%</b></span>
    <span>Housing fan</span><span><b>${s.box_fan ?? '–'}%</b></span>
    <span>Pi housing fan</span><span><b>${s.pi_fan ?? '–'}%</b></span>`;
}

// ---- Jobs ---------------------------------------------------------------------
async function renderJobs() {
  const dir = state.cwd;
  const parts = dir.split('/').filter(Boolean);
  const crumbs = ['<a data-p="/">Pi</a>'].concat(parts.map((p, i) => `<a data-p="/${parts.slice(0, i + 1).join('/')}">${esc(p)}</a>`)).join(' / ');
  $('#view').innerHTML = `
  <div class="card">
    <div class="row spread"><div class="crumbs">${crumbs}</div>
      <div class="row"><button id="mkdir" class="small">New folder</button><button id="reload" class="small">Refresh</button></div></div>
    <div class="drop" id="drop" style="margin:12px 0">Drop .pap files here or <label style="color:var(--accent);cursor:pointer">choose<input type="file" id="pick" accept=".pap" multiple hidden></label> – they go into <b>${esc(dir)}</b></div>
    <div id="uploads"></div>
    <table><thead><tr><th>Name</th><th>Size</th><th></th></tr></thead><tbody id="files"><tr><td class="dim" colspan="3">Loading…</td></tr></tbody></table>
  </div>`;
  document.querySelectorAll('.crumbs a').forEach((a) => (a.onclick = () => { state.cwd = a.dataset.p; renderJobs(); }));
  $('#reload').onclick = renderJobs;
  $('#mkdir').onclick = async () => {
    const name = prompt('Folder name');
    if (name) { await cmd(`MKDIR ${q(join(dir, name))}`); renderJobs(); }
  };
  const drop = $('#drop');
  drop.ondragover = (e) => { e.preventDefault(); drop.classList.add('over'); };
  drop.ondragleave = () => drop.classList.remove('over');
  drop.ondrop = (e) => { e.preventDefault(); drop.classList.remove('over'); uploadFiles(e.dataTransfer.files); };
  $('#pick').onchange = (e) => uploadFiles(e.target.files);
  renderUploads();

  const r = await cmd(`LS ${q(dir)}`, { quiet: true });
  const body = $('#files');
  if (!r.ok) { body.innerHTML = `<tr><td colspan="3" class="error">${esc(r.error)}</td></tr>`; return; }
  const dirs = r.records.filter((x) => x.type === 'dir' && !x.f.name.startsWith('.') && x.f.name !== 'System Volume Information');
  const files = r.records.filter((x) => x.type === 'file' && /\.pap$/i.test(x.f.name));
  const up = dir !== '/' ? `<tr class="clickable" data-dir=".."><td>⬑ ..</td><td></td><td></td></tr>` : '';
  body.innerHTML = up + dirs.map((d) => `<tr class="clickable" data-dir="${esc(d.f.name)}"><td>📁 ${esc(d.f.name)}</td><td></td><td></td></tr>`).join('') +
    files.map((f) => `<tr data-file="${esc(f.f.name)}"><td>${esc(f.f.name)}</td><td class="dim">${(+f.f.size / 1024).toFixed(1)} KB</td>
      <td class="actions"><button class="small primary" data-a="run">Run</button> <button class="small" data-a="check">Check</button>
      <button class="small" data-a="view">View</button> <button class="small" data-a="mv">Rename</button> <button class="small" data-a="rm">Delete</button></td></tr>`).join('') ||
    `<tr><td colspan="3" class="dim">No .pap files here.</td></tr>`;
  body.querySelectorAll('tr[data-dir]').forEach((tr) => (tr.onclick = () => {
    const d = tr.dataset.dir;
    state.cwd = d === '..' ? '/' + parts.slice(0, -1).join('/') : join(dir, d);
    renderJobs();
  }));
  body.querySelectorAll('button[data-a]').forEach((b) => (b.onclick = () => fileAction(b.dataset.a, join(dir, b.closest('tr').dataset.file))));
}

async function fileAction(a, path) {
  if (a === 'run') return runJob(path);
  if (a === 'check') {
    const r = await send({ t: 'cmd', line: `CHECK ${q(path)}` });
    const problems = r.records.filter((x) => x.type === 'problem');
    modal(`<h2>Check ${esc(path)}</h2>${problems.length ? '<table>' + problems.map((p) => `<tr><td class="dim">line ${esc(p.f.line)}</td><td>${esc(p.f.text)}</td></tr>`).join('') + '</table>' : `<p>${r.ok ? '✔ No problems.' : esc(r.error)}</p>`}<div class="row" style="justify-content:flex-end;margin-top:12px"><button onclick="closeModal()">Close</button></div>`);
  }
  if (a === 'view') {
    const r = await cmd(`CAT ${q(path)}`, { quiet: true });
    modal(`<h2>${esc(path)}</h2><pre>${esc(r.records.map((x) => x.text).join('\n'))}</pre><div class="row" style="justify-content:flex-end"><button onclick="closeModal()">Close</button></div>`);
  }
  if (a === 'rm' && (await confirmBox(`Delete ${path}?`, 'Delete', true))) { await cmd(`RM ${q(path)}`); renderJobs(); }
  if (a === 'mv') {
    const to = prompt('New name or path', path);
    if (to && to !== path) { await cmd(`MV ${q(path)} ${q(to.startsWith('/') ? to : join(state.cwd, to))}`); renderJobs(); }
  }
}

async function runJob(path) {
  if (!+state.status.homed || !+state.status.e1_homed) {
    if (!(await confirmBox('The machine is not homed. Home all axes now, then start the job?', 'Home and run'))) return;
    toast('Homing…');
    const h = await cmd('HOME');
    if (!h.ok) return;
  }
  const r = await send({ t: 'cmd', line: `RUN ${q(path)}` });
  const problems = r.records.filter((x) => x.type === 'problem');
  if (r.ok) { toast('Job started'); show('dash'); }
  else modal(`<h2>Not started</h2><p class="error">${esc(r.error)}</p>${problems.map((p) => `<div><span class="dim">line ${esc(p.f.line)}</span> ${esc(p.f.text)}</div>`).join('')}<div class="row" style="justify-content:flex-end;margin-top:12px"><button onclick="closeModal()">Close</button></div>`);
}

async function uploadFiles(list) {
  for (const file of list) {
    if (!/\.pap$/i.test(file.name)) { toast(file.name + ': only .pap files'); continue; }
    const path = join(state.cwd, file.name);
    state.uploads[path] = { path, done: 0, total: file.size, state: 'sending' };
    renderUploads();
    const res = await fetch('/api/upload?path=' + encodeURIComponent(path), { method: 'POST', body: await file.text() });
    if (!res.ok) { state.uploads[path] = { path, state: 'error', error: 'HTTP ' + res.status }; renderUploads(); }
  }
}

function renderUploads() {
  const box = $('#uploads');
  if (!box) return;
  box.innerHTML = Object.values(state.uploads).filter((u) => u.state !== 'done' || Date.now() - (u.at || (u.at = Date.now())) < 4000)
    .map((u) => `<div class="row" style="margin-bottom:8px"><span style="min-width:200px">${esc(u.path)}</span>
      <div class="progress" style="flex:1"><div style="width:${u.total ? Math.round((u.done / u.total) * 100) : 0}%"></div></div>
      <span class="dim">${u.state === 'error' ? '<span class="error">' + esc(u.error) + '</span>' : esc(u.state)}</span></div>`).join('');
}

// ---- Feeders ------------------------------------------------------------------
async function loadFeeders() {
  const r = await cmd('FEEDERS', { quiet: true });
  if (r.ok) state.feeders = r.records.filter((x) => x.type === 'feeder').map((x) => x.f);
  if (state.view === 'feeders') drawFeeders();
}

function renderFeeders() {
  $('#view').innerHTML = `
  <div class="card">
    <div class="row spread"><h2>Feeders</h2>
      <div class="row"><button id="fAdd" class="small">Add feeder</button><button id="fScan" class="small primary">Auto detect</button></div></div>
    <div id="scanBox"></div>
    <table><thead><tr><th>#</th><th>Name</th><th>Part ID</th><th>E1 pos</th><th>Advance</th><th>Pocket</th><th>QR ID</th><th></th></tr></thead><tbody id="fBody"></tbody></table>
  </div>`;
  $('#fAdd').onclick = async () => { const r = await cmd('FEEDER ADD'); if (r.ok) { await loadFeeders(); editFeeder(r.records[0].f.num); } };
  $('#fScan').onclick = async () => {
    if (!(await confirmBox('Auto detect drives E1 along the whole rail; at every feeder the top camera reads its QR code. Keep the rail and the bed clear.', 'Start'))) return;
    state.scan = { state: 'starting', found: [] };
    cmd('SCAN');
    updateScan();
  };
  drawFeeders();
  updateScan(); // a scan may already be running
  loadFeeders();
}

function drawFeeders() {
  const b = $('#fBody');
  if (!b) return;
  b.innerHTML = state.feeders.map((f) => `<tr class="clickable" data-n="${f.num}"><td>${esc(f.num)}</td><td><b>${esc(f.name)}</b></td><td>${esc(f.part)}</td>
    <td>${esc(f.pos)} mm</td><td>${+f.teeth ? esc(f.teeth) + ' teeth · ' : ''}${esc(f.advance)}°</td><td>${esc(f.next)} / ${esc(f.pockets)}</td><td class="dim mono">${esc(f.tag) || '–'}</td>
    <td class="actions"><button class="small" data-a="advance">Test advance</button> <button class="small" data-a="del">Delete</button></td></tr>`).join('') ||
    '<tr><td colspan="8" class="dim">No feeders yet. Use Auto detect or Add feeder.</td></tr>';
  b.querySelectorAll('tr[data-n]').forEach((tr) => (tr.onclick = (e) => {
    const n = tr.dataset.n, a = e.target.dataset.a;
    if (a === 'advance') cmd(`FEEDER ADVANCE ${n}`);
    else if (a === 'del') confirmBox(`Delete feeder ${n}?`, 'Delete', true).then(async (ok) => { if (ok) { await cmd(`FEEDER DELETE ${n}`); loadFeeders(); } });
    else editFeeder(n);
  }));
}

function updateScan() {
  const box = $('#scanBox');
  if (!box) return;
  const s = state.scan;
  if (!s) { box.innerHTML = ''; return; }
  const running = !['done', 'failed'].includes(s.state);
  box.innerHTML = `<div class="card" style="margin:12px 0;background:var(--panel2)"><div class="row spread">
    <span><b>Auto detect:</b> ${esc(s.state)} · E1 ${esc(s.e1 ?? '–')} mm · ${esc(s.count ?? 0)} found ${s.error ? '<span class="error">' + esc(s.error) + '</span>' : ''}</span>
    ${running ? '<button class="small danger" id="scanStop">Stop</button>' : '<button class="small" id="scanClose">Close</button>'}</div>
    ${(s.found || []).map((f) => `<div class="dim">#${esc(f.num)} at ${esc(f.pos)} mm – ${esc(f.name)} ${f.id ? '· ' + esc(f.id) : '(no QR read' + (f.qr_error ? ': ' + esc(f.qr_error) : '') + ')'} ${+f.new ? '<span class="pill accent">new</span>' : ''}</div>`).join('')}</div>`;
  const stop = $('#scanStop');
  if (stop) stop.onclick = () => cmd('SCAN STOP');
  const close = $('#scanClose');
  if (close) close.onclick = () => { state.scan = null; updateScan(); };
}

async function editFeeder(n) {
  const r = await cmd(`FEEDER ${n}`, { quiet: true });
  if (!r.ok) return;
  const f = r.records.find((x) => x.type === 'feeder').f;
  const pockets = r.records.filter((x) => x.type === 'pocket').map((x) => x.f);
  const m = modal(`
    <div class="row spread"><h2>Feeder ${esc(f.num)}</h2><button class="small" onclick="closeModal()">Close</button></div>
    <div class="kv">
      <span>Name</span><input id="eName" value="${esc(f.name)}" maxlength="20">
      <span>Part ID</span><input id="ePart" value="${esc(f.part)}" maxlength="12">
      <span>E1 position</span><span class="row"><input id="ePos" type="number" step="0.01" value="${esc(f.pos)}"> mm <button class="small" id="eGoto">Go there</button></span>
      <span>Advance</span><span class="row"><input id="eTeeth" type="number" min="0" step="1" value="${esc(f.teeth || 0)}" style="width:70px"> teeth
        <span class="dim">(0 = degrees:</span><input id="eAdv" type="number" step="0.1" value="${esc(f.advance)}" style="width:80px"><span class="dim">°)</span>
        <button class="small" id="eTest">Test advance</button></span>
      <span>QR code</span><span class="row"><span class="mono">${esc(f.tag) || '–'}</span><button class="small" id="eQr">Read QR</button>
        <button class="small" id="eQrTeach" title="Jog the top camera onto this feeder's QR code first">Teach QR position</button>
        <button class="small" onclick="openLive('top')">Live top camera</button></span>
      <span>Next pocket</span><span class="row"><input id="eNext" type="number" min="1" value="${esc(f.next)}"> <button class="small" id="eReset">Reset to 1</button></span>
    </div>
    <div class="row" style="justify-content:flex-end;margin:12px 0"><button class="primary" id="eSave">Save</button></div>
    <h2>Pockets</h2>
    <table><thead><tr><th>#</th><th>X</th><th>Y</th><th></th></tr></thead><tbody>
    ${pockets.map((p) => `<tr data-i="${p.i}"><td>${p.i}</td><td><input type="number" step="0.01" value="${p.x}" data-k="x"></td><td><input type="number" step="0.01" value="${p.y}" data-k="y"></td>
      <td class="actions"><button class="small" data-a="set">Save</button> <button class="small" data-a="here">Set to head</button> <button class="small" data-a="goto">Go there</button> <button class="small" data-a="del">Delete</button></td></tr>`).join('')}
    </tbody></table>
    <div class="row" style="margin-top:10px"><button class="small" onclick="openLive('top')">Live top camera</button><button class="small" id="pHere">Add pocket at the head position</button>
      <input type="number" step="0.01" id="pX" placeholder="X"><input type="number" step="0.01" id="pY" placeholder="Y"><button class="small" id="pAdd">Add</button></div>`);
  const reopen = () => editFeeder(n);
  $('#eSave', m).onclick = async () => {
    const res = await cmd(`FEEDER SET ${n} ${q('name=' + $('#eName', m).value)} ${q('part=' + $('#ePart', m).value)} pos=${+$('#ePos', m).value} ${+$('#eTeeth', m).value > 0 ? 'teeth=' + Math.round(+$('#eTeeth', m).value) : 'advance=' + +$('#eAdv', m).value} next=${+$('#eNext', m).value}`);
    if (res.ok) { toast('Saved'); loadFeeders(); }
  };
  $('#eGoto', m).onclick = () => cmd(`FEEDER GOTO ${n}`);
  $('#eTest', m).onclick = () => cmd(`FEEDER ADVANCE ${n}`);
  $('#eQr', m).onclick = async () => { const r = await cmd(`FEEDER QR ${n}`); if (r.ok) { toast('QR code read'); loadFeeders(); reopen(); } };
  $('#eQrTeach', m).onclick = async () => { if (await confirmBox('The top camera is centred on this feeder\'s QR code right now?', 'Teach')) { const r = await cmd(`FEEDER QR TEACH ${n}`); if (r.ok) toast('QR position taught'); } };
  $('#eReset', m).onclick = async () => { await cmd(`FEEDER RESET ${n}`); reopen(); };
  $('#pHere', m).onclick = async () => { await cmd(`POCKET ${n} ADD HERE`); reopen(); };
  $('#pAdd', m).onclick = async () => { await cmd(`POCKET ${n} ADD x=${+$('#pX', m).value} y=${+$('#pY', m).value}`); reopen(); };
  m.querySelectorAll('tr[data-i] button').forEach((b) => (b.onclick = async () => {
    const tr = b.closest('tr'), i = tr.dataset.i;
    const x = +tr.querySelector('[data-k=x]').value, y = +tr.querySelector('[data-k=y]').value;
    if (b.dataset.a === 'set') await cmd(`POCKET ${n} SET ${i} x=${x} y=${y}`);
    if (b.dataset.a === 'here') await cmd(`POCKET ${n} SET ${i} HERE`);
    if (b.dataset.a === 'goto') return cmd(`POCKET ${n} GOTO ${i}`);
    if (b.dataset.a === 'del') await cmd(`POCKET ${n} DELETE ${i}`);
    reopen();
  }));
}

// ---- Move ---------------------------------------------------------------------
// Every motor by name (protocol axis, label, what it moves, unit). X drives both X motors (X2 follows).
const MOTORS = [
  ['x', 'X', 'head left / right (X + X2)', 'mm'],
  ['y', 'Y', 'head front / back', 'mm'],
  ['z', 'Z', 'seesaw (0 = level)', 'mm'],
  ['rot', 'Part rot.', 'nozzle rotation', '°'],
  ['e1', 'Gantry', 'feeder carriage', 'mm'],
  ['e0', 'Feeder', 'feeder drive', '°'],
  ['paste', 'Paste', 'syringe plunger', 'mm'],
];

function renderMove() {
  $('#view').innerHTML = `
  <div class="grid">
    <div class="card">
      <h2>Jog</h2>
      <div class="axisbox" id="mAxes"></div>
      <div class="row" style="margin:14px 0">Step
        ${[0.1, 1, 10, 50].map((s) => `<button class="small step" data-s="${s}">${s}</button>`).join('')}
        <span class="dim">mm or °</span>
        Speed <input type="number" id="jSpeed" value="30" min="1"> mm/s or °/s</div>
      <div class="row" style="gap:28px;align-items:flex-start">
        <div><div class="dim">X / Y</div><div class="jog">
          <span></span><button data-j="y=+">Y+</button><span></span>
          <button data-j="x=-">X−</button><button id="jHalt" title="Stop moving">■</button><button data-j="x=+">X+</button>
          <span></span><button data-j="y=-">Y−</button><span></span></div></div>
        <div class="motors">${MOTORS.filter(([a]) => a !== 'x' && a !== 'y').map(([a, name, what]) => `
          <div class="motor"><div><b>${name}</b><div class="dim">${what}</div></div>
            <button data-j="${a}=-">−</button><button data-j="${a}=+">+</button></div>`).join('')}</div>
      </div>
      <div class="row" style="margin-top:14px"><button id="mHome">Home all</button><button id="mHomeE1">Home E1</button><button id="mE1Zero">Set E1 = 0 here</button><button id="mOff">Motors off</button></div>
      <div class="row" style="margin-top:14px">Go to X <input type="number" id="gX" step="0.1"> Y <input type="number" id="gY" step="0.1"> Z <input type="number" id="gZ" step="0.1"><button id="gGo">Go</button></div>
    </div>
    <div class="card">
      <h2>Outputs</h2>
      <div class="kv">
        <span>Pump</span><span class="row"><input type="range" min="0" max="100" id="oPump" style="flex:1"><b id="oPumpV"></b></span>
        <span>Valve</span><span><button id="oValve" class="small"></button></span>
      </div>
      <h2 style="margin-top:18px">Sensors</h2>
      <div id="sensors" class="dim">…</div>
      <h2 style="margin-top:18px">Motor test</h2>
      <div class="row">
        <select id="mtAxis">${MOTORS.map(([a, name]) => `<option value="${a}">${name}</option>`).join('')}</select>
        ${[-90, -10, -1, 1, 10, 90].map((d) => `<button class="small" data-mt="${d}">${d > 0 ? '+' : ''}${d}°</button>`).join('')}</div>
    </div>
  </div>`;
  let step = 1;
  const steps = document.querySelectorAll('.step');
  const pickStep = (s) => { step = s; steps.forEach((b) => b.classList.toggle('primary', +b.dataset.s === s)); };
  steps.forEach((b) => (b.onclick = () => pickStep(+b.dataset.s)));
  pickStep(1);
  const speed = () => +$('#jSpeed').value || 30;
  document.querySelectorAll('[data-j]').forEach((b) => (b.onclick = () => {
    const [axis, sign] = b.dataset.j.split('=');
    cmd(`JOG ${axis}=${sign === '-' ? -step : step} speed=${axis === 'paste' ? Math.min(speed(), 2) : speed()}`, { quiet: false }); // plunger: 2 mm/s at most
  }));
  $('#jHalt').onclick = () => cmd('HALT');
  $('#mHome').onclick = () => cmd('HOME');
  $('#mHomeE1').onclick = () => cmd('HOME E1');
  $('#mE1Zero').onclick = () => cmd('SETPOS e1=0');
  $('#mOff').onclick = () => cmd('MOTORS OFF');
  $('#gGo').onclick = () => {
    const parts = ['x', 'y', 'z'].map((a) => [a, $('#g' + a.toUpperCase()).value]).filter(([, v]) => v !== '').map(([a, v]) => `${a}=${+v}`);
    if (parts.length) cmd(`MOVE ${parts.join(' ')} speed=${speed()}`);
  };
  const slider = (id, command) => ($(id).onchange = (e) => cmd(`${command} ${e.target.value}`));
  slider('#oPump', 'PUMP');
  $('#oValve').onclick = () => cmd(`VALVE ${+state.status.valve ? 'OFF' : 'ON'}`);
  document.querySelectorAll('[data-mt]').forEach((b) => (b.onclick = () => cmd(`MOTOR ${$('#mtAxis').value} ${b.dataset.mt}`)));
  updateMove();
  show.poll = setInterval(async () => {
    const [r, v, n] = await Promise.all(['SENSORS', 'VACUUM', 'NOZZLES'].map((line) => send({ t: 'cmd', line })));
    const el = $('#sensors');
    if (!el || !r.ok || !r.records.length) return;
    const f = r.records[0].f;
    const dot = (on, label, title) => `<span class="sensor" title="${title}"><span class="dot ${on ? 'on' : ''}"></span>${label}</span>`;
    const group = (title, items) => `<div class="sgroup"><span class="dim">${title}</span>${items.join('')}</div>`;
    // X, Y and the gantry home sensorless: their DIAG pins carry the TMC2209 stall signal (DIAG jumpers 0, 1, 6, 7)
    const switches = [['z_flag', 'Z flag', 'DIAG2'], ['estop', 'E-stop', 'DIAG5'], ['feeder', 'Feeder sensor', 'DIAG4']];
    const stalls = [['x_min', 'X stall', 'DIAG0'], ['x2_min', 'X2 stall', 'DIAG1'], ['e1_home', 'Gantry stall', 'DIAG6'], ['y_min', 'Y stall', 'DIAG7']];
    const pick = (list) => list.filter(([k]) => k in f).map(([k, label, pin]) => dot(+f[k], label, pin));
    let html = group('Switches', pick(switches)) + group('Stall (sensorless homing)', pick(stalls));
    if ('lc' in f) html += group('Load cell', [`<span class="sensor" title="HX711 on EXP1">${+f.lc ? `${(+f.load).toFixed(1)} g` : '<span class="error">not found</span>'}</span>`]);
    const vac = v.ok && v.records.find((x) => x.type === 'vacuum');
    if (vac) {
      const t = +vac.f.sensor ? `${(+vac.f.hpa).toFixed(1)} hPa · drop ${(+vac.f.drop).toFixed(1)} · part ${+vac.f.part ? '<b>yes</b>' : 'no'}` : '<span class="error">sensor not found</span>';
      html += group('Vacuum', [`<span class="sensor">${t}</span>`]);
    }
    const slots = n.ok ? n.records.filter((x) => x.type === 'nozzle') : [];
    if (slots.length) html += group('Nozzle rack', slots.map((x) => `<span class="sensor" title="Rack slot ${esc(x.f.num)}"><span class="dot ${+x.f.present === 1 ? 'ok' : ''}"></span>${esc(x.f.name || 'Slot ' + x.f.num)}${+x.f.mounted ? ' (on head)' : ''}</span>`));
    el.innerHTML = html;
  }, 700);
}

function updateMove() {
  const s = state.status;
  const ax = $('#mAxes');
  if (!ax) return;
  ax.innerHTML = MOTORS.map(([a, name, , unit]) => `<div title="${a}"><span class="dim">${name}</span><b>${s[a] ?? '–'}<small>${unit}</small></b></div>`).join('');
  for (const [id, key] of [['#oPump', 'pump']]) {
    const el = $(id);
    if (document.activeElement !== el) el.value = s[key] ?? 0;
    el.disabled = String(s.pump_used) === '0'; // no pump connected
    $(id + 'V').textContent = (s[key] ?? 0) + '%';
  }
  $('#oValve').textContent = +s.valve ? 'Open – close it' : 'Closed – open it';
}

// ---- Nozzles ------------------------------------------------------------------
// The rack (NOZZLES / NOZZLE), sensors refreshed every 1.5 s; rack geometry = nozzle_* settings.
const NOZZLE_KEYS = [
  ['nozzle_count', 'Slots in the rack (0 = no changer)'],
  ['nozzle_first_x', 'Slot 1 X'], ['nozzle_first_y', 'Slot 1 Y'],
  ['nozzle_last_x', 'Last slot X'], ['nozzle_last_y', 'Last slot Y'],
  ['nozzle_z_pick', 'Push-on Z (seesaw, − = nozzle side down)'], ['nozzle_z_drop', 'Park Z (nozzle sits in the slot)'],
  ['nozzle_z_safe', 'Travel Z at the rack'], ['nozzle_slide_x', 'Slide X into / out of the slot (0 = straight)'],
  ['nozzle_slide_y', 'Slide Y'], ['nozzle_speed', 'Speed at the rack (mm/s)'], ['nozzle_wait_ms', 'Pause while pushed on (ms)'],
  ['nozzle_sensor_invert', 'Sensor signal LOW = nozzle present (1)'], ['nozzle_check', 'Check every step with the sensors (1 / 0)'],
  ['nozzle_touchoff', 'Touch off on the load cell after every change (1 / 0)'],
  ['loadcell', 'Load cell fitted (HX711 on Octopus EXP1) (1 / 0)'], ['loadcell_x', 'Load cell pad X (vacuum nozzle over it)'],
  ['loadcell_y', 'Load cell pad Y'], ['loadcell_height', 'Pad surface above the bed (mm)'],
];

async function renderNozzles() {
  $('#view').innerHTML = `
  <div class="grid">
    <div class="card" style="grid-column:1/-1"><div class="row spread"><h2>Nozzle rack</h2><span id="nzHead" class="pill">…</span></div>
      <div style="overflow-x:auto"><table><thead><tr><th>Slot</th><th>Name</th><th>Offset X</th><th>Offset Y</th><th>Height</th><th>Sensor</th><th></th></tr></thead>
      <tbody id="nzBody"><tr><td class="dim">Loading…</td></tr></tbody></table></div>
      <div class="row" style="margin-top:12px"><button id="nzPut">Put the nozzle back</button>
        <button id="nzTouch" title="Load cell: measures the tip height of the nozzle on the head">Touch off</button>
        <span class="dim">Jobs name the nozzle per part (<code>nozzle = CN040</code>) or for the whole job in <code>[job]</code>.</span></div>
      <div id="nzOut" class="dim" style="margin-top:8px"></div>
    </div>
    <div class="card"><h2>Rack</h2>
      <div class="row" style="margin-bottom:10px"><button class="small" id="nzFirst">Slot 1 = current X / Y</button>
        <button class="small" id="nzLast">Last slot = current X / Y</button></div>
      <table><tbody id="nzSet"></tbody></table>
      <p class="dim">The slots between the first and the last are spaced evenly. Slot sensors come from the tool changer board on the Octopus TFT header; set their order in Calibrate, Nozzle rack.</p>
    </div>
  </div>`;
  const out = (t) => ($('#nzOut').textContent = t);
  let editing = false;
  const load = async () => {
    const r = await send({ t: 'cmd', line: 'NOZZLES' });
    const body = $('#nzBody');
    if (!body || editing) return;
    if (!r.ok) { body.innerHTML = `<tr><td class="error">${esc(r.error)}</td></tr>`; return; }
    const slots = r.records.filter((x) => x.type === 'nozzle').map((x) => x.f);
    const rack = (r.records.find((x) => x.type === 'rack') || {}).f || {};
    const on = slots.find((s) => +s.mounted);
    $('#nzHead').textContent = on ? 'on head: ' + (on.name || 'slot ' + on.num) : 'no nozzle on the head';
    $('#nzHead').className = 'pill ' + (on ? 'accent' : '');
    if (!slots.length) { body.innerHTML = '<tr><td class="dim" colspan="7">No rack: set nozzle_count on the right.</td></tr>'; return; }
    body.innerHTML = slots.map((s) => {
      const sensor = +s.mounted ? '<span class="pill accent">on head</span>' : s.present === '1' ? '<span class="pill ok">in rack</span>'
        : s.present === '0' ? '<span class="pill bad">empty</span>' : '<span class="dim">–</span>';
      return `<tr data-n="${s.num}"><td><b>${s.num}</b><div class="dim" style="font-size:12px">${esc(s.x)}, ${esc(s.y)}</div></td>
        <td><input class="nzName" value="${esc(s.name)}" maxlength="15" style="width:110px"></td>
        <td><input class="nzDx" type="number" step="0.01" value="${esc(s.dx)}" style="width:80px"></td>
        <td><input class="nzDy" type="number" step="0.01" value="${esc(s.dy)}" style="width:80px"></td>
        <td><input class="nzDz" type="number" step="0.01" value="${esc(s.dz ?? 0)}" style="width:80px" title="tip reaches this much further than head2_reach (load cell)"></td>
        <td>${sensor}</td>
        <td class="actions"><button class="small nzSave">Save</button> <button class="small primary nzPick" ${+s.mounted ? 'disabled' : ''}>Pick up</button></td></tr>`;
    }).join('');
    body.querySelectorAll('input').forEach((i) => { i.onfocus = () => (editing = true); i.onblur = () => (editing = false); });
    body.querySelectorAll('.nzSave').forEach((b) => (b.onclick = async () => {
      const tr = b.closest('tr'), n = tr.dataset.n;
      const name = $('.nzName', tr).value.replace(/[\s"]/g, '');
      const res = await cmd(`NOZZLE ${n} name=${name} dx=${+$('.nzDx', tr).value || 0} dy=${+$('.nzDy', tr).value || 0} dz=${+$('.nzDz', tr).value || 0}`);
      if (res.ok) toast('Saved');
      editing = false;
      load();
    }));
    body.querySelectorAll('.nzPick').forEach((b) => (b.onclick = async () => {
      const n = b.closest('tr').dataset.n;
      out('changing to slot ' + n + ' …');
      const res = await cmd('NOZZLE CHANGE ' + n);
      out(res.ok ? 'Nozzle ' + n + ' is on the head' : 'Change failed: ' + res.error);
      load();
    }));
    void rack;
  };
  $('#nzTouch').onclick = async () => {
    out('touching off on the load cell …');
    const res = await cmd('NOZZLE TOUCH');
    const t = res.ok && res.records.find((x) => x.type === 'touch');
    out(t ? `Tip height: reach ${(+t.f.reach).toFixed(3)} mm` + (+t.f.slot ? ` (slot ${t.f.slot}: ${(+t.f.dz >= 0 ? '+' : '') + (+t.f.dz).toFixed(3)} mm)` : ' (head2_reach set)') : 'Touch-off failed: ' + res.error);
    load();
  };
  $('#nzPut').onclick = async () => {
    out('putting the nozzle back …');
    const res = await cmd('NOZZLE CHANGE 0');
    out(res.ok ? 'Nozzle put back' : 'Failed: ' + res.error);
    load();
  };
  const setRows = async () => {
    const g = await cmd('GET', { quiet: true });
    const vals = Object.fromEntries((g.records || []).map((x) => [x.f.key, x.f.value]));
    $('#nzSet').innerHTML = NOZZLE_KEYS.map(([k, help]) => `<tr><td><b>${k}</b><div class="dim" style="font-size:12px">${esc(help)}</div></td>
      <td><input value="${esc(vals[k] ?? '')}" data-k="${k}" style="width:90px"></td><td><button class="small" data-k="${k}">Set</button></td></tr>`).join('');
    $('#nzSet').querySelectorAll('button').forEach((b) => (b.onclick = async () => {
      const res = await cmd(`SET ${b.dataset.k} ${q($('#nzSet').querySelector(`input[data-k="${b.dataset.k}"]`).value)}`);
      if (res.ok) { toast('Saved'); load(); }
    }));
  };
  const teach = (which) => async () => {
    const s = state.status;
    if (s.x === undefined) return toast('no position yet');
    await cmd(`SET nozzle_${which}_x ${s.x}`);
    await cmd(`SET nozzle_${which}_y ${s.y}`);
    setRows();
    load();
  };
  $('#nzFirst').onclick = teach('first');
  $('#nzLast').onclick = teach('last');
  setRows();
  load();
  show.poll = setInterval(load, 1500);
}

// ---- Cameras & calibration -------------------------------------------------------
// The picture comes from CAM <cam> FRAME (JPEG, base64), one request at a time.
const kvText = (rec) => (rec ? Object.entries(rec.f || {}).filter(([, v]) => v !== '').map(([k, v]) => `${k} ${v}`).join(' · ') : '');

function renderCameras() {
  const gen = show.gen;
  $('#view').innerHTML = `
  <div class="grid">
    <div class="card">
      <div class="row spread"><h2>Camera</h2>
        <div class="row"><button class="small" data-cam="top">Top</button><button class="small" data-cam="bottom">Bottom</button>
          <button class="small" id="cLight">Light</button></div></div>
      <img id="camImg" class="campic" alt="camera picture">
      <div class="row" style="margin-top:10px"><button id="cMeasure">Measure part</button><button id="cFid">Find mark</button>
        <button id="cCenter">Centre on mark</button><button id="cSave">Save picture</button></div>
      <div id="cResult" class="dim" style="margin-top:8px"></div>
      <div class="row" style="margin-top:12px">Jog
        ${[0.05, 0.1, 1, 10].map((s) => `<button class="small cstep" data-s="${s}">${s} mm</button>`).join('')}</div>
      <div class="jog" style="margin-top:8px">
        <span></span><button data-cj="y=+">Y+</button><span></span>
        <button data-cj="x=-">X−</button><button id="cHalt">■</button><button data-cj="x=+">X+</button>
        <span></span><button data-cj="y=-">Y−</button><span></span></div>
      <div class="axisbox" id="cAxes" style="margin-top:10px"></div>
    </div>
    <div class="card">
      <h2>Calibration</h2>
      <p class="dim">Cameras, axes, heads, load cell, nozzle rack, vacuum, paste, feeders and joystick are calibrated in the
        Calibrate tab, step by step, with this picture next to the steps.</p>
      <button class="primary" id="cGoCal">Open Calibrate</button>
    </div>
  </div>`;
  let cam = renderCameras.cam || 'top', step = 0.1;
  const img = $('#camImg');
  const result = (t) => ($('#cResult').textContent = t);
  const pick = (c) => {
    cam = renderCameras.cam = c;
    document.querySelectorAll('[data-cam]').forEach((b) => b.classList.toggle('primary', b.dataset.cam === c));
    $('#cMeasure').disabled = c !== 'bottom';
    $('#cFid').disabled = $('#cCenter').disabled = c !== 'top';
    updateCameras();
  };
  document.querySelectorAll('[data-cam]').forEach((b) => (b.onclick = () => pick(b.dataset.cam)));
  const steps = document.querySelectorAll('.cstep');
  const pickStep = (s) => { step = s; steps.forEach((b) => b.classList.toggle('primary', +b.dataset.s === s)); };
  steps.forEach((b) => (b.onclick = () => pickStep(+b.dataset.s)));
  pickStep(0.1);
  document.querySelectorAll('[data-cj]').forEach((b) => (b.onclick = () => {
    const [axis, sign] = b.dataset.cj.split('=');
    cmd(`JOG ${axis}=${sign === '-' ? -step : step} speed=20`);
  }));
  $('#cHalt').onclick = () => cmd('HALT');
  $('#cLight').onclick = () => cmd(`LIGHT ${cam} ${+(state.status['light_' + cam] || 0) > 0 ? 'OFF' : 'ON'}`);
  $('#cMeasure').onclick = async () => { result('measuring …'); const r = await cmd('CAM bottom MEASURE'); if (r.ok) result(kvText(r.records[0])); };
  $('#cFid').onclick = async () => { result('looking …'); const r = await cmd('CAM top FIDUCIAL'); if (r.ok) result(kvText(r.records[0])); };
  $('#cCenter').onclick = async () => {
    const r = await cmd('CAM top FIDUCIAL');
    const f = r.ok && r.records[0] && r.records[0].f;
    if (!f || f.ok !== '1') return result('no mark found');
    await cmd(`JOG x=${(+f.dx).toFixed(3)} y=${(+f.dy).toFixed(3)} speed=20`);
    result(`moved by ${(+f.dx).toFixed(2)} / ${(+f.dy).toFixed(2)} mm`);
  };
  $('#cSave').onclick = async () => {
    const r = await cmd(`CAM ${cam} SAVE /captures/${cam}-${Date.now()}.jpg`);
    if (r.ok) toast('Saved ' + (r.records[0] ? r.records[0].f.path : ''));
  };
  $('#cGoCal').onclick = () => show('calibrate');
  pick(cam);
  const frameLoop = async () => {
    if (show.gen !== gen) return;
    const r = await send({ t: 'cmd', line: `CAM ${cam} FRAME w=640 q=70 cross=1` });
    if (show.gen !== gen) return;
    if (r.ok && r.records[0] && r.records[0].f.jpeg) img.src = 'data:image/jpeg;base64,' + r.records[0].f.jpeg;
    else if (!r.ok) result('camera: ' + r.error);
    setTimeout(frameLoop, r.ok ? 120 : 1500);
  };
  frameLoop();
}

function updateCameras() {
  const s = state.status, ax = $('#cAxes');
  if (!ax) return;
  ax.innerHTML = ['x', 'y', 'z', 'rot'].map((a) => `<div><span class="dim">${a.toUpperCase()}</span><b>${s[a] ?? '–'}</b></div>`).join('');
  const cam = renderCameras.cam || 'top';
  const on = +(s['light_' + cam] || 0) > 0;
  $('#cLight').textContent = on ? 'Light on' : 'Light off';
  $('#cLight').classList.toggle('primary', on);
}

// ---- Calibrate -------------------------------------------------------------------
// Every calibration in one place, in the order to do them. Left: camera picture, jog and position (sticky).
// Right: one card per topic with steps (buttons that measure or take over the current position), live
// readings and the settings they produce (editable; SETTINGS catalog for labels and values).
const CAL_AXES = { x: ['X', 'mm', 100], y: ['Y', 'mm', 100], z: ['Z (seesaw)', 'mm', 5], e1: ['E1 feeder carriage', 'mm', 100], paste: ['Paste plunger', 'mm', 5] };

function renderCalibrate() {
  const gen = show.gen;
  let cam = renderCalibrate.cam || 'top', step = 0.1, vals = {};
  const mem = renderCalibrate.mem || (renderCalibrate.mem = {}); // remembered positions between steps

  const stepRow = (title, text, buttons, extra = '') => `<div class="calstep"><div><b>${title}</b><span class="dim">${text}</span>${extra}</div>
    <div class="row">${buttons}</div><div class="calres dim"></div></div>`;
  const btn = (act, label, cls = '') => `<button class="small ${cls}" data-act="${act}">${label}</button>`;
  // keys: 'key' (label from the settings catalog) or ['key', 'own label'] where the catalog's label is ambiguous
  const fields = (keys) => `<div class="calvals">${keys.map((k) => {
    const [key, label] = Array.isArray(k) ? k : [k, ''];
    return `<label data-f="${key}" data-label="${esc(label)}"><span class="dim"></span><span class="row ctl"></span></label>`;
  }).join('')}</div>`;
  const live = (id) => `<div class="callive" id="${id}">…</div>`;
  const sec = (n, id, title, intro, body) => `<div class="card calsec" id="cal-${id}"><h2><span class="calnum">${n}</span>${title}</h2>
    <p class="dim">${intro}</p>${body}</div>`;

  const sections = [
    sec(1, 'axes', 'Axes: steps per mm', 'Move an axis by a known distance, measure how far it really went (ruler, dial gauge, calliper), let the machine correct its steps. Repeat until it matches.',
      stepRow('Move', 'Pick the axis and the distance, then move it.',
        `<select id="caAxis">${Object.entries(CAL_AXES).map(([k, [label]]) => `<option value="${k}">${label}</option>`).join('')}</select>
         <input type="number" id="caDist" step="0.1" style="width:80px"> <span id="caUnit" class="dim">mm</span> ${btn('axisMove', 'Move', 'primary')}`) +
      stepRow('Correct', 'Enter the distance it really moved.',
        `<input type="number" id="caMeas" step="0.01" style="width:80px"> <span class="dim">mm</span> ${btn('axisFix', 'Correct steps')}`) +
      fields([['steps_x', 'X steps / mm'], ['steps_y', 'Y steps / mm'], ['steps_z', 'Z steps / mm'], ['steps_e1', 'E1 steps / mm'], ['steps_paste', 'Paste plunger steps / mm']])),

    sec(2, 'heads', 'Heads: seesaw and paste tip', 'Z is the seesaw: 0 = level, − lowers the nozzle (head 2), + lowers the paste tip (head 1); if it is the other way round, set invert_z 1. Use a sheet of paper on the bed as feeler. The vacuum nozzles are sprung, so their reach is not jogged: it comes from the load cell touch-off (Nozzles).',
      stepRow('Paste tip reach', 'Jog Z up until the paste tip just grips the paper.', btn('reach1', 'Paste tip touches the bed')) +
      stepRow('Paste tip offset 1/2', 'Jog X / Y until the nozzle tip is exactly over a mark (pen dot, fiducial).', btn('h1mark', 'Nozzle is on the mark')) +
      stepRow('Paste tip offset 2/2', 'Now jog X / Y until the paste tip is over the same mark.', btn('h1off', 'Paste tip is on the mark')) +
      fields(['head2_reach', 'head1_reach', 'head1_offset_x', 'head1_offset_y', 'pick_height', 'z_home'])),

    sec(3, 'bottom', 'Bottom camera', 'Before the top camera. Light on, nozzle tip (no part) roughly above the bottom camera, at the height it looks at parts.',
      stepRow('Live picture', 'A small window with the camera picture that stays open while you jog and calibrate.', `<button class="small" onclick="openLive('bottom')">Live bottom camera</button>`) +
      stepRow('Calibrate', 'The head moves around by a few mm: scale, skew, rotation centre and the look position are measured.', btn('calBottom', 'Calibrate', 'primary')) +
      stepRow('Checkerboard', 'After Calibrate: <a href="/checkerboard.html" target="_blank">print a checkerboard</a> (square size = cal_grid), lay it face down over the camera, sharp and filling the picture. One picture: lens distortion and exact scale.', btn('calBottomGrid', 'Checkerboard', 'primary')) +
      stepRow('Lens without checkerboard', 'Instead of the checkerboard: tip still in the picture centre, it moves the tip to 9 places across the picture.', btn('calBottomLens', 'Lens')) +
      stepRow('Rotation direction', 'A part on the nozzle above the bottom camera; the nozzle turns a little.', btn('rotSign', 'Measure')) +
      stepRow('Test', 'A part on the nozzle: is it found (green outline in the picture)?', btn('testPart', 'Measure part')) +
      fields(['cal_grid', 'cam_bottom_x', 'cam_bottom_y', 'cam_bottom_z', 'cam_bottom_cx', 'cam_bottom_cy', 'cam_bottom_rot_sign', 'cam_bottom_m00', 'cam_bottom_m01', 'cam_bottom_m10', 'cam_bottom_m11', 'cam_bottom_k1', 'cam_bottom_k2'])),

    sec(4, 'top', 'Top camera', 'Best with a printed checkerboard; a mark on the bed (fiducial, pen dot) works too. Light on.',
      stepRow('Live picture', 'A small window with the camera picture that stays open while you jog and calibrate.', `<button class="small" onclick="openLive('top')">Live top camera</button>`) +
      stepRow('Checkerboard', '<a href="/checkerboard.html" target="_blank">print a checkerboard</a> (square size = cal_grid), lay it flat on the bed under the camera, sharp and filling the picture. Lens, scale and skew from the squares, rotation from two small moves.', btn('calTopGrid', 'Checkerboard', 'primary')) +
      stepRow('On a mark', 'Instead of the checkerboard: camera centred above a mark, the head moves around it (scale, skew).', btn('calTop', 'Calibrate')) +
      stepRow('Lens on a mark', 'After "On a mark", mark still in the picture centre: it lands at 9 places across the picture.', btn('calTopLens', 'Lens')) +
      stepRow('Offset 1/2', 'Jog the nozzle tip exactly onto the mark.', btn('topMark', 'Remember nozzle')) +
      stepRow('Offset 2/2', 'Jog the top camera roughly above the same mark.', btn('topOff', 'Measure offset')) +
      stepRow('Test', 'Is the mark found (green circle)?', btn('testMark', 'Find mark') + btn('centerMark', 'Centre on mark')) +
      fields(['cal_grid', 'cam_top_off_x', 'cam_top_off_y', 'cam_top_m00', 'cam_top_m01', 'cam_top_m10', 'cam_top_m11', 'cam_top_k1', 'cam_top_k2'])),

    sec(5, 'loadcell', 'Load cell (nozzle tip height)', 'The pad on the load cell between the feeders. Nothing on the pad for the tare.',
      live('lcLive') +
      stepRow('Tare', 'Pad empty.', btn('lcTare', 'Tare')) +
      stepRow('Force', 'Put a known weight on the pad.', `<input type="number" id="lcGrams" value="100" style="width:80px"> <span class="dim">g</span> ${btn('lcCal', 'Calibrate')}`) +
      stepRow('Pad height 1/2', 'Nozzle on the bed (paper), anywhere.', btn('lcBed', 'Nozzle touches the bed')) +
      stepRow('Pad height 2/2', 'Nozzle centred over the pad, jog Z down until it just touches it. Sets the pad position and height.', btn('lcPad', 'Nozzle touches the pad')) +
      stepRow('Touch off', 'Measures the nozzle on the head on the pad (sets its tip height).', btn('lcTouch', 'Touch off', 'primary')) +
      fields(['loadcell', 'loadcell_x', 'loadcell_y', 'loadcell_height', 'nozzle_touchoff'])),

    sec(6, 'rack', 'Nozzle rack', 'Positions and heights of the push-on nozzle rack. Names and offsets per slot: Nozzles tab.',
      stepRow('First slot', 'Nozzle centred above slot 1.', btn('rackFirst', 'Slot 1 = here')) +
      stepRow('Last slot', 'Nozzle centred above the last slot.', btn('rackLast', 'Last slot = here')) +
      stepRow('Push-on height', 'Nozzle side down until a nozzle in its slot is pushed on firmly.', btn('rackPick', 'Push-on Z = here')) +
      stepRow('Park height', 'Height at which a nozzle sits in its slot (lifting from there pulls it off).', btn('rackDrop', 'Park Z = here')) +
      stepRow('Sensor order', 'Set nozzle_count first. Then trigger the slots in order, 1 first: nozzle in or out, or a finger on the sensor.', btn('rackLearn', 'Start', 'primary') + btn('rackLearnStop', 'Cancel'), live('rackLearnLive')) +
      stepRow('Test', 'Put the nozzle back / pick slot 1 up.', btn('rackPut', 'Put back') + btn('rackOne', 'Pick up slot 1')) +
      fields(['nozzle_count', 'nozzle_first_x', 'nozzle_first_y', 'nozzle_last_x', 'nozzle_last_y', 'nozzle_z_pick', 'nozzle_z_drop', 'nozzle_z_safe', 'nozzle_sensor_invert'])),

    sec(7, 'vacuum', 'Vacuum sensor', 'Three readings set when a part counts as picked.',
      live('vacLive') +
      stepRow('Baseline', 'Pump off.', btn('vacBase', 'Measure')) +
      stepRow('Open', 'Pump on, nozzle open (nothing on it).', btn('vacPump', 'Pump on / off') + btn('vacOpen', 'Measure')) +
      stepRow('Blocked', 'Pump on, nozzle blocked (finger or a part).', btn('vacBlocked', 'Measure')) +
      fields(['vac_baseline', 'vac_pick_min'])),

    sec(8, 'paste', 'Paste extruder', 'Syringe size turns µl into plunger travel. Steps per mm of plunger: section 1 (axis "Paste plunger").',
      stepRow('Test dot', 'Pushes the amount out (prime the syringe first).', `<input type="number" id="pAmt" value="0.5" step="0.1" style="width:70px"> <span class="dim">µl</span> ${btn('pasteDot', 'Push')} ${btn('pastePrime', 'Prime 5 µl')}`) +
      fields(['syringe_d', 'paste_speed', 'paste_retract'])),

    sec(9, 'feeders', 'Feeders and positions', 'Where the top camera finds the feeders\' QR codes, and where rejected parts are dropped.',
      stepRow('QR code position', 'Jog the top camera onto one feeder\'s QR code.', `<select id="fQr"></select> ${btn('qrTeach', 'Teach')} ${btn('qrRead', 'Read QR')} <button class="small" onclick="openLive('top')">Live top camera</button>`) +
      stepRow('Reject position', 'Nozzle above the reject bin.', btn('rejectHere', 'Reject = here')) +
      fields(['feeder_qr_x0', 'feeder_qr_y0', 'feeder_qr_dx', 'feeder_qr_dy', 'reject_x', 'reject_y'])),

    sec(10, 'joystick', 'Joystick', 'The display\'s joystick.',
      live('joyLive') +
      stepRow('Centre', 'Let go of the stick.', btn('joyCenter', 'Measure')) +
      stepRow('Range', 'Move the stick in full circles for 5 s.', btn('joyRange', 'Start (5 s)')) +
      fields(['joy_x_min', 'joy_x_center', 'joy_x_max', 'joy_y_min', 'joy_y_center', 'joy_y_max'])),

    sec(11, 'homing', 'Homing (sensorless)', 'X, Y and the gantry home without switches: the driver stops the motor when its load value (StallGuard) drops below 2 × the threshold <code>tmc_sg</code>. Measure it running free, save the threshold, then test.',
      stepRow('Home one axis', 'Each axis on its own, to see how it behaves.',
        btn('homeX', 'X') + btn('homeY', 'Y') + btn('homeZ', 'Z (flag)') + btn('homeE1', 'Gantry') + btn('homeAll', 'All')) +
      stepRow('Axis', 'The axis to calibrate. Jog it into free travel, away from the ends.',
        `<select id="sgAxis"><option value="x">X (both motors)</option><option value="y">Y</option><option value="e1">Gantry (E1)</option></select>
         ${btn('sgJogM', '−10 mm')} ${btn('sgJogP', '+10 mm')}`) +
      stepRow('Measure', 'Runs this far towards its home end, like homing, and pushes into the end without stopping (start closer than that). At the homing speed and current (home_cur): load while running free (max) and stalled (min).',
        `<input type="number" id="sgMm" value="50" step="10" style="width:70px"> <span class="dim">mm</span> ${btn('sgMeasure', 'Measure', 'primary')}<div id="sgLive" style="margin-top:6px"></div>`) +
      stepRow('Save the threshold', 'Centre between min (stalled) and max (free): it won\'t stop in free travel, but stops at the end.', btn('sgUse', 'Use the suggestion')) +
      stepRow('Test', 'Home this axis with the saved values. Stops too early: lower tmc_sg. Rams the end: raise it, or lower home_cur.', btn('sgHome', 'Home this axis', 'primary')) +
      fields(['home_cur', 'home_speed', 'home_blank', ['tmc_sg_x', 'X threshold'], ['tmc_sg_x2', 'X2 threshold'], ['tmc_sg_y', 'Y threshold'],
        ['tmc_sg_e1', 'Gantry threshold'], ['tmc_cur_x', 'X current'], ['tmc_cur_y', 'Y current'], ['tmc_cur_e1', 'Gantry current'], 'home_xy', 'home_dir_x', 'home_dir_y', 'e1_home', 'e1_home_dir'])),

    sec(12, 'length', 'Axis length', 'How far X, Y or the gantry can travel: it homes, runs to the far end until it stalls, and measures. Homing must work first (section 11).',
      stepRow('Find length', 'The axis runs the full way: keep the path clear.',
        `<select id="flAxis"><option value="x">X</option><option value="y">Y</option><option value="e1">Gantry (E1)</option></select> ${btn('flFind', 'Find length', 'primary')}`) +
      stepRow('Save', 'The length minus the pull-off becomes the travel (soft limit).', btn('flSave', 'Save as travel')) +
      fields(['travel_x', 'travel_y', 'travel_e1'])),

    sec(13, 'offsets', 'Head offsets', 'Nozzle, paste tip and top camera on one reference point, the bottom camera. Each button finds the tip (or the lens), moves it onto the cross, looks again until it sits there, and saves. The picture on the left switches to the camera of the step; the green circle is what it found. Nozzle first.',
      stepRow('Exposure', 'Fix: the camera adjusts with its light on, then that brightness is kept as a fixed exposure (every picture the same). Auto: adjusts each picture (it waits until the picture stands still).',
        btn('expBottom', 'Fix bottom') + btn('expTop', 'Fix top') + btn('expAuto', 'Both auto')) +
      stepRow('Nozzle', 'Nozzle tip roughly over the bottom camera, at its working height. Two small test moves first: it learns which way the picture moves.', btn('hoNozzleCam', 'Nozzle', 'primary') + btn('hoNozzle', 'On a mark (by eye)')) +
      stepRow('Paste tip', 'Paste tip roughly over the bottom camera (Z+ brings it down).', btn('hoPasteCam', 'Paste tip', 'primary') + btn('hoPaste', 'On the mark (by eye)')) +
      stepRow('Top camera', 'Top camera roughly above the bottom camera lens: Find centres it. Not found: centre the lens on the cross by hand and use By eye.', btn('hoCamera', 'Find', 'primary') + btn('hoCameraManual', 'By eye')) +
      fields(['head1_offset_x', 'head1_offset_y', 'cam_top_off_x', 'cam_top_off_y', 'vhome', 'vhome_ref_x', 'vhome_ref_y', 'vhome_max'])),

    sec(14, 'display', 'Touch screen', 'The machine display\'s touch.',
      stepRow('Calibrate touch', 'Opens the calibration on the display: tap the 4 crosses there, one after the other. Press the joystick to cancel.', btn('touchCal', 'Calibrate touch'))),
  ];

  $('#view').innerHTML = `<div class="calgrid">
    <div class="card calside">
      <div class="row spread"><h2>Machine</h2>
        <div class="row"><button class="small" data-cam="top">Top</button><button class="small" data-cam="bottom">Bottom</button>
          <button class="small" id="kLight">Light</button></div></div>
      <img id="kImg" class="campic" alt="camera picture"><div id="kCamErr" class="dim"></div>
      <div class="axisbox" id="kAxes" style="margin-top:10px"></div>
      <div class="row" style="margin-top:10px">Step ${[0.01, 0.1, 1, 10].map((s) => `<button class="small kstep" data-s="${s}">${s}</button>`).join('')}</div>
      <div class="row" style="margin-top:8px;align-items:flex-start;gap:18px">
        <div class="jog"><span></span><button data-kj="y=+">Y+</button><span></span>
          <button data-kj="x=-">X−</button><button id="kHalt" title="Stop">■</button><button data-kj="x=+">X+</button>
          <span></span><button data-kj="y=-">Y−</button><span></span></div>
        <div class="jog" style="grid-template-columns:64px"><button data-kj="z=+" title="paste tip down">Z+</button><button data-kj="z=-" title="nozzle down">Z−</button></div></div>
      <div class="row" style="margin-top:10px"><button class="small" id="kHome">Home</button><button class="small" id="kLevel">Z = 0 (level)</button></div>
      <nav class="caltoc" id="kToc"></nav>
    </div>
    <div class="calmain">${sections.join('')}</div></div>`;

  // ---- side panel: picture, jog, position, contents ----
  $('#kToc').innerHTML = [...document.querySelectorAll('.calsec')].map((c) =>
    `<a data-to="${c.id}">${esc($('h2', c).textContent.replace(/^\d+/, (n) => n + '. '))}</a>`).join('');
  document.querySelectorAll('[data-to]').forEach((a) => (a.onclick = () => document.getElementById(a.dataset.to).scrollIntoView({ behavior: 'smooth', block: 'start' })));
  const img = $('#kImg');
  const pickCam = (c) => {
    cam = renderCalibrate.cam = c;
    bottomPart = false;
    camLights(c);
    document.querySelectorAll('[data-cam]').forEach((b) => b.classList.toggle('primary', b.dataset.cam === c));
    updateCalibrate();
  };
  document.querySelectorAll('[data-cam]').forEach((b) => (b.onclick = () => pickCam(b.dataset.cam)));
  const steps = document.querySelectorAll('.kstep');
  const pickStep = (s) => { step = s; steps.forEach((b) => b.classList.toggle('primary', +b.dataset.s === s)); };
  steps.forEach((b) => (b.onclick = () => pickStep(+b.dataset.s)));
  pickStep(0.1);
  document.querySelectorAll('[data-kj]').forEach((b) => (b.onclick = () => {
    const [axis, sign] = b.dataset.kj.split('=');
    cmd(`JOG ${axis}=${sign === '-' ? -step : step} speed=20`);
  }));
  $('#kHalt').onclick = () => cmd('HALT');
  $('#kHome').onclick = () => cmd('HOME');
  $('#kLevel').onclick = () => cmd('MOVE z=0 speed=5');
  // bottom: off -> calibration colour -> part colour -> off; top: on / off
  let bottomPart = false;
  $('#kLight').onclick = () => {
    const on = +(state.status['light_' + cam] || 0) > 0;
    if (cam === 'bottom' && on && !bottomPart) { bottomPart = true; return cmd('LIGHT bottom PART'); }
    bottomPart = false;
    return cmd(`LIGHT ${cam} ${on ? 'OFF' : 'ON'}`);
  };
  pickCam(cam);
  const frameLoop = async () => {
    if (show.gen !== gen) return camLights(null); // page left: lights back
    const r = await send({ t: 'cmd', line: `CAM ${cam} FRAME w=480 q=65 cross=1` });
    if (show.gen !== gen) return camLights(null);
    const err = $('#kCamErr');
    if (r.ok && r.records[0] && r.records[0].f.jpeg) { img.src = 'data:image/jpeg;base64,' + r.records[0].f.jpeg; err.textContent = ''; }
    else if (!r.ok) err.textContent = 'camera: ' + r.error;
    setTimeout(frameLoop, r.ok ? 150 : 2000);
  };
  frameLoop();

  // ---- settings shown in the sections ----
  const loadVals = async () => {
    const r = await send({ t: 'cmd', line: 'SETTINGS' });
    if (!r.ok || show.gen !== gen) return;
    vals = Object.fromEntries(r.records.filter((x) => x.type === 'setting').map((x) => [x.f.key, x.f]));
    document.querySelectorAll('.calvals label[data-f]').forEach((l) => {
      const s = vals[l.dataset.f];
      if (!s) { l.hidden = true; return; }
      const name = $('span.dim', l);
      name.textContent = l.dataset.label || s.label || s.key;
      name.title = s.key + (s.help ? ' – ' + s.help : '');
      const ctl = $('.ctl', l);
      if (!ctl.firstChild) {
        // the same controls as the Settings tab: switch for on / off, list for choices, number otherwise
        const k = esc(s.key);
        const step = +s.dec > 0 ? Math.pow(10, -s.dec) : 1;
        ctl.innerHTML = (s.kind === 'bool' ? `<label class="switch"><input type="checkbox" data-k="${k}"><span></span></label>`
          : s.kind === 'choice' ? `<select data-k="${k}">${s.choices.split('|').map((o) => `<option>${esc(o)}</option>`).join('')}</select>`
          : s.kind === 'text' ? `<input data-k="${k}">`
          : `<input type="number" data-k="${k}" step="${step}"><span class="unit dim">${esc(s.unit || '')}</span>`) + '<span class="saved" hidden>✓</span>';
        const el = $('[data-k]', ctl);
        el.onchange = async () => {
          const res = await cmd(`SET ${s.key} ${q(el.type === 'checkbox' ? (el.checked ? '1' : '0') : el.value)}`);
          if (res.ok) {
            const mark = $('.saved', ctl);
            mark.hidden = false;
            setTimeout(() => (mark.hidden = true), 1500);
          }
          loadVals();
        };
      }
      const el = $('[data-k]', ctl);
      if (document.activeElement === el) return;
      if (el.type === 'checkbox') el.checked = s.value === '1';
      else el.value = s.value;
    });
  };
  loadVals();
  send({ t: 'cmd', line: 'FEEDERS' }).then((r) => {
    const sel = $('#fQr');
    if (!sel) return;
    const list = r.ok ? r.records.filter((x) => x.type === 'feeder').map((x) => x.f) : [];
    sel.innerHTML = list.length ? list.map((f) => `<option value="${esc(f.num)}">${esc(f.num)}: ${esc(f.name || f.part || '')}</option>`).join('') : '<option value="">no feeders</option>';
  });

  // ---- actions: each returns the text shown under its step ----
  const P = () => {
    const s = state.status;
    if (s.x === undefined) throw new Error('no position yet');
    return { x: +s.x, y: +s.y, z: +s.z };
  };
  const f3 = (v) => (+v).toFixed(3);
  const set = async (pairs) => {
    for (const [k, v] of pairs) {
      const r = await cmd(`SET ${k} ${v}`);
      if (!r.ok) throw new Error(r.error);
    }
    return pairs.map(([k, v]) => `${k} = ${v}`).join(' · ');
  };
  // side panel picture to this camera (head offsets: watch what it finds)
  const showCam = (c) => { const b = document.querySelector(`[data-cam="${c}"]`); if (b) b.click(); };
  const run = async (line, confirmText) => {
    if (confirmText && !(await confirmBox(confirmText, 'Start'))) return null;
    const r = await cmd(line);
    if (!r.ok) throw new Error(r.error);
    const last = r.records.filter((x) => x.type !== '|').pop();
    return last ? kvText(last) : 'done';
  };
  const ACT = {
    axisMove: async () => {
      const ax = $('#caAxis').value, d = +$('#caDist').value;
      if (!d) throw new Error('enter a distance');
      mem.axis = { ax, d };
      if (ax === 'paste') {
        const area = Math.PI * Math.pow(+(vals.syringe_d || {}).value || 10, 2) / 4;
        await run(`PASTE ${(d * area).toFixed(3)}`);
      } else await run(`JOG ${ax}=${d} speed=${ax === 'z' ? 3 : 20}`);
      return `moved ${CAL_AXES[ax][0]} by ${d} mm: now measure it`;
    },
    axisFix: async () => {
      const m = +$('#caMeas').value, a = mem.axis;
      if (!a) throw new Error('move first');
      if (!m) throw new Error('enter the measured distance');
      const key = 'steps_' + a.ax, old = +(vals[key] || {}).value;
      if (!old) throw new Error('no current value for ' + key);
      return set([[key, (old * a.d / m).toFixed(3)]]);
    },
    reach2: async () => { const p = P(); if (p.z >= 0) throw new Error('Z must be below 0 (nozzle side down). If the nozzle goes down at Z > 0, the seesaw runs backwards: set invert_z 1, then home Z again.'); return set([['head2_reach', f3(-p.z)]]); },
    reach1: async () => { const p = P(); if (p.z <= 0) throw new Error('Z must be above 0 (paste side down). If the paste tip goes down at Z < 0, the seesaw runs backwards: set invert_z 1, then home Z again.'); return set([['head1_reach', f3(p.z)]]); },
    h1mark: async () => { mem.h1 = P(); return `nozzle at X ${mem.h1.x} Y ${mem.h1.y} remembered`; },
    h1off: async () => {
      if (!mem.h1) throw new Error('do step 1/2 first');
      const p = P();
      return set([['head1_offset_x', f3(mem.h1.x - p.x)], ['head1_offset_y', f3(mem.h1.y - p.y)]]);
    },
    calBottom: () => run('CAL BOTTOM', 'The head moves around the bottom camera by a few mm. Start?'),
    calBottomGrid: () => run('CAL BOTTOM GRID', 'Checkerboard face down over the bottom camera, filling the picture. Start?'),
    calTopGrid: () => run('CAL TOP GRID', 'Checkerboard flat under the top camera, filling the picture. The head moves a little. Start?'),
    flFind: async () => {
      const ax = $('#flAxis').value;
      if (!(await confirmBox('The axis homes and runs to its far end. Path clear?', 'Start'))) return null;
      const r = await cmd('FINDLEN ' + ax);
      if (!r.ok) throw new Error(r.error);
      const l = r.records.find((x) => x.type === 'findlen');
      if (!l) return 'done';
      mem.fl = { ax, suggest: l.f.suggest };
      return `length ${l.f.mm} mm · travel would be ${l.f.suggest} mm`;
    },
    flSave: async () => {
      if (!mem.fl) throw new Error('find the length first');
      return set([['travel_' + mem.fl.ax, mem.fl.suggest]]);
    },
    expBottom: () => (showCam('bottom'), run('CAL EXPOSURE BOTTOM')),
    expTop: () => (showCam('top'), run('CAL EXPOSURE TOP')),
    expAuto: async () => (await run('CAL EXPOSURE BOTTOM AUTO')) + ' · ' + (await run('CAL EXPOSURE TOP AUTO')),
    hoNozzleCam: () => (showCam('bottom'), run('CAL HEADS NOZZLE CAM')),
    hoNozzle: () => run('CAL HEADS NOZZLE'),
    hoPasteCam: () => (showCam('bottom'), run('CAL HEADS PASTE CAM')),
    hoPaste: () => run('CAL HEADS PASTE'),
    hoCamera: () => (showCam('top'), run('CAL HEADS CAMERA')),
    hoCameraManual: () => (showCam('top'), run('CAL HEADS CAMERA MANUAL')),
    touchCal: async () => {
      const r = await cmd('SCREEN CALIBRATE');
      if (!r.ok) throw new Error(r.error);
      return 'open on the display: tap the 4 crosses there';
    },
    rotSign: () => run('CAL ROTSIGN'),
    testPart: () => run('CAM bottom MEASURE'),
    calTop: () => run('CAL TOP', 'The head moves around the mark by a few mm. Start?'),
    calBottomLens: () => run('CAL BOTTOM LENS', 'The nozzle tip goes to 9 places across the bottom camera picture. Start?'),
    calTopLens: () => run('CAL TOP LENS', 'The head moves so the mark lands at 9 places across the top camera picture. Start?'),
    topMark: () => run('CAL TOPOFFSET MARK'),
    topOff: () => run('CAL TOPOFFSET'),
    testMark: () => run('CAM top FIDUCIAL'),
    centerMark: async () => {
      const r = await cmd('CAM top FIDUCIAL');
      const f = r.ok && r.records[0] && r.records[0].f;
      if (!f || f.ok !== '1') throw new Error('no mark found');
      await cmd(`JOG x=${f3(f.dx)} y=${f3(f.dy)} speed=20`);
      return `moved by ${(+f.dx).toFixed(2)} / ${(+f.dy).toFixed(2)} mm`;
    },
    lcTare: () => run('LOADCELL TARE'),
    lcCal: () => run(`LOADCELL CAL ${+$('#lcGrams').value || 100}`),
    lcBed: async () => { mem.bed = P(); return `bed at Z ${mem.bed.z} remembered`; },
    lcPad: async () => {
      if (!mem.bed) throw new Error('do step 1/2 first');
      const p = P();
      if (p.z <= mem.bed.z) throw new Error('the pad must be higher than the bed (Z above the bed value)');
      return set([['loadcell_x', p.x], ['loadcell_y', p.y], ['loadcell_height', f3(p.z - mem.bed.z)]]);
    },
    lcTouch: () => run('NOZZLE TOUCH', 'The nozzle goes to the pad and touches it. Start?'),
    rackFirst: async () => { const p = P(); return set([['nozzle_first_x', p.x], ['nozzle_first_y', p.y]]); },
    rackLast: async () => { const p = P(); return set([['nozzle_last_x', p.x], ['nozzle_last_y', p.y]]); },
    rackPick: async () => set([['nozzle_z_pick', P().z]]),
    rackDrop: async () => set([['nozzle_z_drop', P().z]]),
    rackLearn: async () => {
      const box = $('#rackLearnLive');
      let r = await cmd('NOZZLE LEARN START');
      if (!r.ok) throw new Error(r.error);
      renderCalibrate.learning = true;
      for (;;) {
        const f = r.records[0].f;
        if (f.step === 'finished') { box.textContent = ''; return `Saved: slot 1..${f.count} = bit ${f.bits}`; }
        if (!renderCalibrate.learning || !document.contains(box)) break;
        box.textContent = `Trigger slot ${f.next} of ${f.count}` + (f.step === 'two' ? ': two changed at once, trigger it again' : '') + (f.bits ? ` (bits so far: ${f.bits})` : '');
        await new Promise((go) => setTimeout(go, 250));
        r = await cmd('NOZZLE LEARN');
        if (!r.ok) { box.textContent = ''; throw new Error(r.error); }
      }
      box.textContent = '';
      await cmd('NOZZLE LEARN CANCEL');
      return 'Cancelled, nothing changed';
    },
    rackLearnStop: async () => { renderCalibrate.learning = false; return null; },
    rackPut: () => run('NOZZLE CHANGE 0'),
    rackOne: () => run('NOZZLE CHANGE 1'),
    vacBase: () => run('CAL VACUUM BASE'),
    vacPump: () => run(+state.status.pump > 0 ? 'PUMP OFF' : 'PUMP ON'),
    vacOpen: () => run('CAL VACUUM OPEN'),
    vacBlocked: () => run('CAL VACUUM BLOCKED'),
    pasteDot: () => run(`PASTE ${+$('#pAmt').value || 0.5}`),
    pastePrime: () => run('PASTE 5'),
    qrTeach: async () => {
      const n = $('#fQr').value;
      if (!n) throw new Error('no feeder');
      return run(`FEEDER QR TEACH ${n}`, 'The top camera is centred on this feeder\'s QR code right now?');
    },
    qrRead: () => run('CAM top QR'),
    rejectHere: async () => { const p = P(); return set([['reject_x', p.x], ['reject_y', p.y]]); },
    joyCenter: () => run('CAL JOY CENTER'),
    joyRange: () => run('CAL JOY RANGE ms=5000'),
    // homing: single axes, StallGuard measurement (SGTEST) and threshold
    homeX: () => run('HOME X'),
    homeY: () => run('HOME Y'),
    homeZ: () => run('HOME Z'),
    homeE1: () => run('HOME E1'),
    homeAll: () => run('HOME'),
    sgJogM: () => run(`JOG ${$('#sgAxis').value}=-10 speed=20`),
    sgJogP: () => run(`JOG ${$('#sgAxis').value}=10 speed=20`),
    sgMeasure: async () => {
      const ax = $('#sgAxis').value, mm = +$('#sgMm').value || 50;
      if (!(await confirmBox(`${ax.toUpperCase()} runs ${mm} mm towards its home end, like homing, and pushes into the end without stopping. Start it less than ${mm} mm from the end. Go?`, 'Start'))) return null;
      sgLive = { min: [Infinity, Infinity], max: [-1, -1] };
      const live = document.getElementById('sgLive');
      if (live) live.textContent = 'waiting for values ...';
      const r = await cmd(`SGTEST ${ax} ${mm}`);
      if (!r.ok) throw new Error(r.error);
      const recs = r.records.filter((x) => x.type === 'sgtest').map((x) => x.f);
      mem.sg = { ax, recs };
      return recs.map((f) => `${f.axis}: min ${f.min} (end) · avg ${f.avg} · max ${f.max} (free) → suggested tmc_sg ${f.suggest}` +
        (+f.stall ? '' : ' - but min is not clearly below max: did it reach the end?')).join('   |   ');
    },
    sgUse: async () => {
      const m = mem.sg;
      if (!m || m.ax !== $('#sgAxis').value) throw new Error('measure this axis first');
      return set(m.recs.map((f) => ['tmc_sg_' + f.axis, f.suggest]));
    },
    sgHome: () => run('HOME ' + { x: 'X', y: 'Y', e1: 'E1' }[$('#sgAxis').value]),
  };
  document.querySelectorAll('[data-act]').forEach((b) => (b.onclick = async () => {
    const res = b.closest('.calstep').querySelector('.calres');
    b.disabled = true;
    res.className = 'calres dim';
    res.textContent = '…';
    try {
      const t = await ACT[b.dataset.act]();
      res.textContent = t === null ? '' : t;
      if (t !== null) res.className = 'calres ok-text';
    } catch (e) {
      res.textContent = e.message;
      res.className = 'calres error';
    }
    b.disabled = false;
    loadVals();
  }));
  const axisSel = () => {
    const [, unit, d] = CAL_AXES[$('#caAxis').value];
    $('#caDist').value = d;
    $('#caUnit').textContent = unit;
  };
  $('#caAxis').onchange = axisSel;
  axisSel();
  updateCalibrate();

  // ---- live readings ----
  show.poll = setInterval(async () => {
    const [lc, v, j] = await Promise.all(['LOADCELL', 'VACUUM', 'JOY'].map((line) => send({ t: 'cmd', line })));
    if (show.gen !== gen) return;
    const one = (r) => (r.ok && r.records[0] && r.records[0].f) || null;
    const l = one(lc), vf = one(v), jf = one(j);
    $('#lcLive').innerHTML = l ? (+l.ok ? `<b>${(+l.g).toFixed(1)} g</b> <span class="dim">raw ${esc(l.raw)} · tare ${esc(l.tare)} · scale ${esc(l.scale)}</span>` : '<span class="error">no load cell found (HX711 on EXP1)</span>')
      : `<span class="error">${esc(lc.error || 'no reading')}</span>`;
    $('#vacLive').innerHTML = vf && +vf.sensor ? `<b>${esc(vf.hpa)} hPa</b> <span class="dim">baseline ${esc(vf.baseline)} · drop ${esc(vf.drop)} · pick needs ${esc(vf.pick_min)} · part ${+vf.part ? 'yes' : 'no'}</span>` : '<span class="error">no vacuum sensor (BMP280)</span>';
    $('#joyLive').innerHTML = jf && +jf.adc ? `<b>x ${esc(jf.x)} · y ${esc(jf.y)}</b> <span class="dim">raw ${esc(jf.raw_x)} / ${esc(jf.raw_y)}${+jf.button ? ' · button' : ''}</span>` : '<span class="error">no joystick ADC (ADS1115)</span>';
  }, 700);
}

function updateCalibrate() {
  const s = state.status, ax = $('#kAxes');
  if (!ax) return;
  ax.innerHTML = ['x', 'y', 'z', 'rot', 'e1'].map((a) => `<div><span class="dim">${a.toUpperCase()}</span><b>${s[a] ?? '–'}</b></div>`).join('');
  const cam = renderCalibrate.cam || 'top';
  const on = +(s['light_' + cam] || 0) > 0;
  $('#kLight').textContent = on ? 'Light on' : 'Light off';
  $('#kLight').classList.toggle('primary', on);
}

// ---- Display mirror ------------------------------------------------------------
// The machine's display as a picture (SCREEN); a click is a touch, the keys stand in for the joystick.
function renderDisplay() {
  const gen = show.gen;
  $('#view').innerHTML = `<div class="card"><div class="row spread"><h2>Machine display</h2><span class="dim">Click = touch · keys: ↑ ↓ Enter Esc</span></div>
    <img id="scr" class="screen" alt="display" tabindex="0">
    <div class="row" style="margin-top:10px"><button data-key="up">▲ Up</button><button data-key="down">▼ Down</button>
      <button data-key="press" class="primary">Press</button><button data-key="hold">Back</button></div>
    <p class="dim">Turn 180° and touch calibration: Settings → Machine display.</p></div>`;
  const img = $('#scr');
  const loop = async () => {
    if (show.gen !== gen) return;
    const r = await send({ t: 'cmd', line: 'SCREEN q=75' });
    if (show.gen !== gen) return;
    if (r.ok && r.records[0] && r.records[0].f.jpeg) img.src = 'data:image/jpeg;base64,' + r.records[0].f.jpeg;
    setTimeout(loop, r.ok ? 250 : 2000);
  };
  loop();
  img.onclick = async (e) => {
    const r = img.getBoundingClientRect();
    const x = Math.round(((e.clientX - r.left) / r.width) * (img.naturalWidth || 480));
    const y = Math.round(((e.clientY - r.top) / r.height) * (img.naturalHeight || 320));
    await send({ t: 'cmd', line: `SCREEN TOUCH ${x} ${y}` });
    setTimeout(() => send({ t: 'cmd', line: 'SCREEN RELEASE' }), 120);
  };
  document.querySelectorAll('[data-key]').forEach((b) => (b.onclick = () => send({ t: 'cmd', line: 'SCREEN KEY ' + b.dataset.key })));
  img.onkeydown = (e) => {
    const k = { ArrowUp: 'up', ArrowDown: 'down', Enter: 'press', Escape: 'hold', Backspace: 'hold' }[e.key];
    if (k) { send({ t: 'cmd', line: 'SCREEN KEY ' + k }); e.preventDefault(); }
  };
}

// ---- Settings -----------------------------------------------------------------
// The catalog (SETTINGS): every setting with group, label, unit, range and help, searchable.
async function renderSettings() {
  $('#view').innerHTML = `
  <div class="settings-top card">
    <div class="row spread"><h2>Settings</h2>
      <div class="row"><button class="small" id="sOpenAll">Open all</button><button class="small" id="sCloseAll">Close all</button>
        <button class="small" id="sReload">Reload from files</button><button class="small danger" id="sDefaults">Defaults…</button></div></div>
    <input id="sSearch" class="search" type="search" placeholder="Search: name, key or description – e.g. speed, nozzle, current, camera" autocomplete="off">
    <div id="sCount" class="dim"></div>
  </div>
  <div class="card"><h2>Machine display</h2>
    <div class="row spread" style="margin-top:8px"><div><b>Turn 180°</b><div class="dim">Picture upside down; the touch turns with it. Saved on the Pi.</div></div>
      <button id="dTurn">…</button></div>
    <div class="dim" style="margin-top:12px">Touch calibration: Calibrate → Touch screen.</div></div>
  <div id="sGroups"><div class="card dim">Loading…</div></div>
  <div class="card"><div class="row spread"><h2>Motion board SD card</h2><button class="small" id="osOpen">Show the card</button></div>
    <p class="dim">Jobs run from the Pi. Files on the microSD in the Octopus slot can be copied over (into <b>/jobs</b>), then run from there.</p>
    <div id="octoSd"></div></div>
  <div class="card"><h2>Raspberry Pi &amp; website</h2><div class="kv" id="sysInfo"></div>
    <p class="dim">WiFi: on the display (Menu → Network &amp; website) or with <code>nmcli</code> on the Pi.</p>
    <div class="row" style="margin-top:12px"><input type="password" id="wPass" placeholder="New website password"><button id="wSetPass">Change</button>
      <button id="logout">Log out</button></div>
  </div>`;
  $('#sReload').onclick = async () => { await cmd('RELOAD'); renderSettings(); };
  $('#sDefaults').onclick = async () => { if (await confirmBox('Set all settings back to their defaults?', 'Reset', true)) { await cmd('DEFAULTS'); renderSettings(); } };
  $('#wSetPass').onclick = async () => {
    const r = await sys('password', $('#wPass').value);
    toast(r.ok ? 'Password changed – log in again' : r.error);
    if (r.ok) setTimeout(() => location.reload(), 1500);
  };
  $('#logout').onclick = async () => { await fetch('/api/logout', { method: 'POST' }); location.reload(); };
  loadSys();
  $('#osOpen').onclick = () => renderOctoSd('/');

  // Machine display: turned 180° (SCREEN TURN, ui.txt tft_rot) and touch calibration (SCREEN CALIBRATE)
  const showTurn = (r) => {
    const b = $('#dTurn');
    if (!b) return;
    if (!r.ok) { b.textContent = 'no display'; b.disabled = true; return; }
    const t = r.records[0] && +r.records[0].f.turned;
    b.disabled = false;
    b.textContent = t ? 'Turned 180° ✓' : 'Turn 180°';
    b.classList.toggle('primary', !!t);
    b.dataset.turned = t ? '1' : '0';
  };
  send({ t: 'cmd', line: 'SCREEN TURN' }).then(showTurn);
  $('#dTurn').onclick = async () => showTurn(await cmd('SCREEN TURN ' + ($('#dTurn').dataset.turned === '1' ? 0 : 1)));

  const r = await cmd('SETTINGS', { quiet: true });
  const box = $('#sGroups');
  if (!box) return;
  if (!r.ok) { box.innerHTML = `<div class="card error">${esc(r.error)}</div>`; return; }
  const all = r.records.filter((x) => x.type === 'setting').map((x) => x.f);
  const groups = [];
  for (const s of all) if (!groups.includes(s.group)) groups.push(s.group);
  const open = renderSettings.open || (renderSettings.open = new Set(['Job & placing']));

  const input = (s) => {
    const k = esc(s.key);
    if (s.kind === 'bool') return `<label class="switch"><input type="checkbox" data-k="${k}" ${s.value === '1' ? 'checked' : ''}><span></span></label>`;
    if (s.kind === 'choice')
      return `<select data-k="${k}">${s.choices.split('|').map((o) => `<option ${o === s.value ? 'selected' : ''}>${esc(o)}</option>`).join('')}</select>`;
    if (s.kind === 'text') return `<input data-k="${k}" value="${esc(s.value)}" class="wide">`;
    const step = +s.dec > 0 ? Math.pow(10, -s.dec) : 1;
    return `<input type="number" data-k="${k}" value="${esc(s.value)}" step="${step}" min="${esc(s.min)}" max="${esc(s.max)}">` +
      (s.unit ? `<span class="unit">${esc(s.unit)}</span>` : '');
  };
  const row = (s) => `<div class="setting" data-k="${esc(s.key)}">
      <div class="label"><b>${esc(s.label)}</b> <code class="key">${esc(s.key)}</code>
        ${s.help ? `<div class="dim help">${esc(s.help)}</div>` : ''}</div>
      <div class="field">${input(s)}<span class="saved" hidden>✓</span></div></div>`;

  const draw = () => {
    const term = $('#sSearch').value.trim().toLowerCase();
    const match = (s) => !term || (s.key + ' ' + s.label + ' ' + s.group + ' ' + s.help).toLowerCase().includes(term);
    let shown = 0;
    box.innerHTML = groups.map((g) => {
      const list = all.filter((s) => s.group === g && match(s));
      if (!list.length) return '';
      shown += list.length;
      const isOpen = term || open.has(g);
      return `<details class="card group" data-g="${esc(g)}" ${isOpen ? 'open' : ''}>
        <summary><h2>${esc(g)}</h2><span class="dim">${list.length}</span></summary>${list.map(row).join('')}</details>`;
    }).join('') || '<div class="card dim">No setting matches.</div>';
    $('#sCount').textContent = term ? `${shown} of ${all.length} settings` : `${all.length} settings in ${groups.length} groups`;
    box.querySelectorAll('details.group').forEach((d) => (d.ontoggle = () => {
      if (term) return;
      if (d.open) open.add(d.dataset.g); else open.delete(d.dataset.g);
    }));
    box.querySelectorAll('[data-k]').forEach((el) => {
      if (el.classList.contains('setting')) return;
      el.onchange = async () => {
        const key = el.dataset.k;
        const value = el.type === 'checkbox' ? (el.checked ? '1' : '0') : el.value;
        const res = await cmd(`SET ${key} ${q(value)}`, { quiet: true });
        const s = all.find((x) => x.key === key);
        if (res.ok) {
          const v = res.records[0] && res.records[0].f.value;
          if (s && v !== undefined) s.value = v;
          if (el.type !== 'checkbox' && v !== undefined) el.value = v;
          const mark = el.closest('.setting').querySelector('.saved');
          mark.hidden = false;
          setTimeout(() => (mark.hidden = true), 1500);
        } else {
          toast(key + ': ' + res.error);
          if (s) { if (el.type === 'checkbox') el.checked = s.value === '1'; else el.value = s.value; }
        }
      };
    });
  };
  $('#sSearch').oninput = draw;
  $('#sSearch').value = renderSettings.term || '';
  $('#sSearch').addEventListener('input', () => (renderSettings.term = $('#sSearch').value));
  $('#sOpenAll').onclick = () => { groups.forEach((g) => open.add(g)); draw(); };
  $('#sCloseAll').onclick = () => { open.clear(); draw(); };
  draw();
}

// OCTOSD LS / COPY: browse the Octopus card, copy a file into the Pi's /jobs folder
async function renderOctoSd(dir) {
  const box = $('#octoSd');
  if (!box) return;
  box.innerHTML = '<p class="dim">Reading the card…</p>';
  const r = await cmd(`OCTOSD LS ${q(dir)}`, { quiet: true });
  if (!$('#octoSd')) return;
  if (!r.ok) { box.innerHTML = `<p class="error">${esc(r.error)}</p><p class="dim">Card in the Octopus slot? Motion standing still?</p>`; return; }
  const parts = dir.split('/').filter(Boolean);
  const crumbs = ['<a data-p="/">card</a>'].concat(parts.map((p, i) => `<a data-p="/${parts.slice(0, i + 1).join('/')}">${esc(p)}</a>`)).join(' / ');
  const entries = r.records.filter((x) => x.type === 'file' && !x.f.name.startsWith('.') && x.f.name !== 'System Volume Information').map((x) => x.f);
  entries.sort((a, b) => (+b.dir - +a.dir) || a.name.localeCompare(b.name));
  box.innerHTML = `<div class="crumbs">${crumbs}</div><table><tbody>` +
    entries.map((e) => +e.dir
      ? `<tr class="clickable" data-dir="${esc(e.name)}"><td>📁 ${esc(e.name)}</td><td></td><td></td></tr>`
      : `<tr data-file="${esc(e.name)}"><td>${/\.pap$/i.test(e.name) ? `<b>${esc(e.name)}</b>` : esc(e.name)}</td><td class="dim">${(+e.size / 1024).toFixed(1)} KB</td>
         <td class="actions"><button class="small primary" data-copy>Copy to the Pi</button></td></tr>`).join('') +
    (entries.length ? '' : '<tr><td class="dim">Empty folder.</td></tr>') + '</tbody></table>';
  box.querySelectorAll('.crumbs a').forEach((a) => (a.onclick = () => renderOctoSd(a.dataset.p)));
  box.querySelectorAll('tr[data-dir]').forEach((tr) => (tr.onclick = () => renderOctoSd(join(dir, tr.dataset.dir))));
  box.querySelectorAll('button[data-copy]').forEach((b) => (b.onclick = async () => {
    const src = join(dir, b.closest('tr').dataset.file);
    b.disabled = true;
    b.textContent = 'Copying…';
    const c = await cmd(`OCTOSD COPY ${q(src)}`);
    b.disabled = false;
    b.textContent = 'Copy to the Pi';
    const saved = c.ok && c.records.find((x) => x.type === 'saved');
    if (!saved) return;
    const to = saved.f.path;
    if (await confirmBox(`Copied to the Pi: ${to}. It runs from there like every job.`, 'Show jobs')) {
      state.cwd = to.slice(0, to.lastIndexOf('/')) || '/';
      show('jobs');
    }
  }));
}

async function loadSys() {
  const r = await sys('info');
  const e = r.sys || {};
  const el = $('#sysInfo');
  if (!el) return;
  el.innerHTML = `<span>Name</span><span>${esc(e.hostname)} · ${esc(e.hostname)}.local</span><span>Address</span><span>${esc(e.ip || '–')}</span>
    <span>Controller</span><span>${e.link ? 'pap-core running' : '<b class="error">pap-core offline</b>'}</span>
    <span>System</span><span class="dim">up ${fmtTime(e.uptime)} · load ${esc(e.load)} · ${e.memFree} / ${e.memTotal} MB free</span>`;
}

// ---- Console ------------------------------------------------------------------
const consoleLines = [];
function logConsole(text, cls = '') {
  consoleLines.push({ text, cls });
  if (consoleLines.length > 500) consoleLines.shift();
  const c = $('#console');
  if (c) { c.insertAdjacentHTML('beforeend', `<div class="${cls}">${esc(text)}</div>`); c.scrollTop = c.scrollHeight; }
}

function renderConsole() {
  $('#view').innerHTML = `<div class="card"><div class="row spread"><h2>Console</h2><span class="dim">Commands: see protocol.md · HELP lists them</span></div>
    <div class="console" id="console">${consoleLines.map((l) => `<div class="${l.cls}">${esc(l.text)}</div>`).join('')}</div>
    <form class="row" id="cForm" style="margin-top:10px"><input id="cLine" style="flex:1" placeholder="e.g. STATUS, LS /jobs, FEEDER 3, SET pick_height 1.5" autocomplete="off"><button class="primary">Send</button></form></div>`;
  const c = $('#console');
  c.scrollTop = c.scrollHeight;
  const history = renderConsole.history || (renderConsole.history = []);
  let hi = history.length;
  $('#cForm').onsubmit = (e) => {
    e.preventDefault();
    const line = $('#cLine').value.trim();
    if (!line) return;
    history.push(line);
    hi = history.length;
    $('#cLine').value = '';
    cmd(line, { quiet: true });
  };
  $('#cLine').onkeydown = (e) => {
    if (e.key === 'ArrowUp' && hi > 0) { $('#cLine').value = history[--hi]; e.preventDefault(); }
    if (e.key === 'ArrowDown') { hi = Math.min(history.length, hi + 1); $('#cLine').value = history[hi] || ''; e.preventDefault(); }
  };
  $('#cLine').focus();
}

// ---------------------------------------------------------------- start
$('#estop').onclick = () => cmd('ESTOP');

async function checkSession() {
  const r = await fetch('/api/session').catch(() => null);
  if (r && r.ok) {
    $('#login').hidden = true;
    $('#app').hidden = false;
    connect();
    show(state.view);
  } else if (r && r.status === 401) {
    $('#app').hidden = true;
    $('#login').hidden = false;
    $('#password').focus();
  } else setTimeout(checkSession, 2000);
}

$('#loginForm').onsubmit = async (e) => {
  e.preventDefault();
  const body = new URLSearchParams({ password: $('#password').value });
  const r = await fetch('/api/login', { method: 'POST', body });
  $('#loginError').hidden = r.ok;
  if (r.ok) checkSession();
};

checkSession();

'use strict';
const $ = id => document.getElementById(id);
const MAX_SELECTED = 8;  // one colour each
const CHARTS = [
  { key: 'tempF', title: 'Temperature', unit: '°F', digits: 1, minSpan: 4 },
  { key: 'heatPct', title: 'Heating', unit: '% of time', digits: 0, fixed: [0, 100] },
  { key: 'humidity', title: 'Humidity', unit: '%', digits: 0, minSpan: 10 },
  { key: 'pressureHpa', title: 'Pressure', unit: 'hPa', digits: 1, minSpan: 4 },
];

// Storage can be unavailable (private windows), so every use may fail quietly.
function stored(area, key, value) {
  try {
    if (value === undefined) return window[area].getItem(key);
    if (value === null) window[area].removeItem(key); else window[area].setItem(key, value);
  } catch { /* not kept */ }
  return null;
}

let token = stored('localStorage', 'token') ?? stored('sessionStorage', 'token') ?? '';
let devices = [];
let slots = {};       // device id -> colour slot, kept while the device is selected
let view = null;      // what is drawn: { start, end, step, series }
let loadNumber = 0;
try { slots = JSON.parse(stored('localStorage', 'slots') ?? '{}') ?? {}; } catch { slots = {}; }
let remembered = Object.keys(slots).length > 0;

const nameOf = d => d.name || `Thermostat ${d.id.slice(-4)}`;
const color = id => `var(--s${slots[id]})`;

function make(tag, className, text) {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text !== undefined) e.textContent = text;
  return e;
}

function svg(tag, attrs, parent, text) {
  const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (text !== undefined) e.textContent = text;
  parent?.append(e);
  return e;
}

async function get(path) {
  const r = await fetch('/api/v1' + path, { headers: { 'X-Api-Token': token }, cache: 'no-store' });
  if (r.status === 401) throw new Error('token');
  if (!r.ok) throw new Error(`The server answered ${r.status}.`);
  return r.json();
}

function showLogin(text) {
  $('login').hidden = false;
  $('main').hidden = $('forget').hidden = true;
  $('message').textContent = text;
}

function report(e) {
  if (e.message === 'token') {
    const tried = token !== '';
    forget();
    showLogin(tried ? 'That token was not accepted.' : '');
  } else {
    $('message').textContent = e instanceof TypeError ? 'The server is not answering.' : e.message;
  }
}

function forget() {
  loadNumber++;  // an answer still on its way is not shown
  token = '';
  stored('localStorage', 'token', null);
  stored('sessionStorage', 'token', null);
}

function age(t) {
  if (t === null) return 'no records yet';
  const s = Date.now() / 1000 - t - 300;  // a record is sent at the end of its 5 minutes
  if (s < 600) return 'up to date';
  if (s < 7200) return `${Math.round(s / 60)} min ago`;
  if (s < 172800) return `${Math.round(s / 3600)} h ago`;
  return `${Math.round(s / 86400)} days ago`;
}

function select(id) {
  const used = new Set(Object.values(slots));
  for (let i = 0; i < MAX_SELECTED; i++) {
    if (!used.has(i)) { slots[id] = i; return; }
  }
}

async function loadDevices(number) {
  const list = await get('/devices');
  if (number !== loadNumber) return false;
  devices = list;
  for (const id of Object.keys(slots)) {
    const s = slots[id];
    if (!devices.some(d => d.id === id) || !Number.isInteger(s) || s < 0 || s >= MAX_SELECTED) delete slots[id];
  }
  if (!remembered && devices.length > 0) {  // first visit: show what fits
    devices.slice(0, MAX_SELECTED).forEach(d => select(d.id));
    remembered = true;
  }
  drawDevices();
  return true;
}

function drawDevices() {
  const focused = document.activeElement?.dataset?.device;
  $('devices').replaceChildren(...devices.map(d => {
    const label = make('label', 'device');
    const box = make('input');
    box.type = 'checkbox';
    box.dataset.device = d.id;
    box.checked = d.id in slots;
    box.disabled = !box.checked && Object.keys(slots).length >= MAX_SELECTED;
    box.addEventListener('change', () => {
      if (box.checked) select(d.id); else delete slots[d.id];
      remembered = true;
      stored('localStorage', 'slots', JSON.stringify(slots));
      load();
    });
    const key = make('span', 'key');
    if (box.checked) key.style.background = color(d.id);
    label.append(box, key, make('span', '', nameOf(d)), make('small', '', age(d.lastReading)));
    return label;
  }));
  if (focused) $('devices').querySelector(`[data-device="${focused}"]`)?.focus();
}

// Null if the custom dates are not usable.
function range() {
  let end = Math.ceil(Date.now() / 1000);
  let start;
  if ($('range').value === 'custom') {
    const from = new Date($('from').value + 'T00:00');
    const to = new Date($('to').value + 'T00:00');
    to.setDate(to.getDate() + 1);  // through the end of that day
    start = from.getTime() / 1000;
    end = to.getTime() / 1000;
    if (!(start >= 0 && start < end && end - start <= 10 * 365 * 86400 && end < 1e10)) return null;
  } else {
    start = end - Number($('range').value) * 86400;
  }
  const days = (end - start) / 86400;
  const step = days <= 2 ? 300 : days <= 8 ? 900 : days <= 45 ? 3600 : days <= 400 ? 21600 : 86400;
  return { start, end, step };
}

async function load() {
  if (!token) return showLogin('');
  const number = ++loadNumber;
  $('charts').classList.add('loading');
  try {
    if (!await loadDevices(number)) return;
    const r = range();
    if (!r) {
      view = null;
      draw();
      $('message').textContent = 'Choose a From date that is not after the To date.';
      return;
    }
    const bucket = r.step > 300 ? `&bucket=${r.step}` : '';
    const chosen = devices.filter(d => d.id in slots);
    const series = await Promise.all(chosen.map(async d => {
      const a = await get(`/devices/${d.id}/readings?from=${r.start}&to=${r.end}${bucket}`);
      return { id: d.id, name: nameOf(d), truncated: a.truncated,
        rows: a.readings, byTime: new Map(a.readings.map(x => [x.time, x])) };
    }));
    if (number !== loadNumber) return;
    view = { ...r, series };
    $('login').hidden = true;
    $('main').hidden = $('forget').hidden = false;
    $('message').textContent = devices.length === 0 ? 'No thermostat has sent a record yet.'
      : chosen.length === 0 ? 'Select a thermostat.'
      : series.some(s => s.truncated) ? 'Some records did not fit and are not shown.'
      : series.every(s => s.rows.length === 0) ? 'No records in this range.' : '';
    draw();
  } catch (e) {
    if (number === loadNumber) report(e);
  } finally {
    if (number === loadNumber) $('charts').classList.remove('loading');
  }
}

function yTicks(lo, hi) {
  const raw = (hi - lo) / 4;
  const power = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 5, 10].map(m => m * power).find(s => s >= raw);
  const ticks = [];
  for (let i = Math.ceil(lo / step - 1e-9); i * step <= hi + 1e-9; i++) ticks.push(Number((i * step).toFixed(6)));
  return ticks;
}

// Local clock positions, so days and hours stay right across daylight saving.
function xTicks(start, end, most) {
  const hours = (end - start) / 3600;
  const step = [1, 2, 3, 6, 12, 24, 48, 72, 120, 168, 336, 720, 1440, 2160, 2880, 4320, 8760].find(h => hours / h <= most) ?? 17520;
  const format = step < 24 ? { hour: 'numeric' } : hours > 400 * 24 ? { month: 'short', year: 'numeric' }
    : { month: 'short', day: 'numeric' };
  const t = new Date(start * 1000);
  t.setHours(0, 0, 0, 0);
  const ticks = [];
  while (t.getTime() / 1000 < end) {
    const s = t.getTime() / 1000;
    if (s >= start) {
      const midnight = t.getHours() === 0 && step < 24;
      ticks.push([s, t.toLocaleString([], midnight ? { month: 'short', day: 'numeric' } : format)]);
    }
    if (step < 24) t.setHours(t.getHours() + step); else t.setDate(t.getDate() + step / 24);
  }
  return ticks;
}

// A gap in the records breaks the line. A lone point becomes a dot (round caps).
function path(rows, key, step, x, y) {
  let d = '', last = null, length = 0;
  for (const r of rows) {
    const v = r[key];
    if (v === null) { if (length === 1) d += 'h0'; last = null; length = 0; continue; }
    const joined = last !== null && r.time - last <= step * 1.5;
    if (!joined) { if (length === 1) d += 'h0'; length = 0; }
    d += `${joined ? 'L' : 'M'}${x(r.time).toFixed(1)} ${y(v).toFixed(1)}`;
    last = r.time;
    length++;
  }
  return length === 1 ? d + 'h0' : d;
}

function draw() {
  hideTip();
  const cards = [];
  if (view) {
    for (const c of CHARTS) {
      if (view.series.some(s => s.rows.some(r => r[c.key] !== null))) cards.push(c);
    }
  }
  const box = $('charts');
  const width = Math.max(260, box.clientWidth - 26);
  box.replaceChildren(...cards.map(c => chart(c, width)));
  drawTable();
}

function chart(c, width) {
  const { start, end, step, series } = view;
  const height = c.key === 'tempF' ? 300 : 170;
  const left = 46, right = 10, top = 10, bottom = 26;
  const showSet = c.key === 'tempF' && $('setpoint').checked;

  let [lo, hi] = c.fixed ?? [Infinity, -Infinity];
  if (!c.fixed) {
    for (const s of series) for (const r of s.rows) {
      for (const v of [r[c.key], showSet ? r.setpointF : null]) {
        if (v !== null) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
      }
    }
    const pad = Math.max((hi - lo) * 0.08, (c.minSpan - (hi - lo)) / 2);
    lo -= pad;
    hi += pad;
  }
  // A record is drawn at the middle of its interval.
  const px = t => left + (t - start) / (end - start) * (width - left - right);
  const x = t => px(t + step / 2);
  const y = v => top + (hi - v) / (hi - lo) * (height - top - bottom);

  const card = make('section', 'card');
  const h = make('h2', '', c.title + ' ');
  h.append(make('span', '', `${c.unit}${step > 300 ? `, ${stepText(step)} averages` : ''}`));
  const g = svg('svg', { viewBox: `0 0 ${width} ${height}`, height, tabindex: 0, role: 'img',
    'aria-label': `${c.title} chart. Left and right arrow keys read the values; the table below has them all.` });
  card.append(h, g);

  for (const t of yTicks(lo, hi)) {
    svg('line', { class: 'grid', x1: left, x2: width - right, y1: y(t), y2: y(t) }, g);
    svg('text', { x: left - 6, y: y(t) + 4, 'text-anchor': 'end' }, g, String(t));
  }
  for (const [t, text] of xTicks(start, end, Math.max(2, Math.floor(width / 70)))) {
    const half = text.length * 3.5;
    if (px(t) - half < 0 || px(t) + half > width) continue;
    svg('text', { x: px(t), y: height - 6, 'text-anchor': 'middle' }, g, text);
  }
  svg('line', { class: 'axis', x1: left, x2: width - right, y1: height - bottom, y2: height - bottom }, g);

  // The first and last records can start just outside the range.
  const clip = `clip-${c.key}`;
  svg('rect', { x: left, y: 0, width: width - left - right, height }, svg('clipPath', { id: clip }, g));
  const marks = svg('g', { 'clip-path': `url(#${clip})` }, g);
  for (const s of series) {
    if (showSet) svg('path', { class: 'line set', d: path(s.rows, 'setpointF', step, x, y) }, marks).style.stroke = color(s.id);
  }
  for (const s of series) {
    svg('path', { class: 'line', d: path(s.rows, c.key, step, x, y) }, marks).style.stroke = color(s.id);
  }

  const cross = svg('line', { class: 'cross', y1: top, y2: height - bottom, visibility: 'hidden' }, g);
  const times = [...new Set(series.flatMap(s => s.rows.map(r => r.time)))].sort((a, b) => a - b);
  let at = -1;
  const show = (i, clientX, clientY) => {
    at = Math.max(0, Math.min(times.length - 1, i));
    const cx = x(times[at]);
    cross.setAttribute('x1', cx);
    cross.setAttribute('x2', cx);
    cross.setAttribute('visibility', 'visible');
    showTip(c, times[at], showSet, clientX, clientY);
  };
  const hide = () => { cross.setAttribute('visibility', 'hidden'); hideTip(); };
  const pointed = e => {
    const b = g.getBoundingClientRect();
    const t = start + ((e.clientX - b.left) * width / b.width - left) / (width - left - right) * (end - start) - step / 2;
    let a = 0, z = times.length - 1;  // nearest record
    while (a < z) { const m = (a + z) >> 1; if (times[m] < t) a = m + 1; else z = m; }
    show(a > 0 && t - times[a - 1] < times[a] - t ? a - 1 : a, e.clientX, e.clientY);
  };
  g.addEventListener('pointermove', pointed);
  g.addEventListener('pointerdown', pointed);
  g.addEventListener('pointerleave', hide);
  g.addEventListener('blur', hide);
  g.addEventListener('keydown', e => {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    const i = at < 0 ? times.length - 1 : at + (e.key === 'ArrowLeft' ? -1 : 1);
    const b = g.getBoundingClientRect();
    const clamped = Math.max(0, Math.min(times.length - 1, i));
    show(clamped, b.left + x(times[clamped]) * b.width / width, b.top + 40);
  });
  return card;
}

const stepText = s => s < 3600 ? `${s / 60}-minute` : s < 86400 ? `${s / 3600}-hour` : 'daily';
const when = t => new Date(t * 1000).toLocaleString([], { month: 'short', day: 'numeric', year: 'numeric', hour: 'numeric', minute: '2-digit' });
const number = (v, digits) => v === null || v === undefined ? '–' : v.toFixed(digits);

function showTip(c, time, showSet, clientX, clientY) {
  const tip = $('tip');
  tip.replaceChildren(make('div', 'when', when(time)));
  for (const s of view.series) {
    const r = s.byTime.get(time);
    if (!r) continue;
    const row = make('div', 'row');
    const key = make('span', 'key');
    key.style.background = color(s.id);
    let detail = s.name;
    if (r.sensorFault) detail += ', sensor fault';
    else if (c.key === 'tempF' && view.step > 300 && r.tempMinF !== null) {
      detail += `, ${number(r.tempMinF, 1)} to ${number(r.tempMaxF, 1)}`;
    }
    if (showSet) detail += `, set ${number(r.setpointF, view.step > 300 ? 1 : 0)}`;
    row.append(key, make('strong', '', number(r[c.key], c.digits)), make('span', '', detail));
    tip.append(row);
  }
  tip.hidden = false;
  const b = tip.getBoundingClientRect();
  const x = clientX + 14 + b.width > innerWidth ? clientX - 14 - b.width : clientX + 14;
  const y = Math.min(clientY + 14, innerHeight - b.height - 8);
  tip.style.left = `${Math.max(8, x)}px`;
  tip.style.top = `${Math.max(8, y)}px`;
}

function hideTip() { $('tip').hidden = true; }

const TABLE_ROWS = 500;
function drawTable() {
  const box = $('table');
  box.replaceChildren();
  if (!$('tableBox').open || !view) return;
  const rows = view.series.flatMap(s => s.rows.map(r => [s, r]))
    .sort((a, b) => b[1].time - a[1].time || a[0].name.localeCompare(b[0].name));
  const table = make('table');
  const head = make('tr');
  for (const t of ['Time', 'Thermostat', 'Temp °F', 'Set °F', 'Heating %', 'Humidity %', 'Pressure hPa']) {
    const th = make('th', '', t);
    th.scope = 'col';
    head.append(th);
  }
  table.append(make('thead'), make('tbody'));
  table.tHead.append(head);
  for (const [s, r] of rows.slice(0, TABLE_ROWS)) {
    const tr = make('tr');
    const cells = [when(r.time), s.name, r.sensorFault ? 'sensor fault' : number(r.tempF, 1), number(r.setpointF, 1),
      number(r.heatPct, 0), number(r.humidity, 0), number(r.pressureHpa, 1)];
    for (const t of cells) tr.append(make('td', '', t));
    table.tBodies[0].append(tr);
  }
  box.append(table);
  if (rows.length > TABLE_ROWS) box.append(make('p', '', `The newest ${TABLE_ROWS} of ${rows.length} rows.`));
}

$('login').addEventListener('submit', e => {
  e.preventDefault();
  token = $('token').value.trim();
  $('token').value = '';
  if (!/^[\x21-\x7e]+$/.test(token)) { token = ''; return showLogin('That token was not accepted.'); }
  stored($('remember').checked ? 'localStorage' : 'sessionStorage', 'token', token);
  load();
});
$('forget').addEventListener('click', () => { forget(); showLogin(''); });
$('range').addEventListener('change', () => {
  const custom = $('range').value === 'custom';
  $('custom').hidden = !custom;
  if (custom && !$('from').value) {
    const iso = d => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
    const d = new Date();
    $('to').value = iso(d);
    d.setDate(d.getDate() - 6);
    $('from').value = iso(d);
  }
  load();
});
$('from').addEventListener('change', load);
$('to').addEventListener('change', load);
$('setpoint').addEventListener('change', draw);
$('tableBox').addEventListener('toggle', drawTable);
let resizing = 0;
let lastWidth = 0;
addEventListener('resize', () => {
  clearTimeout(resizing);
  resizing = setTimeout(() => {
    if (innerWidth !== lastWidth) { lastWidth = innerWidth; draw(); }
  }, 150);
});
// New records appear by themselves, but not while a value is being read or a date picked.
setInterval(() => {
  if (token && !document.hidden && $('tip').hidden && $('range').value !== 'custom') load();
}, 60000);
document.addEventListener('visibilitychange', () => { if (!document.hidden && token && $('tip').hidden) load(); });
load();

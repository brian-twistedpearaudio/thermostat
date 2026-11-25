'use strict';
// Thermostat controls. Loaded before app.js, which calls startControls().
const FIELD_NAMES = { set: 'set point', mode: 'mode', swing: 'swing', offset: 'sensor offset', name: 'name', schedule: 'schedule' };
const SAVED_FIELDS = ['swing', 'offset', 'name', 'schedule'];  // sent by Save
const FRESH_SECONDS = 40;
const SET_MIN = 45, SET_MAX = 85, MAX_EVENTS = 6;

let links = new Map();  // device id -> what is known and what is to be sent
let panel = null;       // the open controls: { id, dirty, schedule }

function link(id) {
  if (!links.has(id)) {
    links.set(id, { id, data: null, received: 0, sent: 0, seen: 0, want: {}, sending: null, timer: 0,
      pollError: '', putError: '' });
  }
  return links.get(id);
}

const minutes = s => s < 7200 ? `${Math.max(1, Math.round(s / 60))} min`
  : s < 172800 ? `${Math.round(s / 3600)} h` : `${Math.round(s / 86400)} days`;
const setText = (e, text) => { if (e.textContent !== text) e.textContent = text; };
const stateAge = c => c.data?.state ? c.data.state.age + (Date.now() - c.received) / 1000 : Infinity;
const isFresh = c => stateAge(c) <= FRESH_SECONDS;

// Empty if the thermostat is answering, or nothing is known yet.
function silence(c) {
  if (!c.data || isFresh(c)) return '';
  if (c.pollError) return c.pollError;
  if (!c.data.fields) return 'Not connected for control yet.';
  let s = stateAge(c);
  if (s === Infinity) {  // the server was restarted: use the time it last heard anything
    s = Date.now() / 1000 - (devices.find(d => d.id === c.id)?.lastSeen ?? NaN);
  }
  return s >= 0 ? `Not answering for ${minutes(s)}.` : 'Not answering.';
}

// What the user chose, sent or not, else what the server holds.
function shown(c, key) {
  if (c.want[key] !== undefined) return c.want[key];
  if (c.sending?.[key] !== undefined) return c.sending[key];
  const f = c.data.fields[key];
  if (key === 'set' && f.applied && c.data.state) {  // the schedule may have changed it since
    return String(Math.min(SET_MAX, Math.max(SET_MIN, Math.round(c.data.state.setpointF))));
  }
  return f.value;
}

async function callConfig(c, body) {
  const r = await fetch(`/api/v1/devices/${encodeURIComponent(c.id)}/config`, {
    method: body ? 'PUT' : 'GET', body: body && JSON.stringify(body), cache: 'no-store', keepalive: !!body,
    headers: { 'X-Api-Token': token, ...body && { 'Content-Type': 'application/json' } },
    signal: AbortSignal.timeout(10000),
  });
  if (r.ok) return r.json();
  if (r.status === 401) throw new Error('token');
  if (r.status === 404) throw new Error('The server does not know this thermostat.');
  if (r.status === 409) throw new Error('This thermostat has not connected for control yet.');
  if (r.status === 400) {
    const field = FIELD_NAMES[(await r.json().catch(() => null))?.error];
    throw new Error(field ? `The server did not accept the ${field}. Nothing was changed.` : 'The server did not accept the change.');
  }
  throw new Error(`The server answered ${r.status}.`);
}

const noAnswer = e => e instanceof TypeError || e.name === 'TimeoutError' || e.name === 'AbortError';
const inWords = e => noAnswer(e) ? 'The server is not answering.' : e.message;
const isCurrent = c => links.get(c.id) === c;  // false after Forget token

// Answers are taken in the order of their requests.
function take(c, order, data) {
  if (order < c.seen) return;
  c.seen = order;
  c.data = data;
  c.received = Date.now();
  c.pollError = '';
}

async function poll(id) {
  const c = link(id);
  if (c.sending) return;  // its answer will be newer
  const order = ++c.sent;
  try {
    const data = await callConfig(c);
    if (!isCurrent(c)) return;
    take(c, order, data);
  } catch (e) {
    if (!isCurrent(c)) return;
    if (e.message === 'token') return report(e);
    if (order >= c.seen) c.pollError = inWords(e);
  }
  drawControls();
}

// Sends what is wanted, one request at a time.
async function send(c) {
  clearTimeout(c.timer);
  c.timer = 0;
  if (c.sending || Object.keys(c.want).length === 0) return;
  c.sending = c.want;
  c.want = {};
  c.putError = '';
  const order = c.seen = ++c.sent;  // answers to earlier polls are now too old
  drawControls();
  let failed = false;
  try {
    const data = await callConfig(c, c.sending);
    if (!isCurrent(c)) return;
    take(c, order, data);
    if (panel?.id === c.id) {
      for (const key of SAVED_FIELDS) {  // a field edited meanwhile stays unsaved
        if (key in c.sending && readField(key).value === c.sending[key]) panel.dirty.delete(key);
      }
    }
    if ('name' in c.sending) load();
  } catch (e) {
    if (!isCurrent(c)) return;
    if (e.message === 'token') return report(e);
    failed = true;
    c.putError = inWords(e) + (noAnswer(e) ? ' The change may not have been stored.' : '');
  }
  c.sending = null;
  drawControls();
  if (failed) poll(c.id);
  if (!c.timer) send(c);  // what was chosen meanwhile, unless presses are still being combined
}

function pollAll() { for (const d of devices) poll(d.id); }

// Called by forget(): nothing of the old token is kept or shown.
function closeControls() {
  for (const c of links.values()) clearTimeout(c.timer);
  links = new Map();
  panel = null;
  $('controls').close();
}

// ----- The list on the left -----

function deviceState(d) {
  const row = make('div', 'state');
  row.dataset.state = d.id;
  const button = make('button', '', 'Controls');
  button.type = 'button';
  button.dataset.controls = d.id;
  button.setAttribute('aria-label', `Controls for ${nameOf(d)}`);
  button.setAttribute('aria-haspopup', 'dialog');
  button.addEventListener('click', () => openControls(d.id));
  const values = make('span');
  values.append(make('span'), ' ', make('b', 'mark', 'heating'));
  row.append(values, button);
  return row;
}

function drawStates() {
  for (const row of $('devices').querySelectorAll('[data-state]')) {
    const c = link(row.dataset.state);
    const [text, mark] = row.firstChild.children;
    const s = isFresh(c) ? c.data.state : null;
    const off = c.data?.fields?.mode.value === 'off' && c.data.fields.mode.applied;
    setText(text, s ? `${s.tempF === null ? 'Sensor fault' : s.tempF.toFixed(1) + ' °F'}, set ${number(s.setpointF, 0)} °F${off ? ', off' : ''}`
      : c.data ? silence(c) : c.pollError);
    mark.hidden = !s?.heating;
  }
}

function drawControls() {
  drawStates();
  drawPanel();
}

// ----- The panel -----

function openControls(id) {
  hideTip();
  panel = { id, dirty: new Set(), schedule: null };
  for (const key of SAVED_FIELDS) if (key !== 'schedule') $('c-' + key).value = '';
  $('c-schedule').replaceChildren();
  link(id).putError = '';
  $('c-problem').textContent = '';
  drawPanel();
  $('controls').showModal();
  $('controls').scrollTop = 0;
  poll(id);
}

function drawPanel() {
  if (!panel) return;
  const c = link(panel.id);
  const d = devices.find(x => x.id === panel.id);
  const fields = c.data?.fields;
  setText($('c-title'), d ? nameOf(d) : 'Thermostat');
  setText($('c-error'), c.putError || c.pollError);
  $('c-body').hidden = !fields;
  const s = isFresh(c) ? c.data.state : null;
  const heating = s?.heating ? ', heating' : '';
  setText($('c-now'), !c.data ? (c.pollError ? '' : 'Loading…')
    : !fields ? 'This thermostat has not connected for control yet. Its software may need an update.'
    : s ? `Now ${s.tempF === null ? 'unknown (sensor fault)' : `${s.tempF.toFixed(1)} °F`}${heating}`
    : c.pollError ? '' : silence(c));
  if (!fields) return;

  setText($('c-set'), shown(c, 'set'));
  const mode = shown(c, 'mode');
  for (const b of document.querySelectorAll('[name=c-mode]')) b.checked = b.value === mode;
  for (const key of SAVED_FIELDS) if (!panel.dirty.has(key)) fillField(key, fields[key].value);
  for (const key of Object.keys(FIELD_NAMES)) {
    setText($(`c-${key}-status`), key in c.want || c.sending && key in c.sending ? 'Sending…'
      : panel.dirty.has(key) ? 'Not saved'
      : !fields[key].applied ? 'Waiting for the thermostat' : '');
  }
  $('c-lasts').hidden = fields.schedule.value === '-';
  setText($('c-lasts'), s?.manual ? 'This change lasts until the next schedule event.'
    : 'A change lasts until the next schedule event.');
}

// Only fields the user is not in or has not edited.
function fillField(key, value) {
  const e = $('c-' + key);
  if (e.contains(document.activeElement)) return;
  if (key === 'schedule') {
    if (panel.schedule === value) return;
    panel.schedule = value;
    e.replaceChildren();
    if (value !== '-') for (const x of value.split(',')) addEvent(`${x.slice(0, 2)}:${x.slice(2, 4)}`, x.slice(5));
    nameEvents();
  } else {
    e.value = key === 'swing' ? value : key === 'offset' ? (value / 10).toFixed(1) : value;
  }
}

function edited(key) {
  panel.dirty.add(key);
  if (key === 'schedule') panel.schedule = null;
  $('c-problem').textContent = '';
  drawPanel();
}

function addEvent(time, set) {
  const row = make('div', 'event');
  const t = make('input');
  t.type = 'time';
  t.value = time;
  const f = make('input');
  f.type = 'number';
  Object.assign(f, { min: SET_MIN, max: SET_MAX, step: 1, value: set });
  const remove = make('button', '', 'Remove');
  remove.type = 'button';
  remove.addEventListener('click', () => {
    const next = row.nextElementSibling ?? row.previousElementSibling;
    row.remove();
    nameEvents();
    (next?.querySelector('button') ?? $('c-add')).focus();
    edited('schedule');
  });
  row.append(t, f, make('span', '', '°F'), remove);
  $('c-schedule').append(row);
  return t;
}

function nameEvents() {
  const rows = [...$('c-schedule').children];
  rows.forEach((row, i) => {
    const [t, f, , remove] = row.children;
    t.setAttribute('aria-label', `Event ${i + 1} time`);
    f.setAttribute('aria-label', `Event ${i + 1} set point, °F`);
    remove.setAttribute('aria-label', `Remove event ${i + 1}`);
  });
  $('c-add').disabled = rows.length >= MAX_EVENTS;
  $('c-none').hidden = rows.length > 0;
}

// The field in the form the server takes: { value }, or { problem, at } in words.
function readField(key) {
  const e = $('c-' + key);
  if (key === 'swing') return { value: e.value };
  if (key === 'name') {
    const value = e.value.trim();
    return /^[\x20-\x7e]{0,16}$/.test(value) ? { value }
      : { problem: 'The name can have up to 16 plain letters, digits and signs.', at: e };
  }
  if (key === 'offset') {
    const tenths = Number(e.value) * 10;
    const value = Math.round(tenths);
    return e.value !== '' && Math.abs(value) <= 100 && Math.abs(tenths - value) < 1e-6 ? { value: String(value) }
      : { problem: 'The sensor offset must be from -10 to 10 °F, in tenths.', at: e };
  }
  const events = new Map();
  for (const row of e.children) {
    const [t, f] = row.children;
    if (!/^\d\d:\d\d/.test(t.value)) return { problem: 'Each schedule event needs a time.', at: t };
    const time = t.value.slice(0, 2) + t.value.slice(3, 5);
    const set = Number(f.value);
    if (f.value === '' || !Number.isInteger(set) || set < SET_MIN || set > SET_MAX) {
      return { problem: `Each schedule set point must be a whole number from ${SET_MIN} to ${SET_MAX} °F.`, at: f };
    }
    if (events.has(time)) return { problem: 'Two schedule events have the same time.', at: t };
    events.set(time, set);
  }
  return { value: [...events].sort((a, b) => a[0] < b[0] ? -1 : 1).map(([t, f]) => `${t}=${f}`).join(',') || '-' };
}

function save() {
  const c = link(panel.id);
  const changes = {};
  for (const key of panel.dirty) {
    const { value, problem, at } = readField(key);
    if (problem) {
      $('c-problem').textContent = problem;
      at.focus();
      return;
    }
    changes[key] = value;
  }
  $('c-problem').textContent = panel.dirty.size === 0 ? 'Nothing to save.' : '';
  Object.assign(c.want, changes);
  send(c);
}

function press(step) {
  const c = link(panel.id);
  if (!c.data?.fields) return;
  c.want.set = String(Math.min(SET_MAX, Math.max(SET_MIN, Number(shown(c, 'set')) + step)));
  clearTimeout(c.timer);
  c.timer = setTimeout(() => send(c), 700);  // quick presses become one change
  drawPanel();
}

function startControls() {
  for (let v = 5; v <= 30; v += 5) $('c-swing').append(new Option(`${(v / 10).toFixed(1)} °F`, v));
  $('c-close').addEventListener('click', () => $('controls').close());
  $('controls').addEventListener('close', () => {
    if ($('controls').open) return;  // opened again before this event came
    const id = panel?.id;
    panel = null;
    if (id && links.has(id)) send(links.get(id));  // a last press does not wait
    if (id) $('devices').querySelector(`[data-controls="${CSS.escape(id)}"]`)?.focus();
  });
  $('c-down').addEventListener('click', () => press(-1));
  $('c-up').addEventListener('click', () => press(1));
  for (const b of document.querySelectorAll('[name=c-mode]')) {
    b.addEventListener('change', () => {
      const c = link(panel.id);
      c.want.mode = b.value;
      send(c);
      drawPanel();
    });
  }
  for (const key of SAVED_FIELDS) $('c-' + key).addEventListener('input', () => edited(key));
  $('c-add').addEventListener('click', () => {
    const time = addEvent('', shown(link(panel.id), 'set'));
    nameEvents();
    edited('schedule');
    time.focus();
  });
  $('c-form').addEventListener('submit', e => { e.preventDefault(); save(); });
  let ticks = 0;
  setInterval(() => {
    if (!token || document.hidden) return;
    if (++ticks % 3 === 0) pollAll();
    else if (panel) poll(panel.id);
    drawControls();  // the ages go on without an answer
  }, 5000);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) for (const c of links.values()) send(c);
    else if (token) pollAll();
  });
}

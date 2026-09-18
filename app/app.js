// SPDX-License-Identifier: GPL-3.0-or-later
// The phone client: pair, inbox, thread, composer.
//
// Agent output and pane titles are attacker-controlled — a program can print anything and a
// terminal title comes from OSC sequences — so every string from the wire goes in through
// textContent. There is no innerHTML in this file, and the CSP forbids inline script anyway.

import { Rrp, loadDevice, forgetDevice, fingerprint, b64, un64, storedValue, storeValue,
  dropValue } from './rrp.js';
import { ScreenView, KEYS, controlByte, keyEventBytes } from './screen.js';

const rrp = new Rrp();
let panes = [];
let current = null;
let reconnectTimer = null;
let capability = 'view';
let features = [];
let screenView = null;
let historySeq = 0;
let historyRequest = null;
let tab = 'agent';
let driving = false;
let sticky = { ctrl: false, alt: false };
let directKeys = false;
let passwordEntry = false;  // the owner's per-device switch (welcome.password_entry)
let answerNode = null;      // the answer being streamed, without a terminal to print into
let recorder = null;        // the MediaRecorder while a voice clip is being recorded
let voiceRequest = '';      // the id of the clip waiting for the desktop's transcript
const models = new Map();   // pane -> {model, waiting}: the model indicator (issue 3ES1)

const $ = (id) => document.getElementById(id);
const show = (name) => {
  for (const screen of document.querySelectorAll('.screen')) {
    screen.hidden = screen.id !== `screen-${name}`;
  }
};

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function setStatus(text, kind = '') {
  const chip = $('link-status');
  chip.textContent = text;
  chip.className = `chip ${kind}`;
}

function deviceName() {
  const agent = navigator.userAgent;
  const platform = /Android/i.test(agent) ? 'Android'
    : /iPhone|iPad/i.test(agent) ? 'iOS'
    : /Mac/i.test(agent) ? 'macOS'
    : /Windows/i.test(agent) ? 'Windows' : 'Linux';
  const browser = /Firefox/i.test(agent) ? 'Firefox'
    : /Edg/i.test(agent) ? 'Edge'
    : /Chrome/i.test(agent) ? 'Chrome'
    : /Safari/i.test(agent) ? 'Safari' : 'browser';
  return { name: `${platform} ${browser}`, platform: browser };
}

// ---- pairing ----------------------------------------------------------------------------------

async function startPairing(link) {
  show('pair');
  $('pair-state').textContent = 'Connecting to your desktop…';
  $('pair-code').textContent = '·····';
  const desktopFingerprint = await fingerprint(link.desktopPublic);
  $('pair-fingerprint').textContent = desktopFingerprint;

  const { name, platform } = deviceName();
  $('pair-device').textContent = name;
  try {
    const record = await rrp.pair(link, { name, platform });
    history.replaceState(null, '', location.pathname);
    $('pair-state').textContent = 'Paired.';
    await afterConnect(record);
  } catch (error) {
    $('pair-state').textContent = error.message || 'Pairing failed.';
    $('pair-retry').hidden = false;
  }
}

// ---- connecting -------------------------------------------------------------------------------

async function afterConnect(record) {
  $('desktop-name').textContent = record.desktopName || 'desktop';
  $('capability').textContent = record.capability;
  capability = record.capability;
  if (!rrp.session) await rrp.connect(record);
  setStatus('connected', 'ok');
  show('inbox');
  updateNotifyRow().catch(() => {});
}

async function connectStored() {
  const record = await loadDevice();
  if (!record) {
    show('welcome');
    return;
  }
  setStatus('connecting…');
  try {
    await rrp.connect(record);
    setStatus('connected', 'ok');
    await afterConnect(record);
  } catch (error) {
    setStatus('offline', 'warn');
    show('inbox');
    $('inbox-empty').textContent = error.message || 'Could not reach your desktop.';
    scheduleReconnect();
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(async () => {
    reconnectTimer = null;
    const record = await loadDevice();
    if (!record || rrp.session) return;
    try {
      await rrp.connect(record);
      setStatus('connected', 'ok');
      rrp.resume();
      if (current) rrp.send({ t: 'pane_focus', pane: current });
    } catch {
      scheduleReconnect();
    }
  }, 3000);
}

// ---- inbox ------------------------------------------------------------------------------------

const STATUS_LABEL = {
  idle: 'Idle', running: 'Running', waiting_input: 'Waiting for input',
  password: 'Password prompt', finished: 'Finished', failed: 'Failed',
};

function needsYou(pane) {
  return ['waiting_input', 'password', 'failed'].includes(pane.status);
}

function renderInbox() {
  const list = $('pane-list');
  list.replaceChildren();
  const sorted = [...panes].sort((a, b) => (needsYou(b) - needsYou(a))
    || (b.updated || 0) - (a.updated || 0));
  for (const pane of sorted) {
    const row = el('button', 'pane-row');
    row.type = 'button';
    const head = el('div', 'pane-head');
    head.append(el('span', 'pane-title', pane.title || pane.id));
    const chip = el('span', `chip status-${pane.status}`, STATUS_LABEL[pane.status] || pane.status);
    head.append(chip);
    row.append(head);
    row.append(el('div', 'pane-cwd', pane.cwd || ''));
    if (pane.queue) row.append(el('div', 'pane-queue', `${pane.queue} queued`));
    row.addEventListener('click', () => openPane(pane.id));
    list.append(row);
  }
  $('inbox-empty').textContent = panes.length ? '' : 'No panes yet.';
}

// ---- notifications ------------------------------------------------------------------------------
// Web Push (docs/REMOTE-PROTOCOL.md section 9). The subscription and the seal key go to the
// **desktop**, inside the Noise session; the rendezvous only ever posts bytes it cannot read, and
// the service worker discards any push it cannot open with the seal key. So the worst a hostile
// rendezvous can do is deliver nothing — never invent a "password prompt".
//
// Permission is asked for from this button and never on load: a browser refuses it outside a
// gesture, and a permission sheet on first open is the thing that gets an app blocked for good.

const IOS = /iPad|iPhone|iPod/.test(navigator.userAgent)
  || (/Mac/.test(navigator.userAgent) && navigator.maxTouchPoints > 1);

function installedApp() {
  return window.matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
}

function pushUsable() {
  return 'serviceWorker' in navigator && 'PushManager' in window && 'Notification' in window;
}

async function currentSubscription() {
  if (!pushUsable()) return null;
  const registration = await navigator.serviceWorker.getRegistration();
  return registration ? registration.pushManager.getSubscription() : null;
}

// Which notifications this phone wants. The desktop cannot know — it depends on whose phone this
// is and what today looks like — so the choice is made here, kept on this device's record on the
// desktop, and changed by sending `push_subscribe` again, which never re-prompts for permission.
const NOTIFY_KINDS = [
  ['waiting_input', 'Waiting for input'],
  ['password', 'Password prompt'],
  ['failed', 'A turn failed'],
  ['plan', 'A plan is ready'],
  ['agent_finished', 'The agent finished (after 30 seconds)'],
];
let notifyKinds = null;                    // what the desktop last told us it has stored

function renderNotifyKinds(on) {
  const list = $('notify-kinds');
  list.replaceChildren();
  list.hidden = !on;
  if (!on) return;
  const wanted = notifyKinds || NOTIFY_KINDS.map(([kind]) => kind);
  for (const [kind, label] of NOTIFY_KINDS) {
    const row = el('div', 'notify-kind');       // a div, so each sits on its own line
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.id = `notify-kind-${kind}`;
    box.checked = wanted.includes(kind);
    box.addEventListener('change', chooseNotifyKinds);
    const text = el('label', 'muted small', ` ${label}`);
    text.htmlFor = box.id;
    row.append(box, text);
    list.append(row);
  }
}

async function chooseNotifyKinds() {
  const chosen = NOTIFY_KINDS.map(([kind]) => kind)
    .filter((kind) => $(`notify-kind-${kind}`)?.checked);
  if (!chosen.length) {
    // Wanting none of them is unsubscribing, and says so rather than silently keeping the last set.
    await toggleNotifications();
    return;
  }
  try {
    await sendSubscription(await currentSubscription(), chosen);
  } catch (error) {
    $('notify-note').textContent = error.message || 'That did not work.';
  }
  renderNotifyKinds(true);
}

// One `push_subscribe`, from whatever this device currently holds. Used both to subscribe and to
// change the list afterwards, so there is one place that decides what the desktop is told.
async function sendSubscription(subscription, kinds) {
  const key = await storedValue('push-key');
  if (!subscription || !key) throw new Error('this phone is not subscribed.');
  await rrp.send({
    t: 'push_subscribe',
    endpoint: subscription.endpoint,
    p256dh: b64(new Uint8Array(subscription.getKey('p256dh'))),
    auth: b64(new Uint8Array(subscription.getKey('auth'))),
    key: b64(new Uint8Array(await crypto.subtle.exportKey('raw', key))),
    kinds,
  });
  const state = await rrp.once('push_state', 15000);
  notifyKinds = state.kinds || kinds;
  await storeValue('push-kinds', notifyKinds);     // so a reload draws the boxes before it asks
  return state;
}

async function updateNotifyRow() {
  const button = $('notify');
  const note = $('notify-note');
  if (!button) return;
  if (!pushUsable()) {
    button.hidden = true;
    // iOS delivers Web Push only to a PWA on the Home Screen, and simply has no PushManager
    // otherwise — no error, no prompt — so say what to do instead of failing silently.
    note.textContent = IOS && !installedApp()
      ? 'To be notified on iPhone or iPad: tap the Share button, choose “Add to Home Screen”, '
        + 'and open Relay from there. iOS only delivers notifications to an installed app.'
      : 'This browser cannot show notifications.';
    return;
  }
  button.hidden = false;
  const on = Boolean(await currentSubscription());
  button.textContent = on ? 'Stop notifying this phone' : 'Notify me on this phone';
  if (on && !notifyKinds) notifyKinds = await storedValue('push-kinds');
  renderNotifyKinds(on);
  if (on) {
    note.textContent = 'Your desktop decides what is worth telling you, and a notification never '
      + 'carries what is on the screen.';
  } else if (Notification.permission === 'denied') {
    note.textContent = 'Notifications are blocked for this site in your browser’s settings.';
  } else {
    note.textContent = '';
  }
}

async function enableNotifications() {
  const note = $('notify-note');
  note.textContent = '';
  // First thing in the gesture: Safari drops the user activation across an await.
  const permission = await Notification.requestPermission();
  if (permission !== 'granted') {
    note.textContent = 'Notifications are off for this site.';
    return;
  }
  const registration = await navigator.serviceWorker.ready;
  const reply = await fetch(`${rrp.origin}/v1/push/key`);
  const { vapid } = await reply.json();
  const subscription = await registration.pushManager.subscribe({
    userVisibleOnly: true, applicationServerKey: un64(vapid),
  });
  // The seal key: generated here, kept where the service worker reads it, and sent to the
  // desktop and nowhere else.
  const key = await crypto.subtle.generateKey({ name: 'AES-GCM', length: 256 }, true,
    ['encrypt', 'decrypt']);
  await storeValue('push-key', key);
  // Everything on to begin with: one tap turns off what you did not want, and a notification you
  // never saw is not something you can decide about.
  await sendSubscription(subscription, (await storedValue('push-kinds'))
    || NOTIFY_KINDS.map(([kind]) => kind));
}

async function disableNotifications() {
  const subscription = await currentSubscription();
  if (subscription) await subscription.unsubscribe();
  await dropValue('push-key');
  await dropValue('push-kinds');
  notifyKinds = null;
  await rrp.send({ t: 'push_unsubscribe' });
  await rrp.once('push_state', 15000);
}

async function toggleNotifications() {
  const button = $('notify');
  button.disabled = true;
  try {
    if (await currentSubscription()) await disableNotifications();
    else await enableNotifications();
  } catch (error) {
    $('notify-note').textContent = error.message || 'That did not work.';
  } finally {
    button.disabled = false;
    await updateNotifyRow();
  }
}

// ---- thread -----------------------------------------------------------------------------------

function openPane(paneId) {
  current = paneId;
  const pane = panes.find((item) => item.id === paneId);
  $('thread-title').textContent = pane?.title || paneId;
  $('thread-cwd').textContent = pane?.cwd || '';
  renderModel();
  $('thread-body').replaceChildren();
  answerNode = null;
  driving = false;
  directKeys = false;
  openTerminal();
  show('thread');
  rrp.send({ t: 'pane_focus', pane: paneId }).catch(() => {});
}

function closePane() {
  if (current) {
    if (driving) rrp.send({ t: 'control_release', pane: current }).catch(() => {});
    rrp.send({ t: 'pane_blur', pane: current }).catch(() => {});
  }
  current = null;
  driving = false;
  show('inbox');
}

// ---- terminal -----------------------------------------------------------------------------

function openTerminal() {
  const hasScreen = features.includes('screen');
  $('terminal-pane').hidden = !hasScreen;
  $('thread-body').hidden = hasScreen;
  if (!hasScreen) return;
  ensureScreen();
  // Another pane's scrollback is not this one's, and the rows are numbered per pane.
  screenView.resetHistory();
  screenView.fit();
  updateDriveUi();
  rrp.send({ t: 'screen_get', pane: current }).catch(() => {});
}

// The view, with the two things only this file can give it: how to ask for a page of scrollback,
// and where the "new output" affordance lives. A desktop that does not offer `history` simply
// leaves the first one unset, and the view never asks.
function ensureScreen() {
  if (screenView) return screenView;
  screenView = new ScreenView($('screen-wrap'));
  screenView.onBehind = (behind) => { $('term-new-output').hidden = !behind; };
  if (features.includes('history')) {
    screenView.onNeedHistory = (beforeRow, count) => {
      if (!current) { screenView.pending = false; return; }
      historySeq += 1;
      historyRequest = `h${historySeq}`;
      const message = { t: 'history_get', pane: current, count, id: historyRequest };
      if (beforeRow !== null) message.before_row = beforeRow;
      rrp.send(message).catch(() => { screenView.pending = false; });
    };
  }
  return screenView;
}

function onScreen(message) {
  if (message.pane !== current) return;
  ensureScreen().apply(message);
}

// A page of scrollback. Anything for another pane, or for a request we have stopped waiting on,
// is dropped rather than painted above somebody else's screen.
function onHistory(message) {
  if (!screenView || message.pane !== current) return;
  if (message.id && message.id !== historyRequest) return;
  screenView.applyHistory(message);
}

// Typing belongs at the live end. Snapping first means a key never lands somewhere the person
// cannot see it happen.
function toLive() {
  if (screenView) screenView.toLive();
}

// The masked field replaces the prompt box while the pane is at a password prompt and this
// device is trusted to answer it. The nonce comes from the pane list itself, minted by the
// desktop for exactly this prompt; sending clears the field whatever the desktop then says.
function updateSecretRow() {
  const pane = panes.find((item) => item.id === current);
  const ask = pane?.status === 'password' && pane.secret_nonce
    && capability === 'full' && passwordEntry;
  $('secret-row').hidden = !ask;
  if (ask) {
    $('composer').hidden = true;
    $('term-note').textContent = 'This pane is asking for a password.';
  } else if (capability === 'agent' || capability === 'full') {
    updateDriveUi();               // restore the composer's own visibility rules
  }
}

function sendSecret() {
  const box = $('secret-text');
  const text = box.value;
  const pane = panes.find((item) => item.id === current);
  if (!text || !current || !pane?.secret_nonce) return;
  rrp.send({ t: 'secret_input', pane: current, nonce: pane.secret_nonce,
             bytes: b64(new TextEncoder().encode(text)) })
    .then(() => { box.value = ''; })
    .catch((error) => { $('term-note').textContent = error.message; });
}

function updateDriveUi() {
  const allowed = capability === 'full';
  $('term-take').hidden = driving || !allowed;
  $('term-release').hidden = !driving;
  $('term-direct').hidden = !driving;
  $('term-keys').hidden = !driving;
  $('term-capture').hidden = !driving || !directKeys;
  $('term-direct').classList.toggle('is-on', directKeys);
  $('term-mode').textContent = driving
    ? (directKeys ? 'Typing directly' : 'You have the keyboard')
    : 'Watching';
  $('term-mode').className = `chip ${driving ? 'ok' : ''}`;
  // A `view` device may not compose at all, so it gets no box rather than one that only ever
  // reports "not permitted". With direct typing on, the keys go straight through instead.
  const canCompose = capability === 'agent' || capability === 'full';
  $('composer').hidden = !canCompose || (driving && directKeys);
  updateVoiceUi();
  $('composer-text').placeholder = driving ? 'Type a line for the program…' : 'Ask or run…';
  if (!allowed) {
    $('term-note').textContent = 'This device is paired for viewing only.';
  } else if (!driving) {
    $('term-note').textContent = 'Read only until you take over.';
  } else {
    $('term-note').textContent = '';
  }
}

// Direct typing: every key goes straight to the program, so a tablet with a keyboard behaves
// like a terminal. The prompt box stays the default on a phone, where soft keyboards mangle
// per-key input; this is the opt-in for when there are real keys.
function setDirectKeys(on) {
  directKeys = on;
  updateDriveUi();
  if (on) {
    const capture = $('term-capture');
    capture.value = '';
    capture.focus();
  }
}

function onDirectKey(event) {
  if (!driving || !directKeys) return;
  const bytes = keyEventBytes(event);
  if (bytes === null) return;
  event.preventDefault();
  sendKeys(bytes);
}

function sendKeys(text) {
  if (!driving || !current) return;
  toLive();
  const bytes = new TextEncoder().encode(text);
  rrp.send({ t: 'keys', pane: current, bytes: b64(bytes) })
    .catch((error) => { $('term-note').textContent = error.message; });
}

function buildKeyRow() {
  const row = $('term-keys');
  row.replaceChildren();
  const add = (label, action, name) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = label;
    if (name) button.dataset.sticky = name;
    button.addEventListener('click', action);
    row.append(button);
    return button;
  };
  add('Esc', () => sendKeys(KEYS.Esc));
  add('Tab', () => sendKeys(KEYS.Tab));
  add('Ctrl', () => toggleSticky('ctrl'), 'ctrl');
  add('Alt', () => toggleSticky('alt'), 'alt');
  for (const [label, key] of [['←', 'Left'], ['↓', 'Down'], ['↑', 'Up'], ['→', 'Right']]) {
    add(label, () => sendKeys(KEYS[key]));
  }
  for (const key of ['^C', '^D', '^Z', '^L', '^R', '^U', '^W']) {
    add(key, () => sendKeys(KEYS[key]));
  }
  add('Home', () => sendKeys(KEYS.Home));
  add('End', () => sendKeys(KEYS.End));
  add('PgUp', () => sendKeys(KEYS.PgUp));
  add('PgDn', () => sendKeys(KEYS.PgDn));
  add('Enter', () => sendKeys(KEYS.Enter));
}

function toggleSticky(name) {
  sticky[name] = !sticky[name];
  for (const button of $('term-keys').querySelectorAll('[data-sticky]')) {
    button.classList.toggle('sticky-on', !!sticky[button.dataset.sticky]);
  }
}

// ---- agent state ------------------------------------------------------------------------------

// The agent's *output* arrives through the screen stream, because Relay prints it into the pane's
// terminal (ARCHITECTURE section 8). So there is no transcript to render here — only the state a
// prompt box needs: whether a turn is running, and anything it wants to say.
function onAgent(message) {
  const event = message.event || {};
  // A transcript answers one clip by its id, so it is taken before the open-pane check: it must
  // land in the prompt box even if the inbox was opened while the desktop was transcribing.
  if (voiceRequest && message.id === voiceRequest && event.event === 'transcribed') {
    voiceDone(event.text || '');
    return;
  }
  // Every pane's model, not just the open one's, so the indicator is right when it is opened.
  trackModel(message.pane, event);
  if (message.pane !== current) return;
  const note = (text) => { $('term-note').textContent = text; };
  // A desktop with no screen stream — the agent companion — has no terminal to print into, so
  // the reply is rendered here instead. With a terminal, this is silent: the same text is
  // already arriving as screen state.
  if (!features.includes('screen')) transcribe(event);
  switch (event.event) {
    case 'agent_started':
      $('composer-stop').hidden = false;
      note('Agent running…');
      break;
    case 'queued':
      note(event.origin ? `Queued · ${event.origin}` : 'Queued');
      break;
    case 'agent_finished':
    case 'agent_stopped':
    case 'cancelled':
      $('composer-stop').hidden = true;
      note(event.event === 'agent_finished' ? '' : 'Stopped.');
      break;
    case 'plan_written':
      note('A plan is ready on the desktop.');
      break;
    case 'model_changed':
    case 'model_applied':
    case 'model_switch_refused':
      renderModel();
      note(modelLine(event));
      break;
    case 'status':
      if (event.text) note(event.text);
      break;
    case 'error':
      $('composer-stop').hidden = true;
      note(event.message || 'The agent reported an error.');
      break;
    default:
      break;
  }
}

// ---- the model a pane runs (issue 3ES1) ---------------------------------------------------------

// A switch can be accepted while the agent works: the request in flight finishes on the old model
// and the new one takes over at the next step (or after a compaction a smaller window needs). The
// lines are the desktop's own, so a phone reads the same story as the pane.
function modelLine(event) {
  const model = event.model || '';
  if (event.event === 'model_changed') {
    const was = event.in_flight_model || '';
    if (event.applies === 'after_compaction') {
      return `↻ ${model} takes over once the conversation is compacted to fit its window · ${was} summarises it`;
    }
    if (event.applies === 'next_step' || event.applies === 'turn_end') {
      const when = event.applies === 'turn_end' ? 'after this turn' : 'at the next step';
      return `↻ ${model} takes over ${when} · ${was} is not interrupted${event.will_compact ? ' · will compact to fit' : ''}`;
    }
    return `Model: ${model} · conversation kept`;
  }
  if (event.event === 'model_applied') {
    let line = `→ now on ${model}`;
    if (event.at === 'turn_end') line += ' · from the next turn';
    if (event.history_converted) line += ` · conversation converted from ${event.from_model || ''}`;
    if (event.compacted) line += ' · compacted to fit its window';
    return line;
  }
  // The reason is a whole sentence that names both models.
  return `✗ ${event.reason || `${model} did not take over.`}`;
}

function trackModel(pane, event) {
  switch (event.event) {
    case 'model_changed':
      models.set(pane, { model: event.model || '',
                         waiting: event.applies && event.applies !== 'now' ? event.in_flight_model || '' : '' });
      break;
    case 'model_applied':
      models.set(pane, { model: event.model || '', waiting: '' });
      break;
    case 'model_switch_refused':
      models.set(pane, { model: event.current_model || '', waiting: '' });
      break;
    default:
      break;
  }
}

// The indicator under the pane's title: the model the pane is on, and while a switch waits, the
// model still finishing the request in flight.
function renderModel() {
  const node = $('thread-model');
  const info = models.get(current);
  node.hidden = !info?.model;
  node.classList.toggle('waiting', !!info?.waiting);
  node.textContent = !info?.model ? '' : info.waiting ? `${info.model} · ${info.waiting} finishing the current step`
                                                      : info.model;
}

// ---- tool-call lines (docs/AGENT-SESSIONS-PROTOCOL.md § 23) -----------------------------------
// One concise line per call — "ran pytest · 212 lines · exit 1 · 8 s" — rewritten in place when
// the call lands, with the full detail behind the disclosure triangle. `preview` is legacy: it is
// read only when the worker sent no `label` at all, and is otherwise just what the fold shows.

// The headings an old preview led with, and the kind and the two tenses they stand for.
const LEGACY_PREVIEWS = {
  'RUN COMMAND': ['run', 'ran', 'running'],
  'READ FILE': ['read', 'read', 'reading'],
  'LIST DIRECTORY': ['list', 'listed', 'listing'],
  'WRITE FILE': ['edit', 'wrote', 'writing'],
  'EDIT FILE': ['edit', 'edited', 'editing'],
};
const PREVIEW_NOTE = /^(Working directory|Timeout|Old bytes|New bytes): /;

const shortPath = (path) => (path.length <= 40 ? path : path.slice(path.lastIndexOf('/') + 1));
const thousands = (value) => String(value).replace(/\B(?=(\d{3})+(?!\d))/g, ',');

// One event's label, or one built from its legacy preview when it carries none.
function toolLabel(event) {
  const label = event.label;
  if (label && typeof label === 'object') {
    return {
      kind: label.kind || '', title: label.title || '', running: label.running || '',
      stats: Array.isArray(label.stats) ? label.stats.filter((piece) => typeof piece === 'string') : [],
      error: typeof label.error === 'string' ? label.error : '', path: label.path || '',
      failed: label.ok === false || !!label.error,
      inlineDiff: label.inline_diff === true,
      open: (label.open && label.open.type) || '',
      merge: label.merge && label.merge.key ? label.merge : null,
      fallback: false,
    };
  }
  const lines = String(event.preview || '').split('\n');
  const verbs = LEGACY_PREVIEWS[(lines[0] || '').trim()];
  let subject = '';
  let loose = '';
  for (let at = 0; at < lines.length; at += 1) {
    const line = lines[at].trim();
    if (!line || PREVIEW_NOTE.test(lines[at])) continue;
    if (!loose) loose = line;
    if (at > 0) { subject = line; break; }
  }
  let kind = 'other';
  let title = '';
  let running = '';
  let path = '';
  if (verbs) {
    [kind] = verbs;
    const what = kind === 'run' ? subject.slice(0, 40) : shortPath(subject);
    if (kind !== 'run') path = subject;
    title = what ? `${verbs[1]} ${what}` : verbs[1];
    running = what ? `${verbs[2]} ${what}` : verbs[2];
  } else {
    const name = String(event.tool || 'tool').replace(/_/g, ' ');
    title = loose && loose.length <= 60 ? `${name} ${loose}` : name;
    running = `running ${name}`;
  }
  const result = event.result && typeof event.result === 'object' ? event.result : {};
  const exit = typeof event.exit_code === 'number' ? event.exit_code : result.exit_code;
  const error = typeof result.error === 'string' ? result.error.split('\n')[0].slice(0, 120) : '';
  return {
    kind, title, running, path, error,
    stats: typeof exit === 'number' ? [`exit ${exit}`] : [],
    failed: !!error || (typeof exit === 'number' && exit !== 0),
    inlineDiff: false, open: '', merge: null, fallback: true,
  };
}

// The line itself: title, the stats, and the reason when the call never happened.
const labelLine = (label) =>
  [label.title, ...label.stats, ...(label.error ? [label.error] : [])].filter(Boolean).join(' · ');

// "read 6 files · 4,100 lines" (§ 23.7).
function mergeLine(run) {
  const verb = { read: 'read', list: 'listed' }[run.key] || run.key;
  const noun = run.count === 1 ? run.singular || run.key : run.plural || run.key;
  if (!run.unit) return `${verb} ${run.count} ${noun}`;
  const unit = run.total === 1 ? (run.unit === 'entries' ? 'entry' : 'line') : run.unit;
  return `${verb} ${run.count} ${noun} · ${thousands(run.total)} ${unit}`;
}

// The rows of the turn being transcribed, so a result rewrites the row its start drew.
let toolCalls = new Map();
let toolRun = null;      // the run of consecutive reads or listings in progress
let lastCall = null;

function resetToolCalls() {
  toolCalls = new Map();
  toolRun = null;
  lastCall = null;
}

function drawToolRow(call) {
  const text = call.done ? labelLine(call.label) : call.label.running || call.label.title;
  call.summary.textContent = call.done && call.label.failed ? `✗ ${text}` : text;
  call.box.classList.toggle('failed', !!(call.done && call.label.failed));
  call.box.classList.toggle('ok', !!(call.done && !call.label.failed));
}

function addToolDetail(call, text) {
  if (!text) return;
  call.detail.textContent += (call.detail.textContent ? '\n' : '') + text;
}

// A short diff prints under the line with no tap at all, added green and removed red (§ 23.6).
function renderDiff(box, diff) {
  box.textContent = '';
  for (const line of String(diff).split('\n')) {
    if (!line || line.startsWith('+++') || line.startsWith('---') || line.startsWith('@@')) continue;
    const kind = line.startsWith('+') ? 'add' : line.startsWith('-') ? 'del' : 'keep';
    box.append(el('div', `diff-line ${kind}`, line));
  }
}

function startToolRow(body, event) {
  const row = el('div', 'tool-row');
  const box = el('details', 'tool');
  const summary = el('summary', 'tool-line');
  const detail = el('pre', 'tool-detail', event.preview || '');
  box.append(summary, detail);
  const diffBox = el('div', 'tool-diff');
  row.append(box, diffBox);
  body.append(row);
  const call = {
    id: event.call_id || '', row, box, summary, detail, diffBox,
    label: toolLabel(event), done: false,
  };
  drawToolRow(call);
  toolCalls.set(call.id || `anon-${toolCalls.size}`, call);
  lastCall = call;
  return call;
}

function finishToolRow(body, event) {
  let call = event.call_id ? toolCalls.get(event.call_id) : null;
  if (!call && !event.call_id && lastCall && !lastCall.done) call = lastCall;
  if (!call) call = startToolRow(body, { tool: event.tool, call_id: event.call_id });
  const landed = toolLabel(event);
  if (landed.fallback && call.label.title) {
    // An old worker's result carries no preview either: keep the title its start gave us.
    call.label = { ...call.label, stats: landed.stats, failed: landed.failed, error: landed.error };
  } else {
    call.label = landed;
  }
  call.done = true;
  const result = event.result && typeof event.result === 'object' ? event.result : {};
  if (typeof result.error === 'string') addToolDetail(call, result.error);
  if (typeof event.diff === 'string' && event.diff) {
    call.diff = event.diff;
    addToolDetail(call, event.diff);
  }

  const merge = call.label.merge;
  if (merge && !call.label.failed && toolRun && toolRun.key === merge.key
      && toolRun.head !== call && call === lastCall) {
    // This call joins the run above it: its own row goes, and the run's line counts it (§ 23.7).
    toolRun.count += 1;
    toolRun.total += Number(merge.lines ?? merge.entries ?? 0);
    toolRun.unit = toolRun.unit || (merge.lines !== undefined ? 'lines' : merge.entries !== undefined ? 'entries' : '');
    addToolDetail(toolRun.head, call.detail.textContent);
    call.row.remove();
    toolCalls.delete(call.id || '');
    lastCall = toolRun.head;
    toolRun.head.label = { ...toolRun.head.label, title: mergeLine(toolRun), stats: [], error: '' };
    drawToolRow(toolRun.head);
    return;
  }
  drawToolRow(call);
  toolRun = merge && !call.label.failed
    ? { key: merge.key, head: call, count: 1, total: Number(merge.lines ?? merge.entries ?? 0),
        unit: merge.lines !== undefined ? 'lines' : merge.entries !== undefined ? 'entries' : '',
        singular: merge.singular, plural: merge.plural }
    : null;
  if (call.label.inlineDiff && call.diff) renderDiff(call.diffBox, call.diff);
}

// The fallback transcript: prompt, tool lines and the answer, for a desktop that cannot send a
// screen. Everything here is program or model output, so it goes in through textContent.
function transcribe(event) {
  const body = $('thread-body');
  const stick = body.scrollHeight - body.scrollTop - body.clientHeight < 80;
  switch (event.event) {
    case 'queued': {
      const bubble = el('div', 'prompt');
      bubble.append(el('div', 'prompt-text', event.text || ''));
      if (event.origin) bubble.append(el('div', 'prompt-origin', event.origin));
      body.append(bubble);
      resetToolCalls();
      answerNode = null;
      break;
    }
    case 'tool_started': {
      startToolRow(body, event);
      answerNode = null;
      break;
    }
    case 'tool_result': {
      finishToolRow(body, event);
      answerNode = null;
      break;
    }
    case 'tool_output': {
      // Output belongs behind the running call's line, not between the lines.
      if (lastCall && !lastCall.done) addToolDetail(lastCall, event.text || '');
      break;
    }
    case 'delta': {
      if (!answerNode) {
        answerNode = el('div', 'answer');
        body.append(answerNode);
      }
      answerNode.textContent += event.text || '';
      break;
    }
    case 'plan_written': {
      const card = el('div', 'plan');
      card.append(el('div', 'plan-title', event.title || 'Plan'));
      card.append(el('pre', 'plan-body', event.preview || ''));
      const run = el('button', 'plan-run', 'Execute');
      run.type = 'button';
      run.addEventListener('click', () => {
        run.disabled = true;
        // By id, never by path: the desktop minted it and resolves it against its own table.
        rrp.send({ t: 'plan_execute', pane: current, plan_id: event.plan_id }).catch(() => {});
      });
      card.append(run);
      body.append(card);
      answerNode = null;
      break;
    }
    case 'recap':
      body.append(el('div', 'note', event.text || ''));
      answerNode = null;
      break;
    case 'model_changed':
    case 'model_applied':
    case 'model_switch_refused':
      // With a screen these lines arrive in the terminal, printed by the desktop; here they are
      // the transcript's own, worded the same.
      body.append(el('div', event.event === 'model_switch_refused' ? 'note error model-line' : 'note model-line',
                     modelLine(event)));
      answerNode = null;
      break;
    case 'agent_finished':
    case 'agent_stopped':
    case 'cancelled':
      // A new turn never continues the last one's run of reads.
      resetToolCalls();
      answerNode = null;
      break;
    default:
      break;
  }
  if (stick) body.scrollTop = body.scrollHeight;
}

// ---- voice ------------------------------------------------------------------------------------
//
// Hold the thought, not the phone: tap the microphone, speak, tap it again. The clip is recorded
// here and sent inside the session to the desktop, which transcribes it with the key it already
// has and sends back the words. No provider is contacted from this device, no key ever reaches
// it, and nothing is kept: the recorder's chunks are dropped as soon as the clip has gone. The
// text lands in the prompt box rather than being sent, because a transcript is a draft.

const VOICE_MAX_MS = 60000;
const VOICE_MAX_BYTES = 700000;   // MAX_VOICE_BYTES in remote/host.py
const VOICE_BITS = 32000;         // 60s of speech well inside the frame the hub will accept
// Containers the hub accepts. Safari on iOS records audio/mp4, which is what it calls m4a.
const VOICE_CONTAINERS = {
  'audio/webm': 'webm', 'audio/ogg': 'ogg', 'audio/mp4': 'm4a', 'audio/aac': 'm4a',
  'audio/x-m4a': 'm4a', 'audio/mpeg': 'mp3', 'audio/wav': 'wav', 'audio/wave': 'wav',
  'audio/x-wav': 'wav',
};

function voiceSupported() {
  return typeof MediaRecorder !== 'undefined'
    && !!navigator.mediaDevices?.getUserMedia
    && !!window.isSecureContext;
}

// The container to ask for. Chrome and Firefox take webm; Safari ignores the option and records
// audio/mp4, which is why the format is read back off the recorder rather than assumed.
function voiceMimeType() {
  for (const type of ['audio/webm;codecs=opus', 'audio/webm', 'audio/ogg;codecs=opus',
                      'audio/mp4']) {
    if (MediaRecorder.isTypeSupported?.(type)) return type;
  }
  return '';
}

function voiceFormat(mimeType) {
  return VOICE_CONTAINERS[(mimeType || '').split(';')[0].trim().toLowerCase()] || '';
}

// The button is there when the desktop offers voice, this device may compose, and the browser
// can actually record. A device paired for viewing never sees it.
function updateVoiceUi() {
  const button = $('composer-mic');
  if (!button) return;
  const allowed = features.includes('voice') && (capability === 'agent' || capability === 'full');
  button.hidden = !allowed || !voiceSupported();
  button.classList.toggle('recording', !!recorder);
  button.classList.toggle('busy', !recorder && !!voiceRequest);
  button.disabled = !recorder && !!voiceRequest;
  button.title = recorder ? 'Stop recording and transcribe'
    : voiceRequest ? 'Transcribing on the desktop…' : 'Record a voice clip';
  button.setAttribute('aria-pressed', recorder ? 'true' : 'false');
}

function voiceNote(text) {
  $('term-note').textContent = text;
}

function toggleVoice() {
  if (recorder) stopVoice();
  else startVoice();
}

async function startVoice() {
  if (recorder || voiceRequest || !current) return;
  let stream;
  try {
    stream = await navigator.mediaDevices.getUserMedia({ audio: true });
  } catch (error) {
    voiceNote(error?.name === 'NotAllowedError'
      ? 'This browser refused the microphone. Allow it for this site and try again.'
      : 'No microphone is available on this device.');
    return;
  }
  const mimeType = voiceMimeType();
  try {
    recorder = new MediaRecorder(stream, {
      ...(mimeType ? { mimeType } : {}), audioBitsPerSecond: VOICE_BITS,
    });
  } catch {
    recorder = new MediaRecorder(stream);
  }
  const chunks = [];
  const done = () => {
    for (const track of stream.getTracks()) track.stop();
    recorder = null;
    updateVoiceUi();
  };
  recorder.addEventListener('dataavailable', (event) => {
    if (event.data?.size) chunks.push(event.data);
  });
  recorder.addEventListener('error', () => { done(); voiceNote('The recording failed.'); });
  recorder.addEventListener('stop', () => {
    const type = recorder?.mimeType || mimeType;
    done();
    sendClip(new Blob(chunks, { type }), type);
    chunks.length = 0;
  });
  recorder.start();
  // A clip has to fit one frame, so the recorder stops itself rather than the person finding out
  // afterwards that nothing was sent. Tied to this recorder, so a later one is not cut short.
  const started = recorder;
  setTimeout(() => { if (recorder === started) stopVoice(); }, VOICE_MAX_MS);
  updateVoiceUi();
  voiceNote('Listening… tap the microphone again to transcribe.');
}

function stopVoice() {
  if (!recorder) return;
  try {
    recorder.stop();
  } catch {
    recorder = null;
    updateVoiceUi();
  }
}

async function sendClip(blob, mimeType) {
  const format = voiceFormat(mimeType);
  if (!blob.size) { voiceNote('Nothing was recorded.'); return; }
  if (!format) { voiceNote(`This browser records ${mimeType || 'an unknown format'}, which the desktop cannot read.`); return; }
  if (blob.size > VOICE_MAX_BYTES) { voiceNote('That clip is too long to send; keep it under a minute.'); return; }
  const pane = current;
  const id = `v${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;
  voiceRequest = id;
  updateVoiceUi();
  voiceNote('Transcribing on the desktop…');
  try {
    const data = new Uint8Array(await blob.arrayBuffer());
    await rrp.send({ t: 'voice', pane, format, data: b64(data), id });
  } catch (error) {
    voiceFailed(error.message);
  }
}

// The words come back as a `transcribed` event carrying the id the clip went out with, so a
// second clip cannot be answered with the first one's text.
function voiceDone(text) {
  voiceRequest = '';
  updateVoiceUi();
  if (!text) { voiceNote('Nothing was said.'); return; }
  const box = $('composer-text');
  // Appended, never replacing: whatever was already typed is still the person's.
  const before = box.value;
  const join = !before || /\s$/.test(before) ? '' : ' ';
  box.value = before + join + text;
  box.style.height = 'auto';
  box.style.height = `${box.scrollHeight}px`;
  box.focus();
  box.setSelectionRange(box.value.length, box.value.length);
  voiceNote('Transcribed · check it, then send.');
}

function voiceFailed(message) {
  voiceRequest = '';
  updateVoiceUi();
  voiceNote(message || 'The clip could not be transcribed.');
}

// ---- the one prompt box ---------------------------------------------------------------------

// What you type is routed the way Relay's own prompt box routes it: a command runs in the shell,
// anything else goes to the agent, and the agent's answer prints into this same terminal. Once you
// have taken over, the line belongs to the running program instead.
function sendPrompt() {
  const box = $('composer-text');
  const text = box.value.trim();
  if (!text || !current) return;
  const fail = (error) => { $('term-note').textContent = error.message; };
  const clear = () => { box.value = ''; box.style.height = 'auto'; };

  if (driving) {
    toLive();
    rrp.send({ t: 'line', pane: current, text }).catch(fail);
    clear();
    return;
  }
  const running = panes.find((pane) => pane.id === current)?.status === 'running';
  rrp.send({
    t: 'compose', pane: current, text, when: running ? 'queue' : 'now',
    // `agent: false` asks the desktop to route it, which only a device trusted with typing may
    // do. Anything else reaches the agent and nothing more.
    ...(capability === 'full' ? { agent: false } : {}),
    msg_id: b64(crypto.getRandomValues(new Uint8Array(9))),
  }).catch(fail);
  clear();
}

// ---- wiring -----------------------------------------------------------------------------------

rrp.addEventListener('welcome', (event) => {
  capability = event.detail.capability || capability;
  features = event.detail.features || [];
  passwordEntry = !!event.detail.password_entry;
  $('capability').textContent = capability;
  updateVoiceUi();
});

rrp.addEventListener('screen_snapshot', (event) => onScreen(event.detail));
rrp.addEventListener('screen_diff', (event) => onScreen(event.detail));
rrp.addEventListener('history', (event) => onHistory(event.detail));

rrp.addEventListener('authcode', (event) => {
  $('pair-code').textContent = event.detail.code;
  $('pair-state').textContent = 'Check this code on your desktop, then allow the device.';
});

rrp.addEventListener('panes', (event) => {
  panes = event.detail.items || [];
  renderInbox();
  if (current) {
    const pane = panes.find((item) => item.id === current);
    if (pane) {
      $('thread-title').textContent = pane.title || pane.id;
      $('thread-cwd').textContent = pane.cwd || '';
    }
    updateSecretRow();
  }
});

rrp.addEventListener('agent', (event) => onAgent(event.detail));

rrp.addEventListener('error', (event) => {
  const detail = event.detail || {};
  if (voiceRequest && detail.id === voiceRequest) { voiceFailed(detail.message); return; }
  // A refused page must not leave the view waiting for one for ever; a later drag asks again.
  if (screenView && detail.id && detail.id === historyRequest) {
    screenView.pending = false;
    screenView.more = false;
    return;
  }
  if (current) append(el('div', 'note error', detail.message || 'Refused.'));
});

rrp.addEventListener('revoked', async () => {
  await forgetDevice();
  setStatus('revoked', 'warn');
  show('welcome');
  $('welcome-note').textContent = 'This device was revoked from the desktop.';
});

rrp.addEventListener('closed', (event) => {
  setStatus('offline', 'warn');
  $('composer-stop').hidden = true;
  if (!event.detail.clean) scheduleReconnect();
});

rrp.addEventListener('fault', (event) => {
  setStatus('dropped', 'warn');
  append(el('div', 'note error', event.detail.message));
});

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') {
    if (!rrp.session) scheduleReconnect();
    else rrp.send({ t: 'client_state', visible: true }).catch(() => {});
  } else if (rrp.session) {
    rrp.send({ t: 'client_state', visible: false }).catch(() => {});
  }
});

window.addEventListener('DOMContentLoaded', () => {
  $('composer-send').addEventListener('click', sendPrompt);
  $('composer-mic').addEventListener('click', toggleVoice);
  $('composer-text').addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      sendPrompt();
    }
  });
  $('composer-text').addEventListener('input', (event) => {
    event.target.style.height = 'auto';
    event.target.style.height = `${Math.min(event.target.scrollHeight, 160)}px`;
  });
  $('secret-send').addEventListener('click', sendSecret);
  $('secret-text').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      sendSecret();
    }
  });
  $('composer-stop').addEventListener('click', () => {
    rrp.send({ t: 'agent_stop', pane: current }).catch(() => {});
  });
  // Sticky Ctrl/Alt from the extra-keys row apply to the next line typed in the box.
  $('composer-text').addEventListener('keydown', (event) => {
    if (!driving || event.key !== 'Enter' || event.shiftKey) return;
    if (!sticky.ctrl && !sticky.alt) return;
    event.preventDefault();
    const text = $('composer-text').value;
    if (sticky.ctrl && text.length === 1) {
      const byte = controlByte(text);
      if (byte) sendKeys(byte);
    } else {
      sendKeys(sticky.alt ? `\x1b${text}` : text);
    }
    sticky.ctrl = false;
    sticky.alt = false;
    for (const button of $('term-keys').querySelectorAll('[data-sticky]')) {
      button.classList.remove('sticky-on');
    }
    $('composer-text').value = '';
  }, true);
  $('thread-back').addEventListener('click', closePane);
  buildKeyRow();
  $('term-take').addEventListener('click', () => {
    rrp.send({ t: 'control_request', pane: current })
      .then(() => { driving = true; updateDriveUi(); $('composer-text').focus(); })
      .catch((error) => { $('term-note').textContent = error.message; });
  });
  $('term-release').addEventListener('click', () => {
    rrp.send({ t: 'control_release', pane: current }).catch(() => {});
    driving = false;
    directKeys = false;
    updateDriveUi();
  });
  $('term-direct').addEventListener('click', () => setDirectKeys(!directKeys));
  $('term-capture').addEventListener('keydown', onDirectKey);
  // Never let the field accumulate text: it is a focus target, not an input.
  $('term-capture').addEventListener('input', (event) => { event.target.value = ''; });
  $('term-capture').addEventListener('paste', (event) => {
    event.preventDefault();
    const text = event.clipboardData?.getData('text') || '';
    if (text && current) {
      toLive();
      rrp.send({ t: 'paste', pane: current, text }).catch(() => {});
    }
  });
  // Tapping the screen while typing directly puts the keyboard back where it belongs.
  $('screen-wrap').addEventListener('click', () => {
    if (driving && directKeys) $('term-capture').focus();
  });
  $('term-new-output').addEventListener('click', () => toLive());
  $('notify').addEventListener('click', toggleNotifications);
  $('pair-retry').addEventListener('click', () => location.reload());
  $('forget').addEventListener('click', async () => {
    await forgetDevice();
    rrp.close();
    location.replace(location.pathname);
  });

  // Opened as a pairing link, but the part after '#' — the one-time code — is gone. The usual
  // cause is the certificate warning: after "visit this website" some browsers reload the page
  // without the fragment. The code is still valid, so scanning again is the whole fix.
  if (!location.hash.includes('v=1') && /\/pair\/?$/.test(location.pathname)) {
    show('welcome');
    $('welcome-note').textContent = 'This pairing link arrived without its code — usually because '
      + 'the browser reloaded the page after the certificate warning. Scan the QR code on your '
      + 'desktop again; it stays valid for five minutes.';
    return;
  }

  if (location.hash.includes('v=1')) {
    let link;
    try {
      link = Rrp.parsePairFragment(location.hash);
    } catch (error) {
      show('welcome');
      $('welcome-note').textContent = error.message;
      return;
    }
    startPairing(link);
    return;
  }
  connectStored();
});

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('./sw.js').catch(() => {});
}

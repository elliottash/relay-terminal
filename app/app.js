// SPDX-License-Identifier: GPL-3.0-or-later
// The phone client: pair, inbox, thread, composer.
//
// Agent output and pane titles are attacker-controlled — a program can print anything and a
// terminal title comes from OSC sequences — so every string from the wire goes in through
// textContent. There is no innerHTML in this file, and the CSP forbids inline script anyway.

import { Rrp, loadDevice, forgetDevice, fingerprint, b64 } from './rrp.js';
import { ScreenView, KEYS, controlByte } from './screen.js';

const rrp = new Rrp();
let panes = [];
let current = null;
let pending = null;          // the turn being rendered
let reconnectTimer = null;
let capability = 'view';
let features = [];
let screenView = null;
let tab = 'agent';
let driving = false;
let sticky = { ctrl: false, alt: false };

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
  show('inbox');
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

// ---- thread -----------------------------------------------------------------------------------

function openPane(paneId) {
  current = paneId;
  const pane = panes.find((item) => item.id === paneId);
  $('thread-title').textContent = pane?.title || paneId;
  $('thread-cwd').textContent = pane?.cwd || '';
  $('thread-body').replaceChildren();
  pending = null;
  driving = false;
  const hasScreen = features.includes('screen');
  $('thread-tabs').hidden = !hasScreen;
  setTab(hasScreen ? 'terminal' : 'agent');
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

function setTab(which) {
  tab = which;
  const terminal = which === 'terminal';
  $('tab-agent').classList.toggle('is-on', !terminal);
  $('tab-terminal').classList.toggle('is-on', terminal);
  $('thread-body').hidden = terminal;
  $('composer').hidden = terminal;
  $('terminal-pane').hidden = !terminal;
  if (terminal) {
    if (!screenView) screenView = new ScreenView($('screen-wrap'));
    screenView.fit();
    updateDriveUi();
    rrp.send({ t: 'screen_get', pane: current }).catch(() => {});
  }
}

function onScreen(message) {
  if (message.pane !== current) return;
  if (!screenView) screenView = new ScreenView($('screen-wrap'));
  screenView.apply(message);
  const wrap = $('screen-wrap');
  wrap.scrollTop = wrap.scrollHeight;
}

function updateDriveUi() {
  const allowed = capability === 'full';
  $('term-take').hidden = driving || !allowed;
  $('term-release').hidden = !driving;
  $('term-keys').hidden = !driving;
  $('term-composer').hidden = !driving;
  $('term-mode').textContent = driving ? 'You have the keyboard' : 'Watching';
  $('term-mode').className = `chip ${driving ? 'ok' : ''}`;
  if (!allowed) {
    $('term-note').textContent = 'This device is paired for viewing only.';
  } else if (!driving) {
    $('term-note').textContent = 'Read only until you take over.';
  } else {
    $('term-note').textContent = '';
  }
}

function sendKeys(text) {
  if (!driving || !current) return;
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

// A whole line at a time is what avoids Android keyboards mangling per-key input; sticky Ctrl
// turns the next line into a control byte instead.
function sendLine() {
  const box = $('term-line');
  const text = box.value;
  if (!current || !driving) return;
  if (sticky.ctrl && text.length === 1) {
    const byte = controlByte(text);
    sticky.ctrl = false;
    toggleSticky('ctrl');
    toggleSticky('ctrl');
    if (byte) sendKeys(byte);
    box.value = '';
    return;
  }
  let payload = text;
  if (sticky.alt) { payload = `\x1b${payload}`; sticky.alt = false; }
  rrp.send({ t: 'line', pane: current, text: payload })
    .catch((error) => { $('term-note').textContent = error.message; });
  box.value = '';
}

function threadBody() {
  return $('thread-body');
}

function atBottom() {
  const body = threadBody();
  return body.scrollHeight - body.scrollTop - body.clientHeight < 80;
}

function append(node) {
  const stick = atBottom();
  threadBody().append(node);
  if (stick) threadBody().scrollTop = threadBody().scrollHeight;
}

function turnCard() {
  if (pending) return pending;
  const card = el('div', 'turn');
  const answer = el('div', 'answer');
  const tools = el('div', 'tools');
  const think = el('div', 'thinking');
  think.hidden = true;
  card.append(think, tools, answer);
  pending = { card, answer, tools, think, text: '' };
  append(card);
  return pending;
}

function onAgent(message) {
  if (message.pane !== current) return;
  const event = message.event || {};
  switch (event.event) {
    case 'queued': {
      const bubble = el('div', 'prompt');
      bubble.append(el('div', 'prompt-text', event.text || ''));
      if (event.origin) bubble.append(el('div', 'prompt-origin', event.origin));
      append(bubble);
      if (event.started) pending = null;
      break;
    }
    case 'agent_started':
      turnCard();
      $('composer-stop').hidden = false;
      break;
    case 'thinking_delta': {
      const turn = turnCard();
      turn.think.hidden = false;
      turn.think.textContent = event.text || '';
      break;
    }
    case 'thinking_done': {
      const turn = turnCard();
      turn.think.classList.add('done');
      break;
    }
    case 'tool_started': {
      const turn = turnCard();
      const row = el('div', 'tool');
      row.dataset.tool = event.tool || '';
      row.append(el('span', 'tool-name', event.tool || 'tool'));
      row.append(el('span', 'tool-preview', event.preview || ''));
      turn.tools.append(row);
      break;
    }
    case 'tool_result': {
      const turn = turnCard();
      const row = [...turn.tools.children].reverse()
        .find((item) => item.dataset.tool === (event.tool || ''));
      if (row) {
        row.classList.add(event.ok === false ? 'failed' : 'ok');
        row.append(el('span', 'tool-summary', event.summary || ''));
      }
      break;
    }
    case 'delta': {
      const turn = turnCard();
      turn.text += event.text || '';
      turn.answer.textContent = turn.text;
      if (atBottom()) threadBody().scrollTop = threadBody().scrollHeight;
      break;
    }
    case 'turn_summary': {
      const turn = turnCard();
      const foot = el('div', 'turn-foot',
        `${event.tools ?? 0} tool${event.tools === 1 ? '' : 's'} · ${(event.elapsed ?? 0).toFixed(1)}s`);
      turn.card.append(foot);
      break;
    }
    case 'agent_finished':
    case 'agent_stopped':
    case 'cancelled':
      $('composer-stop').hidden = true;
      if (event.event !== 'agent_finished') append(el('div', 'note', 'Stopped.'));
      pending = null;
      break;
    case 'plan_written': {
      const card = el('div', 'plan');
      card.append(el('div', 'plan-title', event.title || 'Plan'));
      card.append(el('pre', 'plan-body', event.preview || ''));
      const run = el('button', 'plan-run', 'Execute');
      run.type = 'button';
      run.addEventListener('click', () => {
        run.disabled = true;
        rrp.send({ t: 'plan_execute', pane: current, plan_id: event.plan_id }).catch(() => {});
      });
      card.append(run);
      append(card);
      break;
    }
    case 'recap':
      append(el('div', 'note', event.text || ''));
      break;
    case 'status':
      $('thread-note').textContent = event.text || '';
      break;
    case 'error':
      append(el('div', 'note error', event.message || 'Something failed.'));
      $('composer-stop').hidden = true;
      break;
    default:
      break;
  }
}

// ---- composer ---------------------------------------------------------------------------------

function sendPrompt() {
  const box = $('composer-text');
  const text = box.value.trim();
  if (!text || !current) return;
  const running = panes.find((pane) => pane.id === current)?.status === 'running';
  rrp.send({
    t: 'compose', pane: current, text, when: running ? 'queue' : 'now',
    msg_id: b64(crypto.getRandomValues(new Uint8Array(9))),
  }).catch((error) => append(el('div', 'note error', error.message)));
  box.value = '';
  box.style.height = 'auto';
}

// ---- wiring -----------------------------------------------------------------------------------

rrp.addEventListener('welcome', (event) => {
  capability = event.detail.capability || capability;
  features = event.detail.features || [];
  $('capability').textContent = capability;
});

rrp.addEventListener('screen_snapshot', (event) => onScreen(event.detail));
rrp.addEventListener('screen_diff', (event) => onScreen(event.detail));

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
  }
});

rrp.addEventListener('agent', (event) => onAgent(event.detail));

rrp.addEventListener('error', (event) => {
  const detail = event.detail || {};
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
  $('composer-stop').addEventListener('click', () => {
    rrp.send({ t: 'agent_stop', pane: current }).catch(() => {});
  });
  $('thread-back').addEventListener('click', closePane);
  $('tab-agent').addEventListener('click', () => setTab('agent'));
  $('tab-terminal').addEventListener('click', () => setTab('terminal'));
  buildKeyRow();
  $('term-take').addEventListener('click', () => {
    rrp.send({ t: 'control_request', pane: current })
      .then(() => { driving = true; updateDriveUi(); $('term-line').focus(); })
      .catch((error) => { $('term-note').textContent = error.message; });
  });
  $('term-release').addEventListener('click', () => {
    rrp.send({ t: 'control_release', pane: current }).catch(() => {});
    driving = false;
    updateDriveUi();
  });
  $('term-send').addEventListener('click', sendLine);
  $('term-line').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') { event.preventDefault(); sendLine(); }
  });
  $('pair-retry').addEventListener('click', () => location.reload());
  $('forget').addEventListener('click', async () => {
    await forgetDevice();
    rrp.close();
    location.replace(location.pathname);
  });

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

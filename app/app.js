// SPDX-License-Identifier: GPL-3.0-or-later
// The phone client: pair, inbox, thread, composer.
//
// Agent output and pane titles are attacker-controlled — a program can print anything and a
// terminal title comes from OSC sequences — so every string from the wire goes in through
// textContent. There is no innerHTML in this file, and the CSP forbids inline script anyway.

import { Rrp, loadDevice, forgetDevice, fingerprint, b64 } from './rrp.js';
import { ScreenView, KEYS, controlByte, keyEventBytes } from './screen.js';

const rrp = new Rrp();
let panes = [];
let current = null;
let reconnectTimer = null;
let capability = 'view';
let features = [];
let screenView = null;
let tab = 'agent';
let driving = false;
let sticky = { ctrl: false, alt: false };
let directKeys = false;
let answerNode = null;      // the answer being streamed, without a terminal to print into

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
  if (!screenView) screenView = new ScreenView($('screen-wrap'));
  screenView.fit();
  updateDriveUi();
  rrp.send({ t: 'screen_get', pane: current }).catch(() => {});
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
  if (message.pane !== current) return;
  const event = message.event || {};
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
      answerNode = null;
      break;
    }
    case 'tool_started': {
      const row = el('div', 'tool');
      row.append(el('span', 'tool-name', event.tool || 'tool'));
      row.append(el('span', 'tool-preview', event.preview || ''));
      body.append(row);
      answerNode = null;
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
    case 'agent_finished':
    case 'agent_stopped':
    case 'cancelled':
      answerNode = null;
      break;
    default:
      break;
  }
  if (stick) body.scrollTop = body.scrollHeight;
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
    if (text && current) rrp.send({ t: 'paste', pane: current, text }).catch(() => {});
  });
  // Tapping the screen while typing directly puts the keyboard back where it belongs.
  $('screen-wrap').addEventListener('click', () => {
    if (driving && directKeys) $('term-capture').focus();
  });
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

// SPDX-License-Identifier: GPL-3.0-or-later
// The web view of one Relay pane: what the desktop pane shows under its terminal, drawn from the
// desktop's `pane_state` message (docs/REMOTE-PROTOCOL.md, "pane_state"; src/PaneState.h).
//
//   const view = mountPane(container, { send, keymap });
//   view.update(paneState);        // every pane_state for this pane
//   view.onEditText(message);      // the desktop's queue_edit_text answer
//   view.terminalSlot              // where the host puts the terminal canvas
//   view.destroy();
//
// The layout is the Qt pane's, top to bottom: terminal, thinking bubble, queue strip, prompt box
// with its strip (folder, turn clock, "% left", model). The view writes none of the pane's words:
// every label, hint and title comes from the message, and every action a row offers is one the
// desktop listed for that row. What the view adds is the device: an action sheet and a send menu
// for touch, the desktop's keys and its "Next time" hints for a keyboard.
//
// Everything from the wire goes in through textContent. There is no innerHTML here, and styles are
// set through CSSOM only, so the app's CSP (no inline script or style) holds.

import { visibleHeight } from './viewport.js';

const ACTION_ORDER = ['edit', 'steer', 'send_now', 'to_queue', 'up', 'down', 'remove'];

// The desktop's own words for each action (the queue strip's hint line and row tooltips, Pane.h).
const ACTION_WORDS = {
  remove: (row) => (row.kind === 'steer' ? 'Withdraw' : 'Remove'),
  edit: () => 'Edit',
  to_queue: () => 'Back to the queue',
  send_now: () => 'Send now',
  steer: () => 'At the next tool call',
  up: () => 'Move up',
  down: () => 'Move down',
};

// The send menu: the three places a prompt can go while the agent works.
const SEND_WHEN = [
  ['queue', 'after this turn'],
  ['steer', 'at the next tool call'],
  ['now', 'now'],
];

// Key text for the hints. The queue keys are fixed on the desktop (Pane.h, rebuildQueueStrip);
// `send_now` and `sessions` are Keymap actions, so a host passes the live text for those.
const DEFAULT_KEYS = {
  select: '↑', remove: 'Shift+Delete', move: 'Ctrl+↑/↓', moveUp: 'Ctrl+↑', moveDown: 'Ctrl+↓',
  edit: 'Enter', leave: 'Esc', send: 'Enter', sendNow: 'Ctrl+Enter', sessions: 'Ctrl+Shift+Y',
};
const KEYMAP_ALIASES = { 'agent.interrupt': 'sendNow', 'agent.resume': 'sessions' };

const STEER_WINDOW_MS = 15000;       // Enter again on an empty box within this makes a steer
const HINT_LIMIT = 3;                // relay::ShortcutHints defaults
const HINT_COOLDOWN_MS = 600 * 1000;
const HINT_GAP_MS = 20 * 1000;
const TOAST_MS = 4000;
const LONG_PRESS_MS = 450;
const HINT_STORE = 'relay.pane.hints';

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined && text !== null) node.textContent = text;
  return node;
}

function button(className, text, label) {
  const node = el('button', className, text);
  node.type = 'button';
  if (label) node.setAttribute('aria-label', label);
  return node;
}

function svgIcon(paths) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 16 16');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('class', 'rp-icon');
  for (const d of paths) {
    const path = document.createElementNS(ns, 'path');
    path.setAttribute('d', d);
    svg.appendChild(path);
  }
  return svg;
}

const ICON_SEND = ['M8 13V3', 'M3.5 7.5 8 3l4.5 4.5'];
const ICON_LIST = ['M5 4h8', 'M5 8h8', 'M5 12h8', 'M2.5 4h.01', 'M2.5 8h.01', 'M2.5 12h.01'];

const str = (value) => (typeof value === 'string' ? value : '');
const obj = (value) => (value && typeof value === 'object' && !Array.isArray(value) ? value : null);
const arr = (value) => (Array.isArray(value) ? value : []);

// "Ctrl+Shift+Y" against a KeyboardEvent.
function keyMatches(text, event) {
  if (!text) return false;
  const parts = text.split('+');
  const key = parts.pop();
  const want = new Set(parts.map((p) => p.toLowerCase()));
  if (want.has('ctrl') !== event.ctrlKey || want.has('shift') !== event.shiftKey
      || want.has('alt') !== event.altKey || want.has('meta') !== event.metaKey) return false;
  return event.key.toLowerCase() === key.toLowerCase();
}

// How an action was asked for: a touch, the keyboard (a button's click from Enter or Space has no
// detail), or the mouse, which is the one that earns a "Next time" hint.
function howOf(event, input) {
  if (event.pointerType === 'touch' || input === 'touch') return 'touch';
  return event.detail === 0 ? 'keyboard' : 'mouse';
}

let mounts = 0;

function readHintCounts() {
  try { return JSON.parse(localStorage.getItem(HINT_STORE) || '{}') || {}; } catch { return {}; }
}

function writeHintCounts(counts) {
  try { localStorage.setItem(HINT_STORE, JSON.stringify(counts)); } catch { /* private window */ }
}

export function mountPane(container, options = {}) {
  const send = typeof options.send === 'function' ? options.send : () => {};
  const keys = { ...DEFAULT_KEYS };
  for (const [name, text] of Object.entries(options.keymap || {})) {
    if (typeof text === 'string' && text) keys[KEYMAP_ALIASES[name] || name] = text;
  }
  const clock = typeof options.now === 'function' ? options.now : () => Date.now();
  const uid = `rp${++mounts}`;

  let state = null;           // the last pane_state drawn
  let lastSeq = -Infinity;
  let selectedRow = '';       // row id highlighted by the keyboard
  let thinkingExpanded = false;
  let thinkingHidden = false; // × on the bubble: hidden until the next turn's reasoning
  let pendingTail = null;     // reasoning that arrived while text in the bubble was selected
  let staged = null;          // the three-step Enter: {text, stage, at, rowId, steerId, known}
  let typedAhead = '';        // keys typed on a selected row, for when its text comes back
  let editRow = '';           // the row whose text is in the prompt box
  let toastTimer = 0;
  const hintLast = new Map();
  let hintLastAny = -Infinity;

  // ---- the device -------------------------------------------------------------------------
  const root = el('div', 'relay-pane');
  if (options.theme) root.dataset.theme = options.theme;
  const input = () => options.input
    || (matchMedia('(hover: none) and (pointer: coarse)').matches ? 'touch' : 'mouse');
  function placeDevice() {
    const width = root.clientWidth || container.clientWidth || window.innerWidth;
    const kind = input();
    root.dataset.input = kind;
    root.dataset.device = width < 600 ? 'phone' : (kind === 'touch' || width < 900) ? 'tablet' : 'laptop';
  }

  // ---- the terminal and the toast ---------------------------------------------------------
  const termWrap = el('div', 'rp-term-wrap');
  const terminalSlot = el('div', 'rp-terminal');
  const toast = el('div', 'rp-toast');
  toast.setAttribute('role', 'status');
  toast.setAttribute('aria-live', 'polite');
  toast.hidden = true;
  termWrap.append(terminalSlot, toast);

  // ---- the thinking bubble ----------------------------------------------------------------
  const thinking = el('section', 'rp-thinking');
  thinking.setAttribute('aria-label', 'Reasoning');
  thinking.hidden = true;
  const thinkingHead = el('div', 'rp-thinking-head');
  const thinkingHeader = el('span', 'rp-thinking-header');
  const thinkingToggle = button('rp-icon-button rp-thinking-toggle', '▴', 'Show more reasoning');
  thinkingToggle.setAttribute('aria-expanded', 'false');
  const thinkingClose = button('rp-icon-button rp-thinking-close', '×', 'Hide reasoning');
  thinkingHead.append(thinkingHeader, thinkingToggle, thinkingClose);
  const thinkingTail = el('div', 'rp-thinking-tail');
  thinkingTail.tabIndex = 0;
  thinkingTail.setAttribute('role', 'log');
  thinkingTail.setAttribute('aria-label', 'Reasoning text');
  thinking.append(thinkingHead, thinkingTail);

  // ---- the queue strip --------------------------------------------------------------------
  const queue = el('section', 'rp-queue');
  queue.setAttribute('aria-label', 'Queue');
  queue.hidden = true;
  const queueHead = el('div', 'rp-queue-head');
  const queueTitle = el('span', 'rp-queue-title');
  const queueHint = el('span', 'rp-queue-hint');
  queueHead.append(queueTitle, queueHint);
  const queueReason = el('div', 'rp-queue-reason');
  const running = el('div', 'rp-queue-running');
  const runningLead = el('span', 'rp-running-lead', '▸ running');
  const runningLabel = el('span', 'rp-running-label');
  running.append(runningLead, runningLabel);
  const rows = el('ul', 'rp-rows');
  rows.setAttribute('role', 'listbox');
  rows.setAttribute('aria-label', 'Queued prompts');
  rows.tabIndex = 0;
  queue.append(queueHead, queueReason, running, rows);

  // ---- the prompt box ---------------------------------------------------------------------
  const composer = el('form', 'rp-composer');
  composer.setAttribute('aria-label', 'Prompt');
  composer.hidden = true;
  const line = el('div', 'rp-composer-line');
  const box = el('textarea', 'rp-input');
  box.rows = 1;
  box.setAttribute('aria-label', 'Prompt');
  box.setAttribute('enterkeyhint', 'send');
  box.autocapitalize = 'off';
  box.spellcheck = false;
  const modeChip = el('span', 'rp-chip rp-mode');
  line.append(box, modeChip);
  const strip = el('div', 'rp-strip');
  const folderChip = el('span', 'rp-chip rp-folder');
  const sessionsButton = button('rp-chip rp-sessions-button', '', 'Conversations');
  sessionsButton.setAttribute('aria-haspopup', 'dialog');
  sessionsButton.append(svgIcon(ICON_LIST));
  const sessionsTitle = el('span', 'rp-sessions-title');
  sessionsButton.append(sessionsTitle);
  const spacer = el('span', 'rp-spacer');
  const clockChip = el('span', 'rp-chip rp-clock');
  const contextChip = el('span', 'rp-chip rp-context');
  const modelWrap = el('span', 'rp-model-wrap');
  const model = el('select', 'rp-chip rp-model');
  model.setAttribute('aria-label', 'Model');
  const modelChevron = el('span', 'rp-model-chevron', '▾');
  modelChevron.setAttribute('aria-hidden', 'true');
  modelWrap.append(model, modelChevron);
  // Where the host puts a control of its own (the client's microphone), so its buttons sit in the
  // pane's strip instead of a second bar under it. Empty and invisible until the host fills it.
  const hostSlot = el('span', 'rp-host-slot');
  const sendGroup = el('span', 'rp-send-group');
  const sendButton = button('rp-send', '', 'Send');
  sendButton.append(svgIcon(ICON_SEND));
  const sendMenuButton = button('rp-send-menu', '▾', 'When to send');
  sendMenuButton.setAttribute('aria-haspopup', 'menu');
  sendGroup.append(sendButton, sendMenuButton);
  strip.append(folderChip, sessionsButton, spacer, clockChip, contextChip, modelWrap, hostSlot, sendGroup);
  composer.append(line, strip);

  // ---- sheets: row actions, the send menu, the session manager -----------------------------
  const layer = el('div', 'rp-layer');
  layer.hidden = true;
  const backdrop = el('div', 'rp-backdrop');
  const sheet = el('div', 'rp-sheet');
  sheet.setAttribute('role', 'dialog');
  sheet.setAttribute('aria-modal', 'true');
  layer.append(backdrop, sheet);
  let sheetOpener = null;
  let sheetKind = '';

  root.append(termWrap, thinking, queue, composer, layer);
  container.appendChild(root);
  placeDevice();
  const resize = typeof ResizeObserver === 'function' ? new ResizeObserver(placeDevice) : null;
  if (resize) resize.observe(root);

  // ---- messages ---------------------------------------------------------------------------
  const paneId = () => (state && state.pane) || options.pane || '';
  function emit(t, fields = {}) {
    send({ t, pane: paneId(), ...fields });
  }

  // 72 random bits, the shape the client's composer already sends.
  function messageId() {
    const bytes = crypto.getRandomValues(new Uint8Array(9));
    return btoa(String.fromCharCode(...bytes));
  }

  // ---- hints (laptop only) ----------------------------------------------------------------
  function showToast(text) {
    toast.textContent = text;
    toast.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toast.hidden = true; }, TOAST_MS);
  }

  function hint(id, text) {
    if (options.hints === false || root.dataset.input === 'touch' || !text) return;
    const now = clock();
    const counts = readHintCounts();
    if ((counts[id] || 0) >= HINT_LIMIT) return;
    if (now - (hintLast.get(id) ?? -Infinity) < HINT_COOLDOWN_MS) return;
    if (now - hintLastAny < HINT_GAP_MS) return;
    counts[id] = (counts[id] || 0) + 1;
    writeHintCounts(counts);
    hintLast.set(id, now);
    hintLastAny = now;
    showToast(text);
  }

  const next = (...parts) => `Next time: ${parts.join(' then ')}`;
  const ACTION_HINTS = {
    edit: () => next(keys.select, keys.edit),
    steer: () => next(keys.select, keys.moveUp),
    send_now: () => next(keys.select, keys.sendNow),
    to_queue: () => next(keys.select, keys.moveDown),
    up: () => next(keys.select, keys.moveUp),
    down: () => next(keys.select, keys.moveDown),
  };
  function removeHint(row) {
    if (row.kind === 'steer') hint('queue.steer.remove.mouse', next(keys.select, keys.remove));
    else hint('queue.remove.mouse', `Next time: ${keys.select} selects a row, ${keys.remove} removes, ${keys.move} reorders`);
  }

  // ---- rows -------------------------------------------------------------------------------
  const rowList = () => arr(obj(state && state.queue) && state.queue.rows).filter(obj);
  const actionsOf = (row) => arr(row && row.actions).filter((a) => typeof a === 'string');
  const findRow = (id) => rowList().find((row) => row.id === id) || null;
  const selectable = () => rowList().filter((row) => actionsOf(row).length > 0);

  function act(row, action, how) {
    if (!row || !actionsOf(row).includes(action)) return;   // only what the desktop offered
    if (action === 'remove') emit('queue_remove', { row: row.id });
    else if (action === 'edit') { editRow = row.id; emit('queue_edit', { row: row.id }); }
    else if (action === 'send_now') emit('queue_send_now', { row: row.id });
    else emit('queue_move', { row: row.id, to: action });
    if (how === 'mouse') {
      if (action === 'remove') removeHint(row);
      else hint(`queue.${action}.mouse`, ACTION_HINTS[action] && ACTION_HINTS[action]());
    }
  }

  function labelInto(node, row) {
    // The label is the desktop's ("↪ next tool call  ✦ check the readme", "$ make", "✦ …"). Its
    // glyph is coloured the way the Qt delegate colours it; the words are left as they came.
    const text = str(row.label);
    node.textContent = '';
    const star = text.indexOf('✦');
    const dollar = text.startsWith('$ ') ? 0 : -1;
    const at = star >= 0 ? star : dollar;
    if (at < 0) { node.append(el('span', 'rp-row-text', text)); return; }
    if (at > 0) node.append(el('span', 'rp-row-lead', text.slice(0, at)));
    node.append(el('span', 'rp-row-glyph', text[at]));
    node.append(el('span', 'rp-row-text', text.slice(at + 1)));
  }

  function renderRows() {
    const list = rowList();
    rows.textContent = '';
    if (selectedRow && !findRow(selectedRow)) selectedRow = '';
    let activeId = '';
    list.forEach((row, index) => {
      const item = el('li', 'rp-row');
      item.id = `${uid}-row-${index}`;
      item.dataset.rowId = str(row.id);
      item.dataset.kind = str(row.kind);
      item.dataset.state = str(row.state);
      item.setAttribute('role', 'option');
      const actions = actionsOf(row);
      item.setAttribute('aria-disabled', actions.length ? 'false' : 'true');
      const selected = row.id === selectedRow;
      item.setAttribute('aria-selected', selected ? 'true' : 'false');
      if (selected) activeId = item.id;
      const label = el('span', 'rp-row-label');
      labelInto(label, row);
      item.title = str(row.label);
      item.appendChild(label);
      if (actions.includes('remove')) {
        const x = button('rp-row-x', '×', `${ACTION_WORDS.remove(row)}: ${str(row.label)}`);
        x.addEventListener('click', (event) => {
          event.stopPropagation();
          act(row, 'remove', howOf(event, root.dataset.input));
        });
        item.appendChild(x);
      }
      if (actions.length) wireRowPointer(item, row);
      rows.appendChild(item);
    });
    if (activeId) rows.setAttribute('aria-activedescendant', activeId);
    else rows.removeAttribute('aria-activedescendant');
    const current = rows.querySelector('[aria-selected="true"]');
    if (current && current.scrollIntoView) current.scrollIntoView({ block: 'nearest' });
  }

  function wireRowPointer(item, row) {
    let pressTimer = 0;
    let longPressed = false;
    item.addEventListener('pointerdown', (event) => {
      if (event.pointerType !== 'touch') return;
      longPressed = false;
      pressTimer = setTimeout(() => { longPressed = true; openRowSheet(row, item); }, LONG_PRESS_MS);
    });
    const cancel = () => clearTimeout(pressTimer);
    item.addEventListener('pointerup', cancel);
    item.addEventListener('pointercancel', cancel);
    item.addEventListener('pointerleave', cancel);
    item.addEventListener('contextmenu', (event) => {
      event.preventDefault();
      if (!longPressed) openRowSheet(row, item);
    });
    item.addEventListener('click', () => {
      if (longPressed) { longPressed = false; return; }
      selectedRow = row.id;
      renderRows();
      openRowSheet(row, item);
    });
  }

  function selectRow(id) {
    selectedRow = id;
    renderRows();
    if (document.activeElement !== rows) rows.focus({ preventScroll: true });
  }

  function leaveRows() {
    selectedRow = '';
    renderRows();
    box.focus();
  }

  // ---- the sheet --------------------------------------------------------------------------
  function openSheet(kind, title, build, opener) {
    closeSheet(false);
    sheetKind = kind;
    sheetOpener = opener || document.activeElement;
    sheet.textContent = '';
    sheet.dataset.kind = kind;
    if (title) {
      const heading = el('div', 'rp-sheet-title', title);
      heading.id = `rp-sheet-title-${kind}`;
      sheet.setAttribute('aria-labelledby', heading.id);
      sheet.appendChild(heading);
    } else {
      sheet.removeAttribute('aria-labelledby');
    }
    build(sheet);
    layer.hidden = false;
    placeSheet(opener);
    const first = sheet.querySelector('button:not([disabled])');
    if (first) first.focus();
  }

  function placeSheet(opener) {
    // A phone gets a bottom sheet (CSS). Wider screens get a popover beside what opened it.
    sheet.style.left = '';
    sheet.style.top = '';
    sheet.style.bottom = '';
    if (root.dataset.device === 'phone' || !opener || !opener.getBoundingClientRect) return;
    const area = root.getBoundingClientRect();
    const at = opener.getBoundingClientRect();
    const width = sheet.offsetWidth;
    const height = sheet.offsetHeight;
    const left = Math.max(8, Math.min(at.right - area.left - width, area.width - width - 8));
    const above = at.top - area.top - height - 6;
    const top = above >= 8 ? above : Math.min(at.bottom - area.top + 6, area.height - height - 8);
    sheet.style.left = `${Math.round(left)}px`;
    sheet.style.top = `${Math.round(Math.max(8, top))}px`;
  }

  function closeSheet(restoreFocus = true) {
    if (layer.hidden) return;
    layer.hidden = true;
    sheet.textContent = '';
    sheetKind = '';
    const opener = sheetOpener;
    sheetOpener = null;
    if (restoreFocus && opener && opener.isConnected && opener.focus) opener.focus();
  }

  function sheetButton(className, text, onClick) {
    const node = button(`rp-sheet-item ${className}`, text);
    node.addEventListener('click', (event) => {
      const how = howOf(event, root.dataset.input);
      closeSheet();
      onClick(how);
    });
    return node;
  }

  function openRowSheet(row, anchor) {
    const actions = actionsOf(row);
    if (!actions.length) return;
    const ordered = ACTION_ORDER.filter((a) => actions.includes(a))
      .concat(actions.filter((a) => !ACTION_ORDER.includes(a) && ACTION_WORDS[a]));
    openSheet('row', str(row.label), (node) => {
      node.dataset.rowId = str(row.id);
      for (const action of ordered) {
        if (!ACTION_WORDS[action]) continue;   // an action this client has no words for
        const item = sheetButton('', ACTION_WORDS[action](row), (how) => act(row, action, how));
        item.dataset.action = action;
        if (action === 'remove') item.classList.add('rp-danger');
        node.appendChild(item);
      }
    }, anchor);
  }

  function openSendMenu() {
    const text = box.value;
    openSheet('send', '', (node) => {
      node.setAttribute('aria-label', 'When to send');
      for (const [when, words] of SEND_WHEN) {
        const item = sheetButton('', words, (how) => {
          compose(text, when);
          if (how === 'mouse') {
            if (when === 'now') hint('compose.now.mouse', `Next time: ${keys.sendNow}`);
            else if (when === 'steer') hint('compose.steer.mouse', `Next time: ${keys.send}, then ${keys.send} again on the empty prompt box`);
          }
        });
        item.dataset.when = when;
        item.disabled = !text.trim();
        node.appendChild(item);
      }
    }, sendMenuButton);
  }

  function openSessions() {
    const sessions = obj(state && state.sessions);
    if (!sessions) return;
    openSheet('sessions', '', (node) => {
      node.setAttribute('aria-label', 'Conversations');
      if (sessions.can_new === true) {
        const fresh = sheetButton('rp-new-conversation', 'New conversation', () => emit('conversation_new'));
        node.appendChild(fresh);
      }
      const list = el('ul', 'rp-session-list');
      // Opening one is the owner's level, and the current conversation is already open: a row is a
      // button only where the desktop said so (`can_open`), and a plain line otherwise.
      const canOpen = sessions.can_open === true;
      for (const session of arr(sessions.rows).filter(obj)) {
        const item = el('li', 'rp-session-row');
        item.dataset.sessionId = str(session.id);
        const current = session.current === true;
        if (current) {
          item.classList.add('rp-current');
          item.setAttribute('aria-current', 'true');
        }
        const dot = el('span', 'rp-session-dot');
        dot.setAttribute('aria-hidden', 'true');
        if (session.running === true) dot.classList.add('rp-running');
        const title = el('span', 'rp-session-title', str(session.title));
        const when = el('span', 'rp-session-when', str(session.when));
        if (canOpen && !current) {
          const open = button('rp-session-open', '');
          open.setAttribute('aria-label', `Open ${str(session.title)}`);
          open.append(dot, title, when);
          open.addEventListener('click', () => {
            closeSheet();
            emit('conversation_open', { session: str(session.id) });
          });
          item.appendChild(open);
        } else {
          item.append(dot, title, when);
        }
        list.appendChild(item);
      }
      node.appendChild(list);
    }, sessionsButton);
  }

  // ---- the prompt box ---------------------------------------------------------------------
  const busy = () => !!(obj(state && state.turn) && state.turn.busy === true);

  function compose(text, when) {
    if (!text.trim()) return;
    // `agent: false` asks the desktop to route the line the way its own prompt box would — a
    // command runs in the shell, anything else goes to the agent. Only a device the desktop
    // trusts with typing may ask for that, and the state says so: a full device is offered the
    // composer's own modes, an agent device only "agent". Without this a typed command reached
    // the agent, which ran it as a tool call on the owner's key (found by #W5N2's end-to-end QA).
    const modes = arr(state && state.composer && state.composer.modes);
    const mayRoute = modes.includes('auto') || modes.includes('shell');
    emit('compose', {
      text, when,
      ...(mayRoute ? { agent: false } : {}),
      // The same dedup id the client's older composer sends, so a retried send is not two prompts.
      msg_id: messageId(),
    });
    editRow = '';
    staged = when === 'queue'
      ? { text, stage: 1, at: clock(), rowId: '', steerId: '', known: new Set(rowList().map((r) => r.id)) }
      : null;
    box.value = '';
    fitBox();
    renderSendState();
  }

  function enter() {
    const text = box.value;
    if (text.trim()) {
      compose(text, busy() ? 'queue' : 'now');
      return;
    }
    // Empty box: the second Enter makes the last queued prompt a steer, the third sends it now.
    if (!staged || clock() - staged.at > STEER_WINDOW_MS) { staged = null; return; }
    if (staged.stage === 1) {
      if (staged.rowId) emit('queue_move', { row: staged.rowId, to: 'steer' });
      else emit('compose', { text: staged.text, when: 'steer' });
      staged = { ...staged, stage: 2, at: clock(), known: new Set(rowList().map((r) => r.id)) };
    } else if (staged.stage === 2) {
      // The steer this view made, or the queued row itself if the desktop kept its id.
      const kept = findRow(staged.rowId);
      const row = staged.steerId || (kept && actionsOf(kept).includes('send_now') ? kept.id : '');
      if (row) emit('queue_send_now', { row });
      else emit('compose', { text: staged.text, when: 'now' });
      staged = null;
    }
  }

  function trackStaged() {
    // Which row is the prompt this view just queued (or just made a steer): the first new one of
    // the right kind since it was sent.
    if (!staged) return;
    const fresh = rowList().filter((row) => !staged.known.has(row.id));
    if (staged.stage === 1 && !staged.rowId) {
      const row = fresh.find((r) => r.kind === 'agent' || r.kind === 'command');
      if (row) staged.rowId = row.id;
    } else if (staged.stage === 2 && !staged.steerId) {
      const row = fresh.find((r) => r.kind === 'steer');
      if (row) staged.steerId = row.id;
    }
  }

  function fitBox() {
    // An empty box is measured against its placeholder, which is the desktop's own sentence and
    // wraps to two lines on a phone: sized to the content alone it would clip the second line.
    // Nothing is painted between the two assignments, so the value never flickers.
    const empty = !box.value;
    if (empty) box.value = box.placeholder;
    box.style.height = 'auto';
    // The visible height, not innerHeight: with an on-screen keyboard up Safari still reports the
    // whole screen, and a box allowed 30% of that fills most of what is left (app/viewport.js).
    const max = Math.round(visibleHeight() * 0.3);
    box.style.height = `${Math.min(box.scrollHeight, max)}px`;
    if (empty) box.value = '';
  }

  function renderSendState() {
    const hasText = !!box.value.trim();
    sendButton.disabled = !hasText;
    sendMenuButton.hidden = !busy();
  }

  // ---- drawing ----------------------------------------------------------------------------
  function show(node, text) {
    node.textContent = text;
    node.hidden = !text;
  }

  function selectionInside(node) {
    const selection = window.getSelection && window.getSelection();
    if (!selection || selection.isCollapsed || !selection.rangeCount) return false;
    return node.contains(selection.getRangeAt(0).commonAncestorContainer);
  }

  function setTail(text) {
    // Never replace the text under a selection: that was the desktop's bug (a selection lost to
    // every streamed chunk). The latest tail waits until the selection is gone.
    if (selectionInside(thinkingTail)) { pendingTail = text; return; }
    pendingTail = null;
    if (thinkingTail.textContent === text) return;
    const follow = thinkingTail.scrollHeight - thinkingTail.scrollTop - thinkingTail.clientHeight < 8;
    thinkingTail.textContent = text;
    if (follow) thinkingTail.scrollTop = thinkingTail.scrollHeight;
  }

  function renderThinking() {
    const t = obj(state.thinking);
    const visible = !!(t && t.visible === true);
    if (!visible) thinkingHidden = false;
    thinking.hidden = !visible || thinkingHidden;
    if (!t) return;
    thinkingHeader.textContent = str(t.header);
    setTail(str(t.tail));
    thinking.classList.toggle('rp-expanded', thinkingExpanded);
    thinkingToggle.textContent = thinkingExpanded ? '▾' : '▴';
    thinkingToggle.setAttribute('aria-expanded', thinkingExpanded ? 'true' : 'false');
    thinkingToggle.setAttribute('aria-label', thinkingExpanded ? 'Show less reasoning' : 'Show more reasoning');
  }

  function renderQueue() {
    const q = obj(state.queue);
    const list = rowList();
    const runningRow = q && obj(q.running);
    const visible = !!q && (list.length > 0 || q.paused === true || !!(runningRow && str(runningRow.label)));
    queue.hidden = !visible;
    if (!q) { rows.textContent = ''; return; }
    queueTitle.textContent = q.paused === true ? 'QUEUE · PAUSED' : 'QUEUE';
    show(queueHint, str(q.hint));
    show(queueReason, q.paused === true ? str(q.pause_reason) : '');
    const label = runningRow ? str(runningRow.label) : '';
    running.hidden = !label;
    runningLabel.textContent = label;
    rows.hidden = list.length === 0;
    renderRows();
  }

  function renderStrip() {
    const c = obj(state.composer);
    composer.hidden = !c;
    if (c) {
      const placeholder = str(c.placeholder);
      if (box.placeholder !== placeholder) { box.placeholder = placeholder; fitBox(); }
      const mode = str(c.mode);
      show(modeChip, mode);
      composer.dataset.mode = mode;
      modeChip.dataset.dest = mode;
    }
    const folder = obj(state.folder);
    show(folderChip, folder ? str(folder.label) : '');
    const turn = obj(state.turn);
    show(clockChip, turn ? str(turn.clock) : '');
    root.dataset.phase = turn ? str(turn.phase) : '';
    const context = obj(state.context);
    show(contextChip, context ? str(context.label) : '');
    const left = context && typeof context.percent_left === 'number' ? context.percent_left : null;
    contextChip.dataset.warn = left !== null && left <= 15 ? 'true' : 'false';
    renderModel();
    const sessions = obj(state.sessions);
    sessionsButton.hidden = !sessions;
    const current = sessions && arr(sessions.rows).filter(obj).find((s) => s.current === true);
    sessionsTitle.textContent = current ? str(current.title) : '';
    renderSendState();
    // The strip is the prompt box's bottom row; with no prompt box (a view-only device) it is
    // still how the model, the clock and the sessions are seen, so it stands on its own.
    if (!c) root.classList.add('rp-no-composer'); else root.classList.remove('rp-no-composer');
    // Send belongs to the prompt box: a device the hub sends no `composer` (a `view` device) may
    // not compose at all, and the strip it keeps must not offer it a send button anyway.
    sendGroup.hidden = !c;
    if (!c && strip.parentNode === composer) root.insertBefore(strip, layer);
    if (c && strip.parentNode !== composer) composer.appendChild(strip);
  }

  function renderModel() {
    const m = obj(state.model);
    modelWrap.hidden = !m;
    if (!m) return;
    const choices = arr(m.choices).filter((choice) => obj(choice) && str(choice.id));
    model.textContent = '';
    const shown = el('option', '', str(m.label));
    shown.value = '';
    shown.disabled = true;
    shown.selected = true;
    model.appendChild(shown);
    for (const choice of choices) {
      const option = el('option', '', `${choice.current === true ? '✓ ' : ''}${str(choice.label)}`);
      option.value = str(choice.id);
      model.appendChild(option);
    }
    model.selectedIndex = 0;
    model.disabled = choices.length === 0;
    modelWrap.dataset.pickable = choices.length ? 'true' : 'false';
  }

  function draw() {
    renderThinking();
    renderQueue();
    renderStrip();
    if (sheetKind === 'sessions') openSessions();
    if (sheetKind === 'row') {
      const id = sheet.dataset.rowId;
      const row = findRow(id);
      if (!row || actionsOf(row).length === 0) closeSheet();
    }
  }

  // ---- events -----------------------------------------------------------------------------
  const listeners = [];
  function on(target, type, handler, opts) {
    target.addEventListener(type, handler, opts);
    listeners.push(() => target.removeEventListener(type, handler, opts));
  }

  on(box, 'input', () => { fitBox(); renderSendState(); });
  on(composer, 'submit', (event) => { event.preventDefault(); enter(); });
  on(sendButton, 'click', () => {
    const text = box.value;
    if (text.trim()) compose(text, busy() ? 'queue' : 'now');
    box.focus();
  });
  on(sendMenuButton, 'click', openSendMenu);
  on(box, 'keydown', (event) => {
    if (event.isComposing || event.keyCode === 229) return;
    if (event.key === 'Enter' && !event.shiftKey && !event.altKey && !event.metaKey) {
      event.preventDefault();
      if (event.ctrlKey) {
        // Ctrl+Enter in the prompt box: send now, interrupting a running turn (agent.interrupt).
        if (box.value.trim()) compose(box.value, 'now');
        return;
      }
      enter();
      return;
    }
    if (event.key === 'ArrowUp' && !event.ctrlKey && !event.shiftKey && !event.altKey && !box.value) {
      const first = selectable()[0];
      if (first) { event.preventDefault(); selectRow(first.id); }
    }
  });

  on(rows, 'focus', () => {
    if (!selectedRow) {
      const first = selectable()[0];
      if (first) { selectedRow = first.id; renderRows(); }
    }
  });
  on(rows, 'keydown', (event) => {
    const list = selectable();
    const row = findRow(selectedRow);
    const index = list.findIndex((r) => r.id === selectedRow);
    const plain = !event.ctrlKey && !event.shiftKey && !event.altKey && !event.metaKey;
    if (event.key === 'Escape') { event.preventDefault(); leaveRows(); return; }
    if (event.key === 'ArrowUp' && plain) {
      event.preventDefault();
      if (index > 0) selectRow(list[index - 1].id);
      else if (index < 0 && list.length) selectRow(list[0].id);
      return;
    }
    if (event.key === 'ArrowDown' && plain) {
      event.preventDefault();
      if (index >= 0 && index < list.length - 1) selectRow(list[index + 1].id);
      else leaveRows();
      return;
    }
    if (!row) return;
    const actions = actionsOf(row);
    if (event.key === 'ArrowUp' && event.ctrlKey && !event.shiftKey) {
      event.preventDefault();
      if (actions.includes('up')) act(row, 'up', 'keyboard');
      else if (actions.includes('steer')) act(row, 'steer', 'keyboard');
      return;
    }
    if (event.key === 'ArrowDown' && event.ctrlKey && !event.shiftKey) {
      event.preventDefault();
      if (actions.includes('down')) act(row, 'down', 'keyboard');
      else if (actions.includes('to_queue')) act(row, 'to_queue', 'keyboard');
      return;
    }
    if (event.key === 'Delete' && event.shiftKey && !event.ctrlKey) {
      event.preventDefault();
      act(row, 'remove', 'keyboard');
      return;
    }
    if (event.key === 'Enter' && event.ctrlKey && !event.shiftKey) {
      event.preventDefault();
      act(row, 'send_now', 'keyboard');
      return;
    }
    if (event.key === 'Enter' && plain) {
      event.preventDefault();
      act(row, 'edit', 'keyboard');
      if (actions.includes('edit')) box.focus();
      return;
    }
    if (event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey
        && actions.includes('edit')) {
      // Typing on a row takes it back into the prompt box, as on the desktop; what was typed
      // follows the row's text when the desktop sends it.
      event.preventDefault();
      typedAhead += event.key;
      act(row, 'edit', 'keyboard');
      box.focus();
    }
  });

  on(thinkingToggle, 'click', () => { thinkingExpanded = !thinkingExpanded; if (state) renderThinking(); });
  on(thinkingClose, 'click', () => { thinkingHidden = true; thinking.hidden = true; box.focus(); });
  on(thinkingTail, 'click', () => {
    // A tap opens or closes the bubble on a touch screen; a tap that ends a selection does not.
    if (root.dataset.input !== 'touch' || selectionInside(thinkingTail)) return;
    thinkingExpanded = !thinkingExpanded;
    if (state) renderThinking();
  });
  on(document, 'selectionchange', () => {
    if (pendingTail !== null && !selectionInside(thinkingTail)) setTail(pendingTail);
  });

  on(model, 'change', () => {
    const choice = model.value;
    model.selectedIndex = 0;
    if (choice) emit('model_pick', { choice });
  });
  on(sessionsButton, 'click', (event) => {
    openSessions();
    if (howOf(event, root.dataset.input) === 'mouse') hint('sessions.mouse', `Next time: ${keys.sessions}`);
  });
  on(backdrop, 'click', () => closeSheet());
  on(layer, 'keydown', (event) => {
    if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); closeSheet(); return; }
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      const items = [...sheet.querySelectorAll('button:not([disabled])')];
      const at = items.indexOf(document.activeElement);
      if (!items.length) return;
      event.preventDefault();
      const step = event.key === 'ArrowDown' ? 1 : -1;
      items[(at + step + items.length) % items.length].focus();
      return;
    }
    if (event.key === 'Tab') {
      const items = [...sheet.querySelectorAll('button:not([disabled])')];
      if (!items.length) return;
      const at = items.indexOf(document.activeElement);
      event.preventDefault();
      const step = event.shiftKey ? -1 : 1;
      items[(at + step + items.length) % items.length].focus();
    }
  });
  on(root, 'keydown', (event) => {
    if (keyMatches(keys.sessions, event) && obj(state && state.sessions)) {
      event.preventDefault();
      if (sheetKind === 'sessions') closeSheet(); else openSessions();
    }
  });
  on(window, 'resize', () => { if (!layer.hidden) placeSheet(sheetOpener); });

  if (options.pane) emit('pane_state_get');

  // ---- the handle -------------------------------------------------------------------------
  return {
    element: root,
    terminalSlot,
    hostSlot,

    update(message) {
      const next = obj(message);
      if (!next || (next.t !== undefined && next.t !== 'pane_state')) return false;
      if (state && next.pane !== undefined && next.pane !== state.pane) lastSeq = -Infinity;
      const seq = typeof next.seq === 'number' ? next.seq : null;
      if (seq !== null && seq < lastSeq) return false;   // an older state, overtaken
      if (seq !== null) lastSeq = seq;
      state = next;
      trackStaged();
      draw();
      return true;
    },

    onEditText(message) {
      const m = obj(message);
      if (!m || (m.pane !== undefined && state && m.pane !== state.pane)) return;
      box.value = str(m.text) + typedAhead;
      typedAhead = '';
      editRow = str(m.row);
      selectedRow = '';
      if (state) renderRows();
      fitBox();
      renderSendState();
      box.focus();
      box.setSelectionRange(box.value.length, box.value.length);
    },

    setTheme(id) {
      if (id) root.dataset.theme = id; else delete root.dataset.theme;
    },

    get editingRow() { return editRow; },

    // Text from somewhere other than the keyboard — a voice clip the client transcribed. It lands
    // in the pane's box, which is the only one on screen while the view is up.
    appendText(text) {
      const addition = String(text || '');
      if (!addition) return;
      box.value = box.value ? `${box.value.replace(/\s*$/, '')} ${addition}` : addition;
      fitBox();
      renderSendState();
      box.focus();
      box.setSelectionRange(box.value.length, box.value.length);
    },

    destroy() {
      clearTimeout(toastTimer);
      if (resize) resize.disconnect();
      for (const off of listeners.splice(0)) off();
      root.remove();
    },
  };
}

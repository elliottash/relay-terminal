// SPDX-License-Identifier: AGPL-3.0-or-later
// The web view of one Relay pane: what the desktop pane shows under its terminal, drawn from the
// desktop's `pane_state` message (docs/REMOTE-PROTOCOL.md, "pane_state"; src/PaneState.h).
//
//   const view = mountPane(container, { send, keymap });
//   view.update(paneState);        // every pane_state for this pane
//   view.onEditText(message);      // the desktop's queue_edit_text answer
//   view.onConversationId(message); // the desktop's conversation_id_text answer
//   view.onRefused(message);       // an `error` answering one of the view's own requests
//   view.onAgentEvent(message);    // a worker event: the agent's ask (question/question_closed)
//   view.onOwnerAsks(message);     // owner_asks: knocks, guest prompts, control requests
//   view.terminalSlot              // where the host puts the terminal canvas
//   view.destroy();
//
// The layout is the Qt pane's, top to bottom: terminal, thinking bubble, queue strip, what is
// waiting for the person (the owner's decisions, then the agent's ask), prompt box with its strip
// (folder, turn clock, "% left", model, Recap, Stop), in the colours of the desktop's own theme
// (the message's `theme`, drawn by app/pane-theme.css).
//
// Everything the pane says about its own work comes from the message and is drawn as it arrived:
// the row labels, the running line, the queue hint, the reasoning header and tail, the turn clock,
// the context and allowance chips, the model names, the placeholder, the session titles. The view
// neither formats nor abbreviates any of it, and every action a row offers is one the desktop
// listed for that row.
//
// What the view does write is the words on its own controls, the parts the Qt pane has no
// equivalent of: the touch action sheet's verbs (ACTION_WORDS), the send menu (SEND_WHEN), the
// pane menu (PANE_ACTIONS), the words on the owner's three decisions (OWNER_ASKS), "New
// conversation", the QUEUE heading, the "▸ running" lead, "Stop", "Skip", the accessibility
// labels, and the "Next time" hints a keyboard gets. Those are fixed words about this view's own
// buttons — never a restatement of anything in the message — and where the desktop has a word for
// the same thing (its strip says QUEUE too, src/Pane.h) the two are kept in step by hand. It used
// to say here that the view writes none of the pane's words, which was never true of them; owner,
// 2026-09-19: published labels would still need these as a fallback, so the claim went instead
// (#0VT4).
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

// The pane's other actions, the ones the strip has no room for. The desktop reaches Recap from
// Actions › Recap and `/recap`; a phone has neither, so it gets a menu of its own.
const PANE_ACTIONS = [
  ['recap', 'Recap the conversation'],
];

// The three decisions an owner's `full` device may take from away (protocol § 10.2, 10.3, 10.4;
// owner's decision 6 on card #PH0N). The words are this view's own, as the sheet's and the queue
// heading's are: the desktop's dialogs say the same, and nothing in the message restates them.
const OWNER_ASKS = {
  knock: { yes: 'Admit', no: 'Refuse' },
  prompt: { yes: 'Run', no: 'Refuse' },
  control: { yes: 'Allow', no: 'Deny' },
};

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
const REFUSED_TOAST_MS = 20000;      // long enough to long-press the id the clipboard refused
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
  let scrolledRow = '';       // the row renderRows last scrolled to, so it scrolls once per move
  let editRow = '';           // the row whose text is in the prompt box
  let idRequest = '';         // the conversation_id ask waiting for its answer or a refusal
  let idSession = '';         // the published token that ask is about
  let idOnArrival = false;    // a tap is waiting on that answer to copy it
  // The ids the desktop has already answered for, by published token, so a tap can write one to
  // the clipboard inside its own gesture (see askConversationId).
  const conversationIds = new Map();
  let editRequest = '';       // the id of the queue_edit waiting for its text or a refusal
  // The agent's ask (sessions protocol 27): the `question` event's own id and its questions, and
  // which of them this view is showing. The desktop puts them up one at a time and each answer is
  // one line, so the view steps through them the same way — there is no event per question.
  let question = null;
  let questionAt = 0;
  // Knocks, guest prompts and control requests waiting for the owner (protocol § 10). The hub
  // sends these to `full` devices only; the latest list replaces the last, so an item that has
  // been decided — here, at the desk, or by lapsing — simply stops arriving.
  let ownerAsks = [];
  let toastTimer = 0;
  const hintLast = new Map();
  let hintLastAny = -Infinity;

  // ---- the device -------------------------------------------------------------------------
  const root = el('div', 'relay-pane');
  if (options.theme) root.dataset.theme = options.theme;
  // The theme (protocol § 16): the desktop sends the id it is drawing itself in and the pane here
  // takes it, so the terminal on the phone is the colour the terminal on the desktop is.
  //
  // Which ids exist is not listed here. app/pane-theme.css generates a block per shipped theme and
  // each one declares its own id in --rt-theme-id, so the view tries the id and reads back what the
  // stylesheet gave it: an id it has no colours for — a theme of the person's own — leaves the view
  // on the theme it is already showing rather than dropping it to the default. A sixth desktop
  // theme therefore reaches the phone by regenerating that file, and nothing here changes.
  function applyTheme(id) {
    const wanted = str(id);
    const previous = root.dataset.theme;
    if (wanted === (previous || '')) return true;
    if (wanted) root.dataset.theme = wanted; else delete root.dataset.theme;
    if (!wanted) return true;
    const applied = getComputedStyle(root).getPropertyValue('--rt-theme-id').trim().replace(/^["']|["']$/g, '');
    if (!applied || applied === wanted) return true;   // no stylesheet to ask: take the id as given
    if (previous) root.dataset.theme = previous; else delete root.dataset.theme;
    return false;
  }
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
  const allowanceChip = el('span', 'rp-chip rp-allowance');
  const modelWrap = el('span', 'rp-model-wrap');
  // Two controls, two boxes. The chevron is `position: absolute` against its box, so a single
  // wrapper around both anchored the model's chevron to the *level*'s right edge and painted it
  // over the level's own word, while the model kept an empty 22 px gutter (#EFT9).
  const modelBox = el('span', 'rp-model-box');
  const model = el('select', 'rp-chip rp-model');
  model.setAttribute('aria-label', 'Model');
  const modelChevron = el('span', 'rp-model-chevron', '▾');
  modelChevron.setAttribute('aria-hidden', 'true');
  modelBox.append(model, modelChevron);
  // The reasoning level (section 3): the model's own levels, as the desktop's effort box has
  // them. Hidden whole when the model takes none.
  const effortBox = el('span', 'rp-effort-box');
  const effort = el('select', 'rp-chip rp-effort');
  effort.setAttribute('aria-label', 'Reasoning effort');
  const effortChevron = el('span', 'rp-model-chevron rp-effort-chevron', '▾');
  effortChevron.setAttribute('aria-hidden', 'true');
  effortBox.append(effort, effortChevron);
  modelWrap.append(modelBox, effortBox);
  // Where the host puts a control of its own (the client's microphone), so its buttons sit in the
  // pane's strip instead of a second bar under it. Empty and invisible until the host fills it.
  const hostSlot = el('span', 'rp-host-slot');
  // The pane's actions with no room in the strip — Recap today (protocol § 6.4's
  // `recap_request`). The desktop reaches it from Actions › Recap; a phone has no palette.
  const moreButton = button('rp-chip rp-more', '⋯', 'Pane actions');
  moreButton.setAttribute('aria-haspopup', 'menu');
  // Stop, between the microphone and Send — the order the client's own composer has always had
  // (app/index.html: mic, Stop, Send), and the one thing the pane view had no way to do at all
  // once it was mounted (#PH0N). Drawn only while a turn is running, because there is nothing to
  // stop otherwise and a phone's strip has no room for a dead button.
  const stopButton = button('rp-stop', 'Stop', 'Stop the agent turn');
  stopButton.hidden = true;
  const sendGroup = el('span', 'rp-send-group');
  const sendButton = button('rp-send', '', 'Send');
  sendButton.append(svgIcon(ICON_SEND));
  const sendMenuButton = button('rp-send-menu', '▾', 'When to send');
  sendMenuButton.setAttribute('aria-haspopup', 'menu');
  sendGroup.append(sendButton, sendMenuButton);
  strip.append(folderChip, sessionsButton, spacer, clockChip, contextChip, allowanceChip, modelWrap,
               moreButton, hostSlot, stopButton, sendGroup);
  composer.append(line, strip);

  // ---- what is waiting for the person ------------------------------------------------------
  // Two strips above the prompt box, in the order they interrupt: the decisions only the owner can
  // take (a knock at the door, a guest's prompt, a guest asking for the keyboard), then the
  // agent's own question, which sits right on top of the box its answer is typed into.
  const asks = el('section', 'rp-asks');
  asks.setAttribute('aria-label', 'Waiting for you');
  asks.hidden = true;
  const ask = el('section', 'rp-ask');
  ask.setAttribute('aria-label', 'The agent is asking');
  ask.hidden = true;
  const askHead = el('div', 'rp-ask-head');
  const askHeader = el('span', 'rp-ask-header');
  const askStep = el('span', 'rp-ask-step');
  askHead.append(askHeader, askStep);
  const askText = el('div', 'rp-ask-text');
  const askChoices = el('div', 'rp-ask-choices');
  ask.append(askHead, askText, askChoices);

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

  root.append(termWrap, thinking, queue, asks, ask, composer, layer);
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
  function showToast(text, ms = TOAST_MS) {
    showToastNodes(ms, document.createTextNode(text));
  }

  // The same, when part of what the toast says is a thing to select rather than to read — the
  // conversation id the clipboard refused.
  function showToastNodes(ms, ...nodes) {
    toast.textContent = '';
    toast.append(...nodes);
    toast.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toast.hidden = true; }, ms);
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
    else if (action === 'edit') {
      // With an id, so the desktop's refusal can be told from any other error (§6.1: a
      // reply carries the request's id) and the keys typed on the row can be dropped.
      editRow = row.id;
      editRequest = messageId();
      emit('queue_edit', { row: row.id, id: editRequest });
    }
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
    if (selectedRow && !findRow(selectedRow)) selectedRow = '';
    // Rebuilt only when the rows actually changed, the same rule the model menu and the
    // conversations list already keep. A `pane_state` arrives up to ten times a second while a
    // turn runs: a finger down on a row when one lands had its `pointerup` on a different node,
    // so no `click` fired and the tap did nothing, and a keyboard's focus was thrown back to the
    // body ten times a second (#PKT5).
    const signature = JSON.stringify([selectedRow, list.map((row) =>
      [str(row.id), str(row.kind), str(row.state), str(row.label), actionsOf(row)])]);
    if (rows.dataset.signature === signature) return;
    rows.dataset.signature = signature;
    rows.textContent = '';
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
    // Only when the selection moved. Called on every rebuild it scrolled the list out from under
    // a reader who had scrolled it somewhere else (#PKT5).
    const current = rows.querySelector('[aria-selected="true"]');
    if (current && current.scrollIntoView && selectedRow !== scrolledRow) {
      current.scrollIntoView({ block: 'nearest' });
    }
    scrolledRow = selectedRow;
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
    delete sheet.dataset.sessions;
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

  // Copy id, in two halves. The write itself has to be the first thing in the tap: Safari drops
  // the user activation across an await, which app/app.js already records for this codebase, and
  // the id only exists after a round trip to the desktop. So the press that precedes the tap asks
  // for the id, the answer is kept, and the tap writes it synchronously. `conversation_id` is rate
  // limited at the hub (20 a minute, remote/host.py) and 16 asks may be open at once, so nothing
  // here asks for a whole sheet of ids: one press is one ask, and an id already answered for is
  // never asked for twice.
  function askConversationId(session) {
    if (!session || conversationIds.has(session)) return;
    if (idRequest && idSession === session) return;   // the press already asked
    idRequest = messageId();
    idSession = session;
    idOnArrival = false;
    emit('conversation_id', { session, id: idRequest });
  }

  function copyConversationId(session) {
    const known = conversationIds.get(session);
    // The sheet closes first, whichever way this goes: the toast is drawn over the terminal and
    // the reader has to be able to see it. Copy id was the one sheet control that left the sheet
    // up, so every outcome it had to report was painted underneath it (#CPY4).
    closeSheet();
    if (known !== undefined) { writeConversationId(known); return; }
    // Nothing kept yet — the press had no time, or this is a keyboard's Enter, which has no press.
    // Ask, and write when the answer lands; on iOS that write is outside the activation and will
    // be refused, which is what the fallback below is for.
    askConversationId(session);
    idOnArrival = true;
  }

  function writeConversationId(conversation) {
    const ok = () => showToast(`Conversation id ${conversation} copied`);
    // Refused — which on iOS is what any write outside the tap gets. The id goes into the toast
    // itself, as selectable text a long-press can take, and the toast stays up long enough for
    // that. It used to be appended to `document.querySelector('.rp-sheet')`: a document-global,
    // kind-blind query against an element `closeSheet()` leaves in the DOM, so it landed in a
    // hidden layer, or under another sheet's buttons (#CPY4).
    const refused = () => showToastNodes(REFUSED_TOAST_MS,
      document.createTextNode('Clipboard refused — long-press to copy: '),
      el('span', 'rp-session-id', conversation));
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(conversation).then(ok, refused);
    } else {
      refused();
    }
  }

  // What the conversations sheet is drawn from, minus the clock. `when` is a relative time the
  // desktop recomputes on every publish and which steps every minute, so a signature over the
  // whole `sessions` block rebuilt the sheet — scroll back to the top, focus back on "New
  // conversation" — once a minute while a turn ran (#CPY4). The clock is patched in instead.
  function sessionsShape(sessions) {
    if (!sessions) return 'null';
    return JSON.stringify([sessions.can_new === true, sessions.can_open === true,
      arr(sessions.rows).filter(obj).map((s) => [str(s.id), str(s.title), s.current === true])]);
  }

  // The clock and the running dot, moved without rebuilding anything: each row keeps its node, so
  // the reader's scroll, the focus and any selection inside it survive.
  function patchSessions(sessions) {
    for (const session of arr(sessions && sessions.rows).filter(obj)) {
      const node = sheet.querySelector(`.rp-session-row[data-session-id="${CSS.escape(str(session.id))}"]`);
      if (!node) continue;
      const when = node.querySelector('.rp-session-when');
      if (when && when.textContent !== str(session.when)) when.textContent = str(session.when);
      const dot = node.querySelector('.rp-session-dot');
      if (dot) dot.classList.toggle('rp-running', session.running === true);
    }
  }

  function openSessions() {
    const sessions = obj(state && state.sessions);
    if (!sessions) return;
    openSheet('sessions', '', (node) => {
      node.setAttribute('aria-label', 'Conversations');
      node.dataset.sessions = sessionsShape(sessions);   // what draw() compares against
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
        // The conversation id never shows on its own (section 16 publishes tokens), so the row
        // carries the ask: the desktop answers this device alone, and the id goes to the
        // clipboard — shown in the toast only where the clipboard refused it.
        const copy = button('rp-session-copy', 'Copy id', `Copy the conversation id of ${str(session.title)}`);
        // The press asks; the tap writes. A finger is down for long enough that the desktop's
        // answer is usually back before the tap, and that is the whole point: the write is then
        // inside the tap's own user activation, which is the only kind iOS honours.
        copy.addEventListener('pointerdown', () => askConversationId(str(session.id)));
        copy.addEventListener('click', (event) => {
          event.stopPropagation();
          copyConversationId(str(session.id));
        });
        item.appendChild(copy);
        list.appendChild(item);
      }
      node.appendChild(list);
    }, sessionsButton);
  }

  // ---- the prompt box ---------------------------------------------------------------------
  const busy = () => !!(obj(state && state.turn) && state.turn.busy === true);

  // One prompt on its way to the desktop. `route` is what the protocol's `agent` field asks for:
  // by default the line is routed the way the desktop's own composer would route it, and
  // `route: false` keeps it agent-bound — which is what an answer to the agent's ask is, whatever
  // its words happen to look like (sessions protocol 27.3).
  function sendCompose(text, when, { route = true } = {}) {
    // `agent: false` asks the desktop to route the line the way its own prompt box would — a
    // command runs in the shell, anything else goes to the agent. Only a device the desktop
    // trusts with typing may ask for that, and the state says so: a full device is offered the
    // composer's own modes, an agent device only "agent". Without this a typed command reached
    // the agent, which ran it as a tool call on the owner's key (found by #W5N2's end-to-end QA).
    const modes = arr(state && state.composer && state.composer.modes);
    const mayRoute = route && (modes.includes('auto') || modes.includes('shell'));
    emit('compose', {
      text, when,
      ...(mayRoute ? { agent: false } : {}),
      // The same dedup id the client's older composer sends, so a retried send is not two prompts.
      msg_id: messageId(),
    });
  }

  // Sent is sent: on a touch screen the on-screen keyboard comes down, so the terminal the answer
  // is arriving in is the whole screen again (a phone's request, 2026-09-22). On a device with a
  // physical keyboard the box keeps the focus. It used to say here that a physical keyboard's
  // blur is invisible, so the blur could be unconditional; it is not invisible to this view.
  // `enter()` is reachable only from the box's own keydown listener, so a blurred box takes the
  // three-step Enter escalation, `queue_resume` on an empty box (#7JD1) and ArrowUp row selection
  // with it — every one of them needs the focus the blur removes (#KBD7).
  function keyboardDown() {
    if (root.dataset.input === 'touch') box.blur();
  }

  function compose(text, when) {
    if (!text.trim()) return;
    if (askList()[questionAt]) {
      // The agent is asking, and the prompt box is where its answer is typed (sessions protocol
      // 27.4) — so while a question is drawn above the box, the line is sent agent-bound and the
      // desktop's ask takes it. Routed first, a `full` device's line could have gone to the shell
      // with the ask left standing and this view stepping past it (27.3); the shell is one Skip
      // away, which is a smaller cost than a wrong answer on the phone's screen.
      answerAsk(text, when);
      editRow = '';
      // Emptied like an ordinary send: the answer to question 1 used to stay in the box, blurred,
      // with Send enabled, so the next tap sent it again as the answer to question 2 (#KBD7).
      box.value = '';
      fitBox();
      renderSendState();
      keyboardDown();
      return;
    }
    sendCompose(text, when);
    editRow = '';
    staged = when === 'queue'
      ? { text, stage: 1, at: clock(), rowId: '', steerId: '', known: new Set(rowList().map((r) => r.id)) }
      : null;
    box.value = '';
    fitBox();
    renderSendState();
    keyboardDown();
  }

  function enter() {
    const text = box.value;
    if (text.trim()) {
      compose(text, busy() ? 'queue' : 'now');
      return;
    }
    // Empty box, and a queue a Stop paused: resume it (#7JD1; owner, 2026-09-21: "why don't we
    // just copy the functionality and have enter resume"). The same key and the same box as the
    // desktop's, and the Resume button that was the only way back is the desktop's alone. It is
    // tried before the steer escalation below because that one needs a turn to steer into, and
    // after a Stop there is none.
    const q = obj(state.queue);
    if (q && q.paused === true) { staged = null; emit('queue_resume'); return; }
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

  // ---- Stop, Recap, and what is waiting for the person --------------------------------------

  // Whether this device may act on the pane at all. The hub empties `composer.modes` for a `view`
  // device (protocol § 16, `for_capability`), and `view` is below the `agent` floor that
  // `agent_stop` and `recap_request` need — so the one flag answers for both buttons.
  const mayAct = () => arr(state && state.composer && state.composer.modes).length > 0;
  // The owner's own level, as `pane_state` itself draws the line: the whole `sessions` block is
  // dropped below `full`. The hub sends the owner's decisions to `full` devices only, so this is
  // defence in depth rather than the gate — a row that arrived anyway is read, not pressed.
  const ownerLevel = () => !!obj(state && state.sessions);

  function renderStop() {
    // Only while a turn is running: `turn.busy` is the one fact about it the state carries, and
    // there is nothing to stop otherwise.
    stopButton.hidden = !mayAct() || !busy();
    moreButton.hidden = !mayAct();
  }

  function openPaneMenu() {
    openSheet('pane', '', (node) => {
      node.setAttribute('aria-label', 'Pane actions');
      for (const [action, words] of PANE_ACTIONS) {
        const item = sheetButton('', words, () => {
          if (action === 'recap') emit('recap_request');
        });
        item.dataset.action = action;
        node.appendChild(item);
      }
    }, moreButton);
  }

  // ---- the agent's ask (sessions protocol 27) ------------------------------------------------

  const askList = () => arr(question && question.questions).filter(obj);

  function stepAsk() {
    questionAt += 1;
    renderAsk();
  }

  function answerAsk(text, when = busy() ? 'queue' : 'now') {
    // There is no `question_answer` a client may send (protocol § 10.1, sessions protocol 27.3):
    // the answer is an ordinary prompt from this device, and the desktop's ask takes it. It is
    // sent **agent-bound**: a choice the model happened to label "git status" is an answer to the
    // question, not a command, and a routed one would run in the shell with the ask left standing
    // (`Pane::submitRemote` hands an unrouted line to the ask before anything else).
    sendCompose(text, when, { route: false });
    stepAsk();
  }

  function renderAsk() {
    const list = askList();
    const item = questionAt < list.length ? list[questionAt] : null;
    ask.hidden = !item;
    if (!item) { askChoices.textContent = ''; ask.dataset.signature = ''; return; }
    // Rebuilt only when the question changed, as the queue rows above are. Unguarded, the options
    // were replaced ten times a second while the turn ran, so a finger down on one lifted onto a
    // node that was no longer in the document and nothing happened (#PKT5).
    const signature = JSON.stringify([str(question && question.id), questionAt, mayAct(),
      str(item.header), str(item.question), list.length,
      arr(item.options).filter(obj).map((o) => [str(o.label), str(o.description),
                                                o.recommended === true])]);
    if (ask.dataset.signature === signature) return;
    ask.dataset.signature = signature;
    askChoices.textContent = '';
    // The worker's own words for the decision and the question (protocol 27.2), drawn as they
    // arrived. The step counter is this view's, because the desktop asks them one at a time and
    // a phone showing question 2 of 3 with no count would look like the whole ask.
    askHeader.textContent = str(item.header);
    askStep.textContent = list.length > 1 ? `${questionAt + 1} of ${list.length}` : '';
    askStep.hidden = list.length <= 1;
    askText.textContent = str(item.question);
    if (!mayAct()) return;     // a `view` device reads the question; it does not answer it
    for (const option of arr(item.options).filter(obj)) {
      const label = str(option.label);
      if (!label) continue;
      const choice = button('rp-ask-choice', label);
      if (option.recommended === true) choice.classList.add('rp-recommended');
      if (str(option.description)) choice.title = str(option.description);
      choice.addEventListener('click', () => answerAsk(label));
      askChoices.appendChild(choice);
    }
    // The desk's two ways past a question (27.4): "/skip" skips this one, and anything else typed
    // is the answer. The prompt box is directly below, so only the skip needs a button here.
    const skip = button('rp-ask-skip', 'Skip');
    skip.addEventListener('click', () => answerAsk('/skip'));
    askChoices.appendChild(skip);
  }

  // ---- the owner's own decisions (protocol § 10.2, 10.3, 10.4) -------------------------------

  // What each waiting item says. The fields are the hub's (a name, a role, the five-digit code, a
  // guest's prompt); the sentence around them is this view's own, like the sheet's verbs.
  function ownerAskLine(item) {
    const name = str(item.name) || 'someone';
    if (item.kind === 'knock') {
      const role = str(item.role);
      const code = str(item.code);
      return `${name} wants to join${role ? ` as ${role}` : ''}${code ? ` · code ${code}` : ''}`;
    }
    if (item.kind === 'prompt') return `${name}: ${str(item.text)}`;
    return `${name} asks to type`;
  }

  // The answer, in the desktop's own words for it (protocol § 10.5): the same three messages the
  // sharing dialog sends, which is what makes a phone's Admit and a desktop's Admit one thing.
  function answerOwnerAsk(item, yes) {
    if (item.kind === 'knock') {
      send({ t: 'knock_answer', participant: str(item.id), admit: yes,
             role: str(item.role) || 'viewer' });
    } else if (item.kind === 'prompt') {
      send({ t: 'prompt_answer', id: str(item.id), approve: yes });
    } else {
      send({ t: 'control_answer', pane: str(item.pane), participant: str(item.id), grant: yes });
    }
    // The hub sends the list again once it has applied the answer; until then the row goes,
    // because a button that stays pressable is a second admit waiting to happen.
    ownerAsks = ownerAsks.filter((other) => other !== item);
    renderOwnerAsks();
  }

  function renderOwnerAsks() {
    const pane = paneId();
    // A knock is about joining this desktop, not this pane, so it is shown wherever you are; a
    // guest's prompt and a request for the keyboard name one pane and belong to that pane.
    const items = ownerAsks.filter((item) => item.kind === 'knock' || !item.pane || item.pane === pane);
    asks.textContent = '';
    asks.hidden = items.length === 0;
    for (const item of items) {
      const row = el('div', 'rp-ask-row');
      row.dataset.kind = str(item.kind);
      row.dataset.askId = str(item.id);
      row.append(el('span', 'rp-ask-row-text', ownerAskLine(item)));
      if (!ownerLevel()) { asks.appendChild(row); continue; }
      const words = OWNER_ASKS[item.kind] || OWNER_ASKS.knock;
      const buttons = el('span', 'rp-ask-row-buttons');
      const yes = button('rp-ask-yes', words.yes);
      yes.addEventListener('click', () => answerOwnerAsk(item, true));
      const no = button('rp-ask-no rp-danger', words.no);
      no.addEventListener('click', () => answerOwnerAsk(item, false));
      buttons.append(yes, no);
      row.appendChild(buttons);
      asks.appendChild(row);
    }
  }

  // ---- drawing ----------------------------------------------------------------------------
  function show(node, text) {
    node.textContent = text;
    node.hidden = !text;
  }

  // The same, for a chip. A chip is a flex box (its dot or icon beside its words), and a flex box
  // never ellipsizes text of its own: the words go in as an anonymous item that `text-overflow`
  // does not reach, so a clock or a folder name too long for the chip was cut straight through a
  // glyph. The words go in a child instead — .rp-chip-text, `min-width: 0`, which is a box that
  // can shrink and can end in an ellipsis. `textContent` still reads exactly the desktop's label.
  function showChip(chip, text) {
    let label = chip.querySelector(':scope > .rp-chip-text');
    if (!label) { label = el('span', 'rp-chip-text'); chip.append(label); }
    label.textContent = text;
    chip.hidden = !text;
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
    if (!q) { rows.textContent = ''; rows.dataset.signature = ''; return; }
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
    // A `view` device is sent a composer whose `modes` is empty, not no composer at all
    // (remote/pane_state.py `for_capability`, protocol § 16's table). It may not compose, so it
    // gets no prompt box and no send button — rather than a box whose every send comes back
    // `not_permitted`. `src/PaneState.cpp` always fills all three modes, so `[]` can only mean
    // "this device is not allowed to type".
    const c = arr(state.composer && state.composer.modes).length ? obj(state.composer) : null;
    composer.hidden = !c;
    if (c) {
      const placeholder = str(c.placeholder);
      if (box.placeholder !== placeholder) { box.placeholder = placeholder; fitBox(); }
      const mode = str(c.mode);
      showChip(modeChip, mode);
      composer.dataset.mode = mode;
      modeChip.dataset.dest = mode;
    }
    const folder = obj(state.folder);
    showChip(folderChip, folder ? str(folder.label) : '');
    const turn = obj(state.turn);
    showChip(clockChip, turn ? str(turn.clock) : '');
    root.dataset.phase = turn ? str(turn.phase) : '';
    const context = obj(state.context);
    showChip(contextChip, context ? str(context.label) : '');
    const left = context && typeof context.percent_left === 'number' ? context.percent_left : null;
    contextChip.dataset.warn = left !== null && left <= 15 ? 'true' : 'false';
    // The Relay Free allowance: the desktop's words, its detail as the title, its warn flag the
    // same style the context chip warns by. Absent on the pane state: the chip hides (the pane
    // moved to a provider with a key).
    const allowance = obj(state.allowance);
    showChip(allowanceChip, allowance ? str(allowance.label) : '');
    allowanceChip.dataset.warn = allowance && allowance.warn === true ? 'true' : 'false';
    allowanceChip.title = allowance ? str(allowance.detail) : '';
    renderModel();
    const sessions = obj(state.sessions);
    sessionsButton.hidden = !sessions;
    const current = sessions && arr(sessions.rows).filter(obj).find((s) => s.current === true);
    sessionsTitle.textContent = current ? str(current.title) : '';
    renderSendState();
    // The strip is the prompt box's bottom row; with no prompt box (a view-only device) it is
    // still how the model, the clock and the sessions are seen, so it stands on its own.
    if (!c) root.classList.add('rp-no-composer'); else root.classList.remove('rp-no-composer');
    // Send belongs to the prompt box: a `view` device may not compose at all, and the strip it
    // keeps must not offer it a send button anyway.
    sendGroup.hidden = !c;
    if (!c && strip.parentNode === composer) root.insertBefore(strip, layer);
    if (c && strip.parentNode !== composer) composer.appendChild(strip);
  }

  function renderModel() {
    const m = obj(state.model);
    modelWrap.hidden = !m;
    // Before the guard below, and outside it: the model's signature carries the label and the
    // choices, and the level is in neither — so a `pane_state` in which only the level moved
    // returned here and the chip kept the level the pane had left (#EFT9).
    renderEffort(m);
    if (!m) return;
    const choices = arr(m.choices).filter((choice) => obj(choice) && str(choice.id));
    // Rebuilt only when the menu actually changed. A `pane_state` arrives up to ten times a second
    // while a turn runs (the clock alone changes every second), and replacing the `<option>`s
    // closes the native picker a phone has open over them — so the one thing a partner is promised,
    // switching the model, only worked while the pane was idle.
    const signature = JSON.stringify([str(m.label),
      choices.map((choice) => [str(choice.id), str(choice.label), choice.current === true])]);
    if (model.dataset.signature === signature) return;
    model.dataset.signature = signature;
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

  function renderEffort(m) {
    const levels = m ? arr(m.efforts).filter((level) => typeof level === 'string' && level) : [];
    // Both, so the chevron goes with the select: a box left standing around a hidden select is a
    // lone ▾ in the strip.
    effort.hidden = levels.length === 0;
    effortBox.hidden = levels.length === 0;
    if (!levels.length) return;
    // A model whose level the pane will not change draws a chip that cannot be picked from, the
    // way the desktop's own effort box is greyed (src/Pane.h, `effortFixed`). The field is the
    // desktop's `effort_fixed`; a state without it leaves the chip live, as before.
    const fixed = m.effort_fixed === true;
    // Same rule as the model menu above: rebuilt only when the levels moved, so an open native
    // picker survives the ten-a-second pane_state.
    const current = typeof m.effort === 'string' ? m.effort : '';
    const signature = JSON.stringify([current, levels, fixed]);
    if (effort.dataset.signature === signature) return;
    effort.dataset.signature = signature;
    effort.textContent = '';
    // The model menu's shape: a disabled placeholder carrying the plain current level, so the
    // closed control reads `high` and the `✓` that marks the current option stays in the list
    // where it means something. Before this the closed chip read `✓▾high` (#EFT9).
    const shown = el('option', '', current || levels[0]);
    shown.value = '';
    shown.disabled = true;
    shown.selected = true;
    effort.appendChild(shown);
    for (const level of levels) {
      const option = el('option', '', `${level === current ? '✓ ' : ''}${level}`);
      option.value = level;
      effort.appendChild(option);
    }
    effort.selectedIndex = 0;
    effort.disabled = fixed;
    effortBox.dataset.pickable = fixed ? 'false' : 'true';
  }

  function draw() {
    // Before the rest: the pane is drawn in the colours the desktop is using. A state without the
    // field — an older desktop — leaves the theme alone.
    if (state.theme !== undefined) applyTheme(state.theme);
    renderThinking();
    renderQueue();
    renderStrip();
    renderStop();
    renderAsk();
    renderOwnerAsks();
    // Rebuilt only when the rows actually changed. A `pane_state` arrives up to ten times a second
    // while a turn runs, and `openSheet()` clears the sheet and focuses its first button — so an
    // open conversations list threw the focus back to the top and scrolled itself there, ten times
    // a second, exactly while the agent was working.
    if (sheetKind === 'sessions') {
      const sessions = obj(state.sessions);
      const shape = sessionsShape(sessions);
      if (sheet.dataset.sessions !== shape) openSessions();
      else patchSessions(sessions);
    }
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
    // The button took the focus, and on a laptop the box needs it back — Enter's escalation,
    // `queue_resume` and ArrowUp are all on the box's own keydown. On a touch screen it must not
    // have it back: a tap on Send is the gesture a phone actually uses, and this line put the
    // on-screen keyboard straight up again (#KBD7).
    if (root.dataset.input !== 'touch') box.focus();
  });
  on(sendMenuButton, 'click', openSendMenu);
  on(stopButton, 'click', () => {
    // The turn's Stop (protocol § 6.4). The desktop answers with `agent_stopped`, which takes
    // `turn.busy` down and the button with it; nothing here guesses at the new state.
    emit('agent_stop');
  });
  on(moreButton, 'click', openPaneMenu);
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
  on(effort, 'change', () => {
    const level = effort.value;
    // Back to the placeholder, as the model menu does: the closed chip says the level the *pane*
    // is on, and only the pane's own `pane_state` moves it.
    effort.selectedIndex = 0;
    if (level) emit('effort_pick', { effort: level });
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

    // The client's Back closes the deepest open layer before it closes the pane under it
    // (app/app.js, `closeOneLayer`), so it has to know whether there was a sheet to close. The
    // internal `closeSheet` answers with its side effect only; this says whether it did anything.
    closeSheet() {
      if (layer.hidden) return false;
      closeSheet();
      return true;
    },

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
      editRequest = '';
      editRow = str(m.row);
      selectedRow = '';
      if (state) renderRows();
      fitBox();
      renderSendState();
      box.focus();
      box.setSelectionRange(box.value.length, box.value.length);
    },

    // The desktop's `conversation_id_text` answer (section 16): the real conversation id behind a
    // token this view asked about. It is kept, so the next tap on that row can write it to the
    // clipboard inside its own gesture, and written now if a tap is already waiting on it — in
    // the toast, as text a long-press can copy, where the clipboard refuses. Returns true when
    // the answer was this view's ask, so the host does not also report it.
    onConversationId(message) {
      const m = obj(message);
      if (!m || !idRequest || str(m.id) !== idRequest) return false;
      const session = idSession;
      const wanted = idOnArrival;
      idRequest = '';
      idSession = '';
      idOnArrival = false;
      const conversation = str(m.conversation);
      if (!conversation) return true;
      // Kept, so the next tap on that row writes it inside its own gesture.
      if (session) conversationIds.set(session, conversation);
      if (wanted) writeConversationId(conversation);
      return true;
    },

    // An `error` answering something this view asked for. The one that matters is a refused
    // `queue_edit`: the row is not coming back, so the keys typed on it must not sit in
    // `typedAhead` waiting to be pushed in front of whatever comes back next — a letter typed on a
    // row the desktop would not give up landed in front of a later row's text. Returns true when
    // the refusal was this view's, so the host does not also say it somewhere else.
    //
    // It is said the way the view says everything: the toast over the terminal, which is where a
    // phone reads it — the client's note lives under a transcript that is hidden while a pane has
    // a screen (app/app.js, threadNote).
    onRefused(message) {
      const m = obj(message);
      if (m && idRequest && str(m.id) === idRequest) {
        idRequest = '';
        idSession = '';
        idOnArrival = false;
        showToast(`The id was refused: ${str(m.message) || 'not permitted'}`);
        return true;
      }
      if (!m || !editRequest || str(m.id) !== editRequest) return false;
      editRequest = '';
      editRow = '';
      typedAhead = '';
      if (state) renderRows();
      renderSendState();
      showToast(str(m.message) || 'That row could not be taken back.');
      return true;
    },

    // A worker event for this pane, as the host received it (`{t:"agent", pane, event}`) or the
    // event on its own. The only ones the view draws are the agent's ask and its closing
    // (sessions protocol 27): everything else the agent says is already on the screen above,
    // because Relay prints it into the pane's terminal. Returns true when it was one of those.
    onAgentEvent(message) {
      const outer = obj(message);
      if (!outer) return false;
      const event = obj(outer.event) || outer;
      const kind = str(event.event);
      if (kind === 'question') {
        question = { id: str(event.id), questions: arr(event.questions).filter(obj) };
        questionAt = 0;
      } else if (kind === 'question_closed') {
        // Stop, or the end of the turn, took the ask away. An id for an ask this view is not
        // showing is somebody else's, or one it has already dropped.
        if (question && str(event.id) && str(event.id) !== question.id) return false;
        question = null;
        questionAt = 0;
      } else if (['agent_finished', 'agent_stopped', 'cancelled', 'error'].includes(kind)) {
        // The turn ended: the worker's `question_closed` says the same, but not for a view that
        // missed it, and a question with no turn behind it has nobody to answer.
        if (!question) return false;
        question = null;
        questionAt = 0;
      } else {
        return false;
      }
      renderAsk();
      return true;
    },

    // `owner_asks {items}`: the knocks, guest prompts and control requests waiting for a decision
    // (protocol § 10). The hub sends the whole list to a `full` device every time it changes, so
    // the latest one replaces the last and an item that was decided anywhere simply stops
    // arriving. Returns true when the message was one.
    onOwnerAsks(message) {
      const m = obj(message);
      if (!m || (m.t !== undefined && m.t !== 'owner_asks')) return false;
      ownerAsks = arr(m.items).filter(obj).filter((item) => OWNER_ASKS[str(item.kind)]);
      renderOwnerAsks();
      return true;
    },

    // A host that knows better than the desktop does (the demo page's picker). Returns false for
    // an id this view has no colours for, having left the theme as it was.
    setTheme(id) {
      return applyTheme(id);
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

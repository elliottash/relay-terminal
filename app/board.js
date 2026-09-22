// SPDX-License-Identifier: AGPL-3.0-or-later
// The Board on the phone (card #SWPH): cards by stage, a card's body and thread, and the
// card actions, by touch.
//
// One model, two views (#0VT4): the desktop's own `BoardWorker` is the only thing that reads or
// writes a card, and this file draws the events it emits and sends back the requests the desktop
// pane would have sent. The contract is on the card:
//
//   phone → hub   {t: "board_request", rid, request: {type, …}}      `full` devices only
//   hub → phone   {t: "board_event", rid, event: {event, …}}         sanitised: no path ever
//
// and `request.type` is one of `board_open`, `board_refresh`, `board_card_get {id}`,
// `board_search {query}`, `board_comment {id, text, kind}`, `board_move {id, status, reason}`,
// `board_create {tab, title, request, labels}`, `board_ask {id, text, mode}`, `board_cancel {id}`,
// `board_resume {id}` and `board_action {id, action}`. Nothing else is ever sent from here: no
// delete, no folders, no cleanup, nothing that names a file.
//
// Everything a card says was written by a model, a collaborator or a merged branch, so every
// string goes in through textContent and Markdown is drawn by app/boardmd.js, which builds nodes
// and never parses HTML. There is no innerHTML in this file.
//
// Three writes may wait for the link (comment, move, create): they go through app/outbox.js with
// a `msg_id`, so a comment typed on a bus lands once when the phone is back. A Discuss, a Plan,
// Execute and Verify start work on the desktop *now*, so offline they are refused in a line and
// the words stay in the box.

import { renderMarkdown, renderLines, bodySections, numberedOptions, OPENABLE } from './boardmd.js';
// A comment's `model=` attribute is the id the turn ran on; a card page names the model
// (card #MDL1, rule 1).
import { nameOf } from './modelname.js';

// ---- the desktop's own words and order (src/BoardModel.cpp) -------------------------------------

const STATUS_TITLES = {
  inbox: 'Inbox', discussing: 'Discussing', planning: 'Planning', planned: 'Planned',
  ready: 'Ready to start', 'in-progress': 'In progress', executing: 'Executing',
  'needs-verification': 'Needs verification', 'needs-review': 'Needs review',
  'needs-labels': 'Needs labels', 'needs-ab': 'Needs A/B', 'needs-qa': 'Needs QA',
  'needs-qa-llm': 'Needs QA (LLM)', 'needs-qa-human': 'Needs QA (human)', deferred: 'Deferred',
  done: 'Done', dropped: 'Dropped', waiting: 'Waiting', draft: 'Draft', approved: 'Approved',
  active: 'Active', retired: 'Retired', verified: 'Verified',
};
// Only the part a multi-status section's header does not already say ("Needs QA" → "LLM QA").
const STATUS_SHORT = {
  'needs-qa-llm': 'LLM QA', 'needs-qa-human': 'human QA', 'needs-review': 'review',
  'needs-labels': 'labels', 'needs-ab': 'A/B', dropped: 'dropped',
};
const DEFAULT_COLUMNS = ['inbox', 'discussing', 'planning', 'planned', 'executing',
  'needs-verification', 'needs-qa', 'done'];
const DEFAULT_COLUMN_STATUSES = {
  waiting: ['needs-review', 'needs-labels', 'needs-ab'],
  'needs-qa': ['needs-qa-llm', 'needs-qa-human'],
  done: ['done', 'dropped'],
};
const EXTRA_RANK = { planning: 1, draft: 2, approved: 3, executing: 4, active: 5, planned: 6,
  'needs-verification': 7, deferred: 8, retired: 9 };
const WORK_STATUSES = ['inbox', 'discussing', 'planning', 'planned', 'ready', 'executing',
  'in-progress', 'needs-verification', 'needs-review', 'needs-labels', 'needs-ab', 'needs-qa-llm',
  'needs-qa-human', 'deferred', 'done', 'dropped'];

const WAITING = 'waiting-on-you';      // the pinned group; not a section the desktop has
const VERIFIED = 'verified';
const DONE = 'done';
const ALL_TAB = '';

// The three writes that may wait for the link, and what each is called in a line.
const WRITE_WORDS = { board_comment: 'Comment', board_move: 'Move', board_create: 'New card' };
const QUEUED_WORDS = 'sends when back online';

// The hub's caps (docs/REMOTE-PROTOCOL.md 17.1). Text that is too long is refused there, not cut —
// a comment that silently lost its last paragraph is worse than one that stops taking letters —
// so each field stops at its cap here.
const TEXT_MAX = 8000;
const TITLE_MAX = 200;
const REASON_MAX = 500;
const QUERY_MAX = 200;
const LABELS_MAX = 16;
const LABEL_OK = /^[A-Za-z0-9][A-Za-z0-9 _.:+-]{0,39}$/;

const VOICE_MAX_MS = 60000;
const VOICE_MAX_BYTES = 700000;        // MAX_VOICE_BYTES in remote/host.py
const VOICE_CONTAINERS = {
  'audio/webm': 'webm', 'audio/ogg': 'ogg', 'audio/mp4': 'm4a', 'audio/aac': 'm4a',
  'audio/x-m4a': 'm4a', 'audio/mpeg': 'mp3', 'audio/wav': 'wav', 'audio/wave': 'wav',
  'audio/x-wav': 'wav',
};

export function statusTitle(status) {
  const known = STATUS_TITLES[status];
  if (known) return known;
  const text = String(status || '').replace(/-/g, ' ');
  return text ? text[0].toUpperCase() + text.slice(1) : '';
}

const str = (value) => (typeof value === 'string' ? value : value == null ? '' : String(value));
const closed = (row) => row.status === 'done' || row.status === 'dropped';
const selfClosed = (row) => row.status === 'done' && str(row.verified_by).trim() !== ''
  && str(row.verified_by).trim() === str(row.implemented_by).trim();
export const waitingOnOwner = (row) => str(row.waiting_on).trim().toLowerCase() === 'owner' && !closed(row);

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function button(className, text, label) {
  const node = el('button', className, text);
  node.type = 'button';
  if (label) node.setAttribute('aria-label', label);
  return node;
}

// ---- sections: the desktop's `Model::sections()` and `sectionForCard()` -------------------------

// The ordered sections of this board: the configured columns with what each collects, then a
// section for any status a card carries that none of them collects, then Verified and Done,
// which are always last.
export function sectionsOf(config, rows) {
  const columns = Array.isArray(config?.columns) && config.columns.length ? config.columns.map(str)
    : DEFAULT_COLUMNS;
  const configured = config?.column_statuses && typeof config.column_statuses === 'object'
    ? config.column_statuses : {};
  const titles = config?.column_titles && typeof config.column_titles === 'object'
    ? config.column_titles : {};
  const titleOf = (id) => str(titles[id]).trim() || statusTitle(id);
  const out = [];
  const collected = new Set();
  for (const id of columns) {
    const listed = Array.isArray(configured[id]) ? configured[id].map(str) : null;
    const manual = !!listed && listed.length === 0;
    const statuses = (listed || DEFAULT_COLUMN_STATUSES[id] || [id])
      .filter((status) => status !== 'done' && status !== 'dropped');
    if (!statuses.length && !manual) continue;       // a configured Done column: it is last
    statuses.forEach((status) => collected.add(status));
    out.push({ id, title: titleOf(id), statuses, manual });
  }
  const extras = [];
  for (const row of rows) {
    if (closed(row) || !row.status || collected.has(row.status) || extras.includes(row.status)) continue;
    extras.push(row.status);
  }
  extras.sort((a, b) => ((EXTRA_RANK[a] ?? 5) - (EXTRA_RANK[b] ?? 5)) || (a < b ? -1 : a > b ? 1 : 0));
  for (const status of extras) out.push({ id: status, title: titleOf(status), statuses: [status], manual: false });
  out.push({ id: VERIFIED, title: titleOf(VERIFIED), statuses: [], manual: false });
  out.push({ id: DONE, title: titleOf(DONE), statuses: ['done', 'dropped'], manual: false });
  return out;
}

export function sectionOf(row, sections) {
  if (row.status === 'done' && str(row.verified_by).trim() && !selfClosed(row)) return VERIFIED;
  if (closed(row)) return DONE;
  const parked = str(row.section).trim();
  if (parked && sections.some((section) => section.id === parked)) return parked;
  const home = sections.find((section) => section.statuses.includes(row.status));
  return home ? home.id : '';
}

function sortRows(rows, closedSection) {
  return rows.slice().sort((a, b) => {
    if (closedSection && str(a.created) !== str(b.created)) return str(a.created) > str(b.created) ? -1 : 1;
    if (str(a.rank) !== str(b.rank)) return str(a.rank) < str(b.rank) ? -1 : 1;
    return str(a.id) < str(b.id) ? -1 : str(a.id) > str(b.id) ? 1 : 0;
  });
}

// ---- time ---------------------------------------------------------------------------------------

// `20260917T141203Z-a1` → a Date, or null.
function entryTime(entryId) {
  const match = str(entryId).match(/^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z/);
  if (!match) return null;
  const [, y, mo, d, h, mi, s] = match.map(Number);
  const date = new Date(Date.UTC(y, mo - 1, d, h, mi, s));
  return Number.isNaN(date.getTime()) ? null : date;
}

function timeText(date, now = Date.now()) {
  if (!date) return '';
  const seconds = Math.floor((now - date.getTime()) / 1000);
  if (seconds < 60) return 'just now';
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)} h ago`;
  if (seconds < 7 * 86400) return `${Math.floor(seconds / 86400)} d ago`;
  return date.toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
}

const clockText = (millis) => new Date(millis).toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });

// ---- the view -----------------------------------------------------------------------------------

export function mountBoard(options) {
  const {
    screen, rowSlot, outbox,
    online = () => true,
    show = () => {},
    openPane = () => {},
    panes = () => [],
    onCount = () => {},
    sendRaw = () => Promise.reject(new Error('not connected.')),
  } = options;
  const clock = typeof options.now === 'function' ? options.now : () => Date.now();

  // ---- state ------------------------------------------------------------------------------
  let enabled = false;          // the desktop offers `board` and this device is `full`
  let voiceOffered = false;     // the desktop offers `voice`
  let seq = 0;                  // request ids
  const asked = new Map();      // rid -> {type, card, text, mode, status, queued}
  let config = null;
  let exists = true;            // the project has a board at all
  let project = '';
  const rows = new Map();       // card id -> row
  let rev = 0;
  let loaded = false;           // a `board` has arrived since this page was opened
  let loadedAt = 0;
  let openAsked = false;        // a `board_open` is in flight
  let visible = false;
  let tab = ALL_TAB;
  let query = '';
  let searchFor = '';           // the words the last `board_search` answer was for
  let searchIds = null;
  let searchTimer = 0;
  const collapsed = loadCollapsed();
  const foldOpen = new Set();   // sections whose "closed by the agent" fold is open
  const busy = new Map();       // card id -> mode, while a card turn runs
  const busySince = new Map();  // card id -> when this view learned of it
  let openId = '';              // the card on the card page
  let card = null;              // its last `board_card`
  let wantCard = '';            // a card a notification asked for, before the board was usable
  let cardAsked = 0;            // the rid of the `board_card_get` in flight
  const drafts = new Map();     // card id -> what was typed and not sent
  const queuedEntries = new Map();  // rid -> {card, text} : comments waiting for the link
  let sheet = null;
  const lineTimers = new WeakMap();
  let landedOn = '';            // the card whose page has been drawn with its body once
  let offlineLine = false;      // the card line is saying "offline", and a card arriving answers it
  let recorder = null;
  let voiceId = '';
  let voiceTarget = null;

  // ---- the inbox row ----------------------------------------------------------------------
  // Drawn like a pane's row and deliberately not one: `.pane-row` is the list of the desktop's
  // panes, which the app and its tests count and index, and the Board is not a pane.
  const inboxRow = button('rb-inbox-row');
  inboxRow.id = 'board-row';
  inboxRow.hidden = true;
  const inboxHead = el('div', 'rb-inbox-head');
  const inboxTitle = el('span', 'rb-inbox-title', 'Board');
  const inboxChip = el('span', 'chip warn rb-inbox-chip');
  inboxChip.hidden = true;
  inboxHead.append(inboxTitle, inboxChip);
  const inboxSub = el('div', 'rb-inbox-sub');
  inboxRow.append(inboxHead, inboxSub);
  inboxRow.addEventListener('click', () => open());
  rowSlot.append(inboxRow);

  // ---- the screen -------------------------------------------------------------------------
  const root = el('div', 'relay-pane rb');
  root.dataset.input = 'touch';           // this app is the touch client: 44 px targets throughout
  root.dataset.card = 'closed';

  const bar = el('div', 'rb-bar');
  const back = button('rb-back', '‹', 'Back to the inbox');
  const barTitle = el('div', 'rb-bar-title');
  const barName = el('div', 'rb-bar-name', 'Board');
  const barSub = el('div', 'rb-bar-sub');
  barTitle.append(barName, barSub);
  const refresh = button('rb-bar-button rb-refresh', '↻', 'Refresh the board');
  const add = button('rb-bar-button rb-add', '+', 'New card');
  bar.append(back, barTitle, refresh, add);

  const body = el('div', 'rb-body');

  const listCol = el('div', 'rb-list-col');
  const tabsRow = el('div', 'rb-tabs');
  tabsRow.setAttribute('role', 'tablist');
  const search = document.createElement('input');
  search.type = 'search';
  search.className = 'rb-search';
  search.placeholder = 'Search cards…';
  search.setAttribute('aria-label', 'Search cards');
  search.autocapitalize = 'off';
  search.autocomplete = 'off';
  search.spellcheck = false;
  search.enterKeyHint = 'search';
  search.maxLength = QUERY_MAX;
  const offline = el('div', 'rb-offline');
  offline.hidden = true;
  offline.setAttribute('role', 'status');
  const line = el('div', 'rb-line');
  line.hidden = true;
  line.setAttribute('role', 'status');
  line.setAttribute('aria-live', 'polite');
  const pull = el('div', 'rb-pull', 'Pull to refresh');
  pull.hidden = true;
  const list = el('div', 'rb-list');
  listCol.append(tabsRow, search, offline, line, pull, list);

  const cardCol = el('div', 'rb-card-col');
  const cardBar = el('div', 'rb-card-bar');
  const cardBack = button('rb-back rb-card-back', '‹', 'Back to the cards');
  const cardRef = el('div', 'rb-card-ref');
  cardBar.append(cardBack, cardRef);
  const cardScroll = el('div', 'rb-card-scroll');
  const cardEmpty = el('div', 'rb-card-empty', 'Pick a card to read it.');
  const cardLine = el('div', 'rb-line rb-card-line');
  cardLine.hidden = true;
  cardLine.setAttribute('role', 'status');
  cardLine.setAttribute('aria-live', 'polite');

  const reply = el('div', 'rb-reply');
  const replyBox = document.createElement('textarea');
  replyBox.className = 'rb-reply-text';
  replyBox.rows = 1;
  replyBox.placeholder = 'Reply on this card…';
  replyBox.setAttribute('aria-label', 'Reply on this card');
  replyBox.autocapitalize = 'sentences';
  replyBox.maxLength = TEXT_MAX;
  const replyRow = el('div', 'rb-reply-row');
  const replyMic = button('rb-mic', '🎤', 'Record a voice clip');
  replyMic.hidden = true;
  const replyStop = button('rb-stop', 'Stop');
  replyStop.hidden = true;
  const sendComment = button('rb-send rb-send-comment', 'Comment');
  const sendDiscuss = button('rb-send-alt rb-send-discuss', 'Discuss');
  const sendPlan = button('rb-send-alt rb-send-plan', 'Plan');
  replyRow.append(replyMic, el('span', 'rb-spacer'), replyStop, sendComment, sendDiscuss, sendPlan);
  reply.append(replyBox, replyRow);
  reply.hidden = true;

  cardCol.append(cardBar, cardScroll, cardEmpty, cardLine, reply);
  body.append(listCol, cardCol);

  const layer = el('div', 'rb-layer');
  layer.hidden = true;
  root.append(bar, body, layer);
  screen.append(root);

  // ---- small things -----------------------------------------------------------------------

  function loadCollapsed() {
    try {
      const stored = JSON.parse(localStorage.getItem('relay-board-collapsed') || 'null');
      if (Array.isArray(stored)) return new Set(stored.map(str));
    } catch { /* a browser without storage: the defaults below */ }
    return new Set([VERIFIED, DONE]);      // the closed stages start folded away
  }

  function saveCollapsed() {
    try { localStorage.setItem('relay-board-collapsed', JSON.stringify([...collapsed])); } catch { /* fine */ }
  }

  function placeDevice() {
    const width = root.clientWidth || window.innerWidth;
    root.dataset.device = width < 600 ? 'phone' : 'tablet';
  }

  // A line, never a modal (the owner's rule for this app, app/outbox.js). `action` is an optional
  // {label, run} drawn as a button at its end. It clears itself unless it is an error.
  function say(node, text, { error = false, action = null, keep = false } = {}) {
    node.replaceChildren();
    node.hidden = !text;
    node.classList.toggle('rb-error', !!text && error);
    if (!text) { clearTimeout(lineTimers.get(node)); return; }
    node.append(el('span', 'rb-line-text', text));
    if (action) {
      const go = button('rb-line-action', action.label);
      go.addEventListener('click', action.run);
      node.append(go);
    }
    const dismiss = button('rb-line-x', '×', 'Dismiss');
    dismiss.addEventListener('click', () => say(node, ''));
    node.append(dismiss);
    clearTimeout(lineTimers.get(node));
    if (!error && !action && !keep) lineTimers.set(node, setTimeout(() => say(node, ''), 6000));
  }

  // Where a line about `cardId` belongs: on the card page when that card is open, else the list.
  const lineFor = (cardId) => (cardId && cardId === openId ? cardLine : line);

  function waiting() {
    if (!enabled) return 0;
    let count = 0;
    for (const row of rows.values()) if (waitingOnOwner(row)) count += 1;
    return count;
  }

  function paintInboxRow() {
    inboxRow.hidden = !enabled;
    if (!enabled) return;
    const count = waiting();
    inboxChip.hidden = count === 0;
    inboxChip.textContent = `${count} waiting on you`;
    if (!loaded) inboxSub.textContent = online() ? 'Loading…' : 'Offline';
    else if (!exists) inboxSub.textContent = 'This project has no board yet.';
    else {
      const openCards = [...rows.values()].filter((row) => !closed(row)).length;
      inboxSub.textContent = `${project ? `${project} · ` : ''}${openCards} open ${openCards === 1 ? 'card' : 'cards'}`;
    }
  }

  // ---- sending ----------------------------------------------------------------------------

  // One `board_request`. A write that may wait goes to the outbox and comes back 'queued' when
  // the link is down; anything else rejects, exactly as `rrp.send` would.
  function request(bodyOf, note = {}) {
    seq += 1;
    const rid = seq;
    asked.set(rid, { type: bodyOf.type, card: str(bodyOf.id), ...note });
    while (asked.size > 64) asked.delete(asked.keys().next().value);
    return outbox.post({ t: 'board_request', rid, request: bodyOf })
      .then((what) => {
        if (what === 'queued') asked.get(rid).queued = true;
        return { rid, what };
      })
      .catch((error) => { asked.delete(rid); throw error; });
  }

  function openBoard() {
    if (!enabled || !online() || openAsked) return;
    openAsked = true;
    request({ type: 'board_open' }).catch(() => { openAsked = false; paintOffline(); });
  }

  function refreshBoard() {
    if (!online()) { paintOffline(); say(line, 'Offline — this is the board as it was.', { error: true }); return; }
    if (!loaded) { openBoard(); return; }
    pull.hidden = false;
    pull.textContent = 'Refreshing…';
    request({ type: 'board_refresh' })
      .catch((error) => say(line, error.message || 'That did not work.', { error: true }))
      .finally(() => setTimeout(() => { pull.hidden = true; }, 600));
    if (openId) getCard(openId);
  }

  function getCard(id) {
    if (!online()) return;
    cardAsked = seq + 1;          // the rid `request` is about to take: one read in flight per card
    request({ type: 'board_card_get', id }).catch(() => { cardAsked = 0; });
  }

  // ---- the list ---------------------------------------------------------------------------

  function tabsOf() {
    const tabs = Array.isArray(config?.tabs) ? config.tabs : [];
    return tabs.filter((item) => item && typeof item === 'object' && str(item.id))
      .map((item) => ({ id: str(item.id), filter: str(item.filter) }));
  }

  // `status:done,dropped` — the one filter board.yaml's own tabs use.
  function tabTakes(tabInfo, row) {
    if (!tabInfo) return row.type !== 'memory' && row.type !== 'alias';
    if (!tabInfo.filter) return str(row.tab) === tabInfo.id;
    return tabInfo.filter.split(/\s+/).every((term) => {
      const [key, value] = term.split(':');
      if (key === 'status') return str(value).split(',').includes(str(row.status));
      if (key === 'label') return (row.labels || []).map(str).includes(str(value));
      return true;
    });
  }

  function matchesWords(row, words) {
    if (!words.length) return true;
    const hay = [row.id, row.title, ...(row.labels || []), row.assignee, row.milestone]
      .map((piece) => str(piece).toLowerCase()).join('\n');
    return words.every((word) => hay.includes(word));
  }

  function shownRows() {
    const tabInfo = tab === ALL_TAB ? null : tabsOf().find((item) => item.id === tab) || null;
    const words = query.toLowerCase().split(/\s+/).filter(Boolean);
    const answered = searchIds && searchFor === query.trim();
    return [...rows.values()].filter((row) => {
      // A closed card shows under a filter tab that asks for it and under All; a folder tab is
      // its own folder, closed cards included, as the desktop's one list is.
      if (!tabTakes(tabInfo, row)) return false;
      if (!words.length) return true;
      return matchesWords(row, words) || (answered && searchIds.has(row.id));
    });
  }

  function paintTabs() {
    const tabs = [{ id: ALL_TAB, title: 'All' }, ...tabsOf().map((item) => ({ id: item.id, title: item.id }))];
    if (!tabs.some((item) => item.id === tab)) tab = ALL_TAB;
    const have = [...tabsRow.children].map((node) => node.dataset.tab);
    if (have.join('\n') !== tabs.map((item) => item.id).join('\n')) {
      tabsRow.replaceChildren();
      for (const item of tabs) {
        const node = button('rb-tab', item.title);
        node.dataset.tab = item.id;
        node.setAttribute('role', 'tab');
        node.addEventListener('click', () => { tab = item.id; paintTabs(); paintList(); });
        tabsRow.append(node);
      }
    }
    for (const node of tabsRow.children) {
      const on = node.dataset.tab === tab;
      node.classList.toggle('rb-tab-on', on);
      node.setAttribute('aria-selected', on ? 'true' : 'false');
    }
    tabsRow.hidden = tabs.length <= 1;
  }

  // The chips of one row, in the desktop's own order (`board::badges`), less what a phone's width
  // cannot carry: labels stop at two.
  function chipsOf(row, showStatus) {
    const chips = [];
    if (waitingOnOwner(row)) chips.push(['rb-chip-waiting', 'waiting on you']);
    else if (str(row.waiting_on)) chips.push(['rb-chip-waiting', `waiting: ${row.waiting_on}`]);
    if (showStatus && row.status) chips.push(['rb-chip-status', STATUS_SHORT[row.status] || statusTitle(row.status)]);
    if (str(row.verified_by) && !selfClosed(row)) chips.push(['rb-chip-done', `✓ ${str(row.verified_by).split(' ')[0]}`]);
    if (row.assignee === 'agent') chips.push(['rb-chip-agent', '✦ agent']);
    else if (str(row.assignee)) chips.push(['rb-chip-plain', str(row.assignee)]);
    for (const label of (row.labels || []).slice(0, 2)) chips.push(['rb-chip-label', str(label)]);
    if (Number(row.tasks_total) > 0) {
      chips.push([Number(row.tasks_done) >= Number(row.tasks_total) ? 'rb-chip-done' : 'rb-chip-plain',
        `☑ ${Number(row.tasks_done) || 0}/${Number(row.tasks_total)}`]);
    }
    if (Number(row.thread_entries) > 0) chips.push(['rb-chip-quiet', `✎ ${Number(row.thread_entries)}`]);
    if (row.private) chips.push(['rb-chip-private', 'private']);
    return chips;
  }

  const rowNodes = new Map();   // `${group}:${id}` -> node, so a change repaints a row in place

  function paintRow(group, row, showStatus) {
    const key = `${group}:${row.id}`;
    let node = rowNodes.get(key);
    if (!node) {
      node = button('rb-row');
      node.dataset.cardId = row.id;
      node.dataset.group = group;
      node.append(el('div', 'rb-row-head'), el('div', 'rb-row-chips'));
      node.firstChild.append(el('span', 'rb-row-flag'), el('span', 'rb-row-id'), el('span', 'rb-row-title'),
        el('span', 'rb-row-busy'));
      node.addEventListener('click', () => openCard(row.id));
      rowNodes.set(key, node);
    }
    const [flag, id, title, lamp] = node.firstChild.children;
    const priority = Math.max(-1, Math.min(3, Number(row.priority) || 0));
    flag.dataset.priority = String(priority);
    flag.hidden = priority === 0;
    id.textContent = `#${row.id}`;
    title.textContent = str(row.title) || '(untitled)';
    const running = busy.has(row.id);
    lamp.hidden = !running;
    lamp.title = running ? `${busy.get(row.id) === 'plan' ? 'Planning' : 'Working'}…` : '';
    lamp.setAttribute('aria-label', running ? 'A turn is running on this card' : '');
    node.classList.toggle('rb-row-open', row.id === openId);
    node.classList.toggle('rb-row-waiting', waitingOnOwner(row));
    const chips = node.lastChild;
    const wanted = chipsOf(row, showStatus);
    const signature = JSON.stringify(wanted);
    if (chips.dataset.signature !== signature) {
      chips.dataset.signature = signature;
      chips.replaceChildren(...wanted.map(([name, text]) => el('span', `rb-chip ${name}`, text)));
    }
    chips.hidden = wanted.length === 0;
    return node;
  }

  function sectionHeader(id, title, count, folded, canFold = true) {
    const node = button('rb-section');
    node.dataset.section = id;
    node.setAttribute('aria-expanded', folded ? 'false' : 'true');
    node.append(el('span', 'rb-section-caret', folded ? '▸' : '▾'), el('span', 'rb-section-title', title),
      el('span', 'rb-section-count', String(count)));
    if (canFold) {
      node.addEventListener('click', () => {
        if (collapsed.has(id)) collapsed.delete(id); else collapsed.add(id);
        saveCollapsed();
        paintList();
      });
    }
    return node;
  }

  // A turn that ends in the agent's answer, or in a Stop, says so. One that died of a provider
  // error does not reach a device, so a lamp goes out by itself after the longest turn there is.
  const BUSY_MAX_MS = 45 * 60 * 1000;
  function pruneBusy() {
    const now = clock();
    for (const id of [...busy.keys()]) {
      if (!busySince.has(id)) busySince.set(id, now);
      else if (now - busySince.get(id) > BUSY_MAX_MS) busy.delete(id);
    }
    for (const id of [...busySince.keys()]) if (!busy.has(id)) busySince.delete(id);
  }

  function paintList() {
    pruneBusy();
    paintInboxRow();
    onCount();
    if (!visible) return;
    const keepScroll = list.scrollTop;
    const nodes = [];
    const used = new Set();
    const take = (group, row, showStatus) => {
      const node = paintRow(group, row, showStatus);
      used.add(`${group}:${row.id}`);
      nodes.push(node);
    };

    if (!loaded) {
      nodes.push(el('div', 'rb-empty', online() ? 'Loading the Board…' : 'Offline. The board shows once your desktop is reachable.'));
    } else if (!exists) {
      nodes.push(el('div', 'rb-empty', 'This project has no board yet. Create one on the desktop: the Board pane offers it.'));
    } else {
      const all = shownRows();
      const filtered = query.trim() !== '';
      const sections = sectionsOf(config, all);
      const pinned = sortRows(all.filter(waitingOnOwner), false);
      if (pinned.length) {
        const folded = !filtered && collapsed.has(WAITING);
        nodes.push(sectionHeader(WAITING, 'Waiting on you', pinned.length, folded));
        nodes[nodes.length - 1].classList.add('rb-section-waiting');
        if (!folded) pinned.forEach((row) => take(WAITING, row, true));
      }
      const grouped = new Map();
      for (const row of all) {
        const home = sectionOf(row, sections);
        if (!home) continue;
        if (!grouped.has(home)) grouped.set(home, []);
        grouped.get(home).push(row);
      }
      for (const section of sections) {
        const closedSection = section.id === DONE || section.id === VERIFIED;
        const cards = sortRows(grouped.get(section.id) || [], closedSection);
        // Nothing is folded while a search is on, and a section with no match gets out of the way.
        if (filtered && !cards.length) continue;
        const folded = !filtered && collapsed.has(section.id);
        nodes.push(sectionHeader(section.id, section.title, cards.length, folded));
        if (folded) continue;
        const multi = section.statuses.length > 1;
        const tucked = [];
        for (const row of cards) {
          if (!filtered && selfClosed(row)) { tucked.push(row); continue; }
          take(section.id, row, multi && row.status !== section.id);
        }
        if (tucked.length) {
          const openFold = foldOpen.has(section.id);
          const fold = button('rb-fold', `${openFold ? '▾' : '▸'} ${tucked.length} closed by the agent`);
          fold.dataset.fold = section.id;
          fold.addEventListener('click', () => {
            if (foldOpen.has(section.id)) foldOpen.delete(section.id); else foldOpen.add(section.id);
            paintList();
          });
          nodes.push(fold);
          if (openFold) tucked.forEach((row) => take(section.id, row, false));
        }
      }
      if (filtered && !nodes.length) nodes.push(el('div', 'rb-empty', `No card matches “${query.trim()}”.`));
      else if (!all.length && !filtered) nodes.push(el('div', 'rb-empty', 'No cards here yet. + files one.'));
    }
    for (const key of [...rowNodes.keys()]) if (!used.has(key)) rowNodes.delete(key);
    list.replaceChildren(...nodes);
    list.scrollTop = keepScroll;
    barSub.textContent = project;
  }

  function paintOffline() {
    const down = enabled && !online();
    const writes = outbox.pending.filter((message) => message.t === 'board_request').length;
    const parts = [];
    if (down) parts.push(loaded ? `Offline · the board as of ${clockText(loadedAt)}` : 'Offline');
    if (writes) parts.push(`${writes === 1 ? '1 card change' : `${writes} card changes`} · ${QUEUED_WORDS}`);
    offline.textContent = parts.join(' · ');
    offline.hidden = parts.length === 0;
    paintInboxRow();
  }

  // ---- the card page ----------------------------------------------------------------------

  const mdOptions = {
    onLink: (url, text) => confirmLink(url, text),
    onCard: (id) => openCard(id),
    knowsCard: (id) => rows.has(id),
  };

  function openCard(id) {
    const wanted = str(id).replace(/^#/, '').toUpperCase();
    if (!wanted) return;
    if (openId && openId !== wanted) drafts.set(openId, replyBox.value);
    if (openId !== wanted) { card = null; landedOn = ''; executeArmed = ''; say(cardLine, ''); }
    openId = wanted;
    root.dataset.card = 'open';
    replyBox.value = drafts.get(wanted) || '';
    growReply();
    paintCard();
    paintList();
    getCard(wanted);
    if (!online()) {
      offlineLine = true;
      say(cardLine, 'Offline — the card opens when your desktop is reachable.', { error: true, keep: true });
    }
  }

  function closeCard() {
    if (openId) drafts.set(openId, replyBox.value);
    openId = '';
    card = null;
    landedOn = '';
    root.dataset.card = 'closed';
    replyBox.blur();
    paintCard();
    paintList();
  }

  function chip(name, text) { return el('span', `rb-chip ${name}`, text); }

  function threadEntry(entry, { pendingRid = 0 } = {}) {
    const kind = str(entry.kind) || 'comment';
    const author = str(entry.author) || 'owner';
    const attrs = entry.attrs && typeof entry.attrs === 'object' ? entry.attrs : {};
    const mode = str(entry.mode || attrs.mode);
    const node = el('div', `rb-entry rb-entry-${kind.replace(/[^a-z]/g, '')}`);
    node.dataset.kind = kind;
    node.dataset.author = author;
    if (entry.entry_id) node.dataset.entryId = str(entry.entry_id);
    if (pendingRid) node.dataset.pending = String(pendingRid);
    if (kind === 'event') {
      // The audit trail: one muted line each, as the desktop draws them.
      node.append(renderMarkdown(str(entry.text).replace(/^\s*-\s+/, ''), mdOptions));
      return node;
    }
    const head = el('div', 'rb-entry-head');
    head.append(el('span', 'rb-entry-author', author === 'agent' ? '✦ agent' : author));
    const words = [];
    if (mode) words.push(mode === 'plan' ? 'Plan' : 'Discuss');
    else if (kind !== 'comment' && kind !== 'note') words.push(kind);
    if (author === 'agent' && str(attrs.model)) words.push(nameOf(str(attrs.model)));
    const when = pendingRid ? QUEUED_WORDS : timeText(entryTime(entry.entry_id), clock());
    if (when) words.push(when);
    head.append(el('span', 'rb-entry-meta', words.join(' · ')));
    node.append(head, renderMarkdown(str(entry.text), mdOptions));
    if (kind === 'question') {
      const choices = numberedOptions(str(entry.text));
      if (choices.length) {
        const row = el('div', 'rb-options');
        for (const choice of choices) {
          const pick = button('rb-option');
          pick.dataset.option = String(choice.number);
          pick.append(el('span', 'rb-option-n', String(choice.number)),
            el('span', 'rb-option-text', choice.text.length > 72 ? `${choice.text.slice(0, 71)}…` : choice.text));
          pick.addEventListener('click', () => {
            const before = replyBox.value;
            replyBox.value = `${before}${before && !before.endsWith('\n') ? '\n' : ''}${choice.number}. `;
            growReply();
            replyBox.focus();
            replyBox.setSelectionRange(replyBox.value.length, replyBox.value.length);
          });
          row.append(pick);
        }
        node.append(row);
      }
    }
    return node;
  }

  function paintActions(row) {
    const actions = el('div', 'rb-actions');
    const status = str(card?.status || row?.status);
    const isClosed = status === 'done' || status === 'dropped';
    const verifyLane = status === 'needs-verification' || status.startsWith('needs-qa');
    const running = busy.has(openId);
    const move = button('rb-action rb-action-move', 'Move…');
    move.addEventListener('click', openMoveSheet);
    actions.append(move);
    if (!isClosed && !verifyLane) {
      const execute = button('rb-action rb-action-execute', 'Execute');
      execute.disabled = running || !card;
      execute.addEventListener('click', () => runAction('execute'));
      actions.append(execute);
    }
    if (verifyLane) {
      const verify = button('rb-action rb-action-verify', 'Verify');
      verify.disabled = running;
      verify.addEventListener('click', () => runAction('verify'));
      actions.append(verify);
    }
    return actions;
  }

  function paintCard() {
    const row = rows.get(openId) || null;
    const have = !!openId;
    cardScroll.hidden = !have;
    cardEmpty.hidden = have;
    reply.hidden = !have;
    cardBar.hidden = !have;
    if (!have) { cardScroll.replaceChildren(); return; }
    cardRef.textContent = `#${openId}`;
    const stick = cardScroll.scrollHeight - cardScroll.scrollTop - cardScroll.clientHeight < 60;
    const keep = cardScroll.scrollTop;
    const nodes = [];

    nodes.push(el('h2', 'rb-card-title', str(card?.title || row?.title) || `#${openId}`));
    const front = card?.front && typeof card.front === 'object' ? card.front : {};
    const chips = el('div', 'rb-card-chips');
    const status = str(card?.status || row?.status);
    if (status) chips.append(chip('rb-chip-status', statusTitle(status)));
    const owes = str(front.waiting_on ?? row?.waiting_on);
    if (owes) chips.append(chip('rb-chip-waiting', owes.toLowerCase() === 'owner' ? 'waiting on you' : `waiting: ${owes}`));
    const who = str(front.assignee ?? row?.assignee);
    if (who) chips.append(chip(who === 'agent' ? 'rb-chip-agent' : 'rb-chip-plain', who === 'agent' ? '✦ agent' : who));
    const where = str(card?.tab || row?.tab);
    if (where) chips.append(chip('rb-chip-plain', where));
    for (const label of (front.labels || row?.labels || [])) chips.append(chip('rb-chip-label', str(label)));
    if (str(front.milestone ?? row?.milestone)) chips.append(chip('rb-chip-quiet', str(front.milestone ?? row?.milestone)));
    const session = str(front.session ?? row?.session);
    if (session) {
      // The pane holding the card (#R9G7). The hub sends the token's first eight characters, which
      // is what the desktop's chip draws; when that pane is in the inbox the chip is the way to it.
      const holder = panes().find((item) => str(item?.id).startsWith(session.slice(0, 8)));
      const tag = holder ? button('rb-chip rb-chip-session rb-chip-link', `pane ${session.slice(0, 8)} ›`) : chip('rb-chip-quiet', `pane ${session.slice(0, 8)}`);
      if (holder) tag.addEventListener('click', () => { hide(); openPane(holder.id); });
      chips.append(tag);
    }
    if (busy.has(openId)) chips.append(chip('rb-chip-busy', busy.get(openId) === 'plan' ? 'planning…' : 'working…'));
    nodes.push(chips, paintActions(row));

    if (!card) {
      nodes.push(el('div', 'rb-empty', online() ? 'Loading the card…' : 'Offline.'));
    } else {
      if (str(front.acceptance)) {
        const box = el('section', 'rb-section-body');
        box.append(el('h3', 'rb-body-heading', 'Acceptance'), renderMarkdown(str(front.acceptance), mdOptions));
        nodes.push(box);
      }
      const tasks = Array.isArray(card.tasks) ? card.tasks : [];
      for (const section of bodySections(str(card.body))) {
        const box = el('section', 'rb-section-body');
        const name = section.heading.replace(/\s*\(.*\)\s*$/, '').trim().toLowerCase();
        if (section.heading) box.append(el('h3', 'rb-body-heading', section.heading));
        if ((name === 'tasks' || name === 'steps') && tasks.length) box.append(paintTasks(tasks));
        else box.append(renderLines(section.lines, mdOptions));
        box.dataset.section = name;
        nodes.push(box);
      }
      if (card.body_truncated || card.truncated) nodes.push(el('div', 'rb-note rb-truncated', 'Some of this card was too long to send. The whole of it is on the desktop.'));

      const thread = Array.isArray(card.thread) ? card.thread : [];
      const total = Number(card.thread_total) || thread.length;
      const threadBox = el('section', 'rb-thread');
      threadBox.append(el('h3', 'rb-body-heading', total > thread.length
        ? `Thread · the last ${thread.length} of ${total}` : 'Thread'));
      if (!thread.length && !queuedEntries.size) threadBox.append(el('div', 'rb-note', 'Nothing yet. A comment, a Discuss or a Plan starts it.'));
      for (const entry of thread) threadBox.append(threadEntry(entry));
      for (const [rid, waitingEntry] of queuedEntries) {
        if (waitingEntry.card === openId) threadBox.append(threadEntry({ author: 'owner', kind: waitingEntry.kind || 'note', text: waitingEntry.text }, { pendingRid: rid }));
      }
      if (busy.has(openId)) {
        const live = el('div', 'rb-entry rb-entry-live');
        live.append(el('div', 'rb-entry-head', `✦ agent · ${busy.get(openId) === 'plan' ? 'planning' : 'working on it'}… the answer lands here.`));
        threadBox.append(live);
      }
      nodes.push(threadBox);
    }
    cardScroll.replaceChildren(...nodes);
    // A card opens at its title — or, when it is waiting on an answer, at the question, which is
    // what the person came for. After that, a reader who was at the end of the thread stays at
    // the end as it grows, and anyone reading further up is left where they are.
    if (card && landedOn !== openId) {
      landedOn = openId;
      const asks = answering() ? [...cardScroll.querySelectorAll('.rb-entry-question')].pop() : null;
      cardScroll.scrollTop = asks ? Math.max(0, asks.offsetTop - cardScroll.offsetTop - 8) : 0;
    } else cardScroll.scrollTop = stick && card ? cardScroll.scrollHeight : keep;
    paintReply();
  }

  function paintTasks(tasks) {
    const box = el('ul', 'rb-tasks');
    for (const task of tasks) {
      const item = el('li', 'rb-task');
      if (Number(task.depth) > 0) item.classList.add('rb-task-nested');
      const tick = document.createElement('input');
      tick.type = 'checkbox';
      tick.disabled = true;
      tick.checked = !!task.done;
      const words = el('span', 'rb-task-text');
      words.append(renderMarkdown(str(task.text), mdOptions));
      if (task.status === 'dropped') words.classList.add('rb-task-dropped');
      item.append(tick, words);
      if (task.status && !['open', 'done'].includes(task.status)) item.append(chip('rb-chip-quiet', str(task.status)));
      box.append(item);
    }
    return box;
  }

  // Is the reply an answer? The thread's newest question with nothing of the owner's after it.
  // The desktop records an answer as a `decision` quoting the owner's words (its own rule for
  // that kind); anything else said on a card is a `note`, as the desktop's reply box sends it.
  function answering() {
    const thread = Array.isArray(card?.thread) ? card.thread : [];
    for (let at = thread.length - 1; at >= 0; at -= 1) {
      const entry = thread[at];
      if (str(entry.kind) === 'event') continue;
      if (str(entry.author) !== 'agent') return false;
      if (str(entry.kind) === 'question') return true;
      if (str(entry.kind) === 'decision') return false;     // already answered, by another route
    }
    return false;
  }

  function paintReply() {
    const running = busy.has(openId);
    const answer = answering();
    sendComment.textContent = answer ? 'Answer' : 'Comment';
    sendComment.dataset.kind = answer ? 'decision' : 'note';
    replyBox.placeholder = answer ? 'Answer the question…' : 'Reply on this card…';
    replyStop.hidden = !running;
    sendDiscuss.disabled = running;
    sendPlan.disabled = running;
    const micOn = voiceUsable();
    replyMic.hidden = !micOn;
    replyMic.classList.toggle('rb-recording', !!recorder && voiceTarget === replyBox);
    replyMic.disabled = !recorder && !!voiceId;
  }

  function growReply() {
    replyBox.style.height = 'auto';
    const cap = Math.max(44, Math.round((window.visualViewport?.height || window.innerHeight) * 0.3));
    replyBox.style.height = `${Math.min(replyBox.scrollHeight, cap)}px`;
  }

  // ---- the three sends, and the actions ---------------------------------------------------

  function sendReply(mode) {
    const text = replyBox.value.trim();
    const id = openId;
    if (!id) return;
    if (mode === 'comment') {
      const kind = answering() ? 'decision' : 'note';
      const word = kind === 'decision' ? 'Answer' : 'Comment';
      if (!text) { say(cardLine, `Write the ${word.toLowerCase()} first.`, { error: true }); replyBox.focus(); return; }
      // `note` is what the desktop's own reply box sends (src/BoardPane.cpp); an answer to the
      // agent's question goes as `decision`, which the desktop writes down as the owner's words,
      // quoted. A device may send no other kind but `question`, which a person never needs to.
      request({ type: 'board_comment', id, text, kind }, { text, word })
        .then(({ rid, what }) => {
          if (what === 'queued') {
            queuedEntries.set(rid, { card: id, text, kind });
            say(lineFor(id), `${word} · ${QUEUED_WORDS}`, { keep: true });
          }
          paintOffline();
          if (id === openId) paintCard();
        })
        .catch((error) => { restoreReply(id, text); say(lineFor(id), error.message || 'That did not send.', { error: true }); });
      clearReply(id);
      return;
    }
    if (mode === 'discuss' && !text) {
      // The empty send **resumes this card's queue** (#7JD1; owner, 2026-09-21: "why don't we
      // just copy the functionality and have enter resume"). At the desk that is Enter on an
      // empty prompt box; here it is Discuss with nothing typed, which is the same box and the
      // same key. Stop is `board_cancel` and pauses the card's queue, and until this a phone had
      // no way back at all — the Resume button is the desktop's, and a device is sent none of a
      // queue's state, so it asks blind and the answer says whether anything was waiting.
      if (!online()) { say(cardLine, 'Offline — resuming the queue needs your desktop.', { error: true }); return; }
      request({ type: 'board_resume', id })
        .catch((error) => say(lineFor(id), error.message || 'That did not send.', { error: true }));
      replyBox.focus();
      return;
    }
    if (!online()) {
      // A turn is work that starts now, on the desktop: it is not something to find running
      // twenty minutes after the thought. The words stay where they are.
      say(cardLine, `Offline — ${mode === 'plan' ? 'Plan' : 'Discuss'} needs your desktop. Your words are still here.`, { error: true });
      return;
    }
    request({ type: 'board_ask', id, text, mode }, { text, mode })
      .then(() => { busy.set(id, mode); if (id === openId) paintCard(); paintList(); })
      .catch((error) => { restoreReply(id, text); say(lineFor(id), error.message || 'That did not send.', { error: true }); });
    clearReply(id);
  }

  function clearReply(id) {
    drafts.delete(id);
    replyBox.value = '';
    growReply();
  }

  // Put refused words back, unless the person has typed something else since.
  function restoreReply(id, text) {
    if (!text) return;
    if (id === openId) {
      if (!replyBox.value.trim()) { replyBox.value = text; growReply(); }
    } else if (!str(drafts.get(id)).trim()) drafts.set(id, text);
  }

  // The desktop's own rule for Execute (src/BoardPane.cpp `execute()`): a card with neither a
  // `## Plan` nor an acceptance line asks once, on the card and not in a dialog, because the
  // pane's agent would be working from the issue alone. The second tap goes ahead.
  let executeArmed = '';
  function needsArming() {
    if (!card) return false;
    const hasPlan = (Array.isArray(card.sections) ? card.sections : [])
      .some((heading) => str(heading).replace(/\s*\(.*\)\s*$/, '').trim().toLowerCase() === 'plan');
    return !hasPlan && !str(card.front?.acceptance).trim();
  }

  function runAction(action) {
    const id = openId;
    if (!id) return;
    if (action === 'execute' && needsArming() && executeArmed !== id) {
      executeArmed = id;
      say(cardLine, `#${id} has no plan and no acceptance yet, so the pane’s agent would work from the issue alone. `
        + 'Tap Execute again to hand it over as it is, or Plan first.', { error: true });
      return;
    }
    executeArmed = '';
    if (!online()) { say(cardLine, `Offline — ${action === 'verify' ? 'Verify' : 'Execute'} needs your desktop.`, { error: true }); return; }
    say(cardLine, action === 'verify' ? 'Asking your desktop to verify…' : 'Asking your desktop to execute…', { keep: true });
    request({ type: 'board_action', id, action })
      .catch((error) => say(lineFor(id), error.message || 'That did not send.', { error: true }));
  }

  function stopTurn() {
    const id = openId;
    if (!id) return;
    request({ type: 'board_cancel', id })
      .catch((error) => say(lineFor(id), error.message || 'That did not send.', { error: true }));
  }

  // ---- sheets -----------------------------------------------------------------------------

  function closeSheet() {
    if (recorder && voiceTarget && sheet && sheet.contains(voiceTarget)) stopVoice();
    layer.hidden = true;
    layer.replaceChildren();
    sheet = null;
  }

  function openSheet(kind, title) {
    closeSheet();
    const backdrop = el('div', 'rb-backdrop');
    backdrop.addEventListener('click', closeSheet);
    sheet = el('div', 'rb-sheet');
    sheet.dataset.kind = kind;
    sheet.setAttribute('role', 'dialog');
    sheet.setAttribute('aria-label', title);
    const head = el('div', 'rb-sheet-head');
    const cancel = button('rb-sheet-cancel', 'Cancel');
    cancel.addEventListener('click', closeSheet);
    head.append(el('div', 'rb-sheet-title', title), cancel);
    sheet.append(head);
    layer.append(backdrop, sheet);
    layer.hidden = false;
    return sheet;
  }

  function openMoveSheet() {
    const id = openId;
    const row = rows.get(id);
    if (!id) return;
    const box = openSheet('move', `Move #${id}`);
    const type = str(card?.type || row?.type) || 'work';
    const statuses = Array.isArray(config?.statuses?.[type]) && config.statuses[type].length
      ? config.statuses[type].map(str) : WORK_STATUSES;
    const current = str(card?.status || row?.status);
    const reason = document.createElement('input');
    reason.type = 'text';
    reason.className = 'rb-field rb-move-reason';
    reason.placeholder = 'Why (optional, one line)';
    reason.setAttribute('aria-label', 'Why the card is moving');
    reason.maxLength = REASON_MAX;
    reason.enterKeyHint = 'done';
    box.append(reason);
    const stages = el('div', 'rb-stages');
    for (const status of statuses) {
      const pick = button('rb-stage', statusTitle(status));
      pick.dataset.status = status;
      if (status === current) { pick.classList.add('rb-stage-current'); pick.disabled = true; }
      pick.addEventListener('click', () => {
        const why = reason.value.trim().replace(/\s+/g, ' ');
        closeSheet();
        request({ type: 'board_move', id, status, reason: why }, { status })
          .then(({ what }) => {
            if (what === 'queued') say(lineFor(id), `Move to ${statusTitle(status)} · ${QUEUED_WORDS}`, { keep: true });
            paintOffline();
          })
          .catch((error) => say(lineFor(id), error.message || 'That did not send.', { error: true }));
      });
      stages.append(pick);
    }
    box.append(stages);
  }

  function folderTabs() {
    return tabsOf().filter((item) => !item.filter && item.id !== 'memory' && item.id !== 'aliases');
  }

  function openCreateSheet() {
    const box = openSheet('create', 'New card');
    const tabs = folderTabs();
    const tabPick = document.createElement('select');
    tabPick.className = 'rb-field rb-create-tab';
    tabPick.setAttribute('aria-label', 'Which tab the card goes in');
    for (const item of tabs) {
      const option = document.createElement('option');
      option.value = item.id;
      option.textContent = item.id;
      tabPick.append(option);
    }
    if (tabs.some((item) => item.id === tab)) tabPick.value = tab;
    tabPick.hidden = tabs.length <= 1;
    const title = document.createElement('input');
    title.type = 'text';
    title.className = 'rb-field rb-create-title';
    title.placeholder = 'Title (optional: the first line is used)';
    title.setAttribute('aria-label', 'Title');
    title.maxLength = TITLE_MAX;
    title.autocapitalize = 'sentences';
    const text = document.createElement('textarea');
    text.className = 'rb-field rb-create-text';
    text.rows = 5;
    text.placeholder = 'What do you want? Your words are kept exactly as typed.';
    text.setAttribute('aria-label', 'The request, in your words');
    text.autocapitalize = 'sentences';
    text.maxLength = TEXT_MAX;
    const labels = document.createElement('input');
    labels.type = 'text';
    labels.className = 'rb-field rb-create-labels';
    labels.placeholder = 'Labels (optional, comma separated)';
    labels.setAttribute('aria-label', 'Labels');
    labels.autocapitalize = 'off';
    const note = el('div', 'rb-sheet-note');
    const foot = el('div', 'rb-sheet-foot');
    const mic = button('rb-mic rb-create-mic', '🎤', 'Record a voice clip');
    mic.hidden = !voiceUsable();
    mic.addEventListener('click', () => toggleVoice(text, mic, note));
    // Without the desktop's transcription there is still the keyboard's own microphone, and
    // saying so is kinder than a button that is not there for no stated reason.
    if (mic.hidden) note.textContent = 'To dictate, use the microphone on your keyboard.';
    const create = button('rb-send rb-create-send', 'Create');
    create.addEventListener('click', () => {
      // Verbatim: not trimmed inside, not reflowed. Only the blank lines at either end go.
      const words = text.value.replace(/^\s*\n/, '').replace(/\s+$/, '');
      if (!words.trim()) { note.textContent = 'Say what you want first.'; note.classList.add('rb-error'); text.focus(); return; }
      const wantedLabels = [...new Set(labels.value.split(',').map((item) => item.trim()).filter(Boolean))];
      const badLabel = wantedLabels.find((item) => !LABEL_OK.test(item));
      if (badLabel !== undefined || wantedLabels.length > LABELS_MAX) {
        note.textContent = badLabel !== undefined
          ? `“${badLabel}” cannot be a label: a label is up to 40 letters, digits, spaces and _ . : + -`
          : `A card takes at most ${LABELS_MAX} labels.`;
        note.classList.add('rb-error');
        labels.focus();
        return;
      }
      const where = tabPick.value || (tabs[0] && tabs[0].id) || 'features';
      closeSheet();
      request({ type: 'board_create', tab: where, title: title.value.trim(), request: words, labels: wantedLabels },
        { opens: true })
        .then(({ what }) => {
          say(line, what === 'queued' ? `New card · ${QUEUED_WORDS}` : 'Filing the card…', { keep: what === 'queued' });
          paintOffline();
        })
        .catch((error) => say(line, error.message || 'That did not send.', { error: true }));
    });
    foot.append(mic, el('span', 'rb-spacer'), create);
    box.append(tabPick, title, text, labels, note, foot);
    setTimeout(() => text.focus(), 50);
  }

  // A link in a card is somebody else's text. The whole address is shown before anything opens,
  // and only a web or mail address is ever offered.
  function confirmLink(url, words) {
    const box = openSheet('link', 'Open this link?');
    if (words && words !== url) box.append(el('div', 'rb-sheet-note', words));
    box.append(el('div', 'rb-link-url', url));
    const foot = el('div', 'rb-sheet-foot');
    const copy = button('rb-send-alt rb-link-copy', 'Copy');
    copy.addEventListener('click', () => {
      navigator.clipboard?.writeText(url).then(() => { copy.textContent = 'Copied'; }).catch(() => { copy.textContent = 'Copying was refused'; });
    });
    foot.append(copy, el('span', 'rb-spacer'));
    if (OPENABLE.test(url)) {
      const go = button('rb-send rb-link-open', 'Open in browser');
      go.addEventListener('click', () => {
        closeSheet();
        window.open(url, '_blank', 'noopener,noreferrer');
      });
      foot.append(go);
    }
    box.append(foot);
  }

  // ---- voice --------------------------------------------------------------------------------
  // The app's clip → desktop transcription path (app/app.js "voice") is addressed to a pane: the
  // hub hands the audio to that pane's worker and sends the words back to the phone that spoke,
  // with the clip's id, without touching the pane. So a card borrows a pane for the round trip
  // — the first in the list — and with no pane open there is no microphone here, and the
  // keyboard's own dictation is what the sheet points to.

  function voiceUsable() {
    return enabled && voiceOffered && typeof MediaRecorder !== 'undefined'
      && !!navigator.mediaDevices?.getUserMedia && !!window.isSecureContext && panes().length > 0;
  }

  function toggleVoice(target, micButton, noteNode) {
    if (recorder) { stopVoice(); return; }
    if (voiceId) return;
    startVoice(target, micButton, noteNode);
  }

  async function startVoice(target, micButton, noteNode) {
    const tell = (text, error = false) => {
      if (noteNode) { noteNode.textContent = text; noteNode.classList.toggle('rb-error', error); }
      else say(cardLine, text, { error, keep: !error });
    };
    let stream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    } catch (error) {
      tell(error?.name === 'NotAllowedError'
        ? 'This browser refused the microphone. Allow it for this site and try again.'
        : 'No microphone is available on this device.', true);
      return;
    }
    const wanted = ['audio/webm;codecs=opus', 'audio/webm', 'audio/ogg;codecs=opus', 'audio/mp4']
      .find((type) => MediaRecorder.isTypeSupported?.(type)) || '';
    try {
      recorder = new MediaRecorder(stream, { ...(wanted ? { mimeType: wanted } : {}), audioBitsPerSecond: 32000 });
    } catch {
      recorder = new MediaRecorder(stream);
    }
    voiceTarget = target;
    const mine = recorder;
    const chunks = [];
    const done = () => {
      for (const track of stream.getTracks()) track.stop();
      recorder = null;
      micButton.classList.remove('rb-recording');
    };
    mine.addEventListener('dataavailable', (event) => { if (event.data?.size) chunks.push(event.data); });
    mine.addEventListener('error', () => { done(); tell('The recording failed.', true); });
    mine.addEventListener('stop', async () => {
      const type = mine.mimeType || wanted;
      done();
      const blob = new Blob(chunks, { type });
      const format = VOICE_CONTAINERS[str(type).split(';')[0].trim().toLowerCase()] || '';
      const pane = panes()[0]?.id;
      if (!blob.size) { tell('Nothing was recorded.', true); return; }
      if (!format) { tell(`This browser records ${type || 'an unknown format'}, which the desktop cannot read.`, true); return; }
      if (blob.size > VOICE_MAX_BYTES) { tell('That clip is too long to send; keep it under a minute.', true); return; }
      if (!pane) { tell('Transcribing needs a pane open on your desktop. Use the keyboard’s dictation instead.', true); return; }
      voiceId = `bv${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;
      voiceTell = tell;
      micButton.disabled = true;
      voiceButton = micButton;
      tell('Transcribing on the desktop…');
      try {
        const bytes = new Uint8Array(await blob.arrayBuffer());
        let binary = '';
        for (let at = 0; at < bytes.length; at += 0x8000) binary += String.fromCharCode(...bytes.subarray(at, at + 0x8000));
        await sendRaw({ t: 'voice', pane, format, data: btoa(binary), id: voiceId });
      } catch (error) {
        voiceOver();
        tell(error.message || 'The clip could not be sent.', true);
      }
    });
    mine.start();
    setTimeout(() => { if (recorder === mine) stopVoice(); }, VOICE_MAX_MS);
    micButton.classList.add('rb-recording');
    tell('Listening… tap the microphone again to transcribe.');
  }

  let voiceTell = null;
  let voiceButton = null;

  function stopVoice() {
    if (!recorder) return;
    try { recorder.stop(); } catch { recorder = null; }
  }

  function voiceOver() {
    voiceId = '';
    if (voiceButton) voiceButton.disabled = false;
    voiceButton = null;
  }

  // The transcript, by the clip's id. True when it was this view's clip.
  function onAgent(message) {
    if (!voiceId || message?.id !== voiceId || message?.event?.event !== 'transcribed') return false;
    const text = str(message.event.text);
    const target = voiceTarget;
    const tell = voiceTell;
    voiceOver();
    if (!text) { if (tell) tell('Nothing was said.', true); return true; }
    if (target && target.isConnected) {
      // Appended, never replacing: whatever was already typed is still the person's.
      const before = target.value;
      target.value = before + (!before || /\s$/.test(before) ? '' : ' ') + text;
      if (target === replyBox) growReply();
      target.focus();
    } else if (openId) {
      drafts.set(openId, `${str(drafts.get(openId))} ${text}`.trim());
    }
    if (tell) tell('Transcribed · check it, then send.');
    return true;
  }

  // ---- events -----------------------------------------------------------------------------

  function patchRows(items) {
    for (const item of Array.isArray(items) ? items : []) {
      if (item && typeof item === 'object' && str(item.id)) rows.set(str(item.id), item);
    }
  }

  function settle(rid) {
    const note = asked.get(rid);
    asked.delete(rid);
    return note || null;
  }

  function errorText(event) {
    return str(event.text || event.message) || 'The desktop refused that.';
  }

  function onEvent(message) {
    const event = message?.event;
    if (!enabled || !event || typeof event !== 'object') return;
    const rid = typeof message.rid === 'number' ? message.rid : 0;
    const cardId = str(event.card_id);
    switch (event.event) {
      case 'board': {
        openAsked = false;
        settle(rid);
        config = event.config && typeof event.config === 'object' ? event.config : {};
        exists = event.exists !== false && str(event.state || 'ready') !== 'uninitialized';
        // The hub drops the project's path and sends its folder's own name instead.
        project = str(event.board_name || event.project);
        rows.clear();
        patchRows(event.cards);
        rev = Number(event.rev) || 0;
        loaded = true;
        loadedAt = clock();
        paintTabs();
        paintList();
        paintOffline();
        if (event.truncated) say(line, 'Some of this board was too long to send. The whole of it is on the desktop.', { keep: true });
        if (wantCard) { const wanted = wantCard; wantCard = ''; open(wanted); }
        else if (openId) getCard(openId);
        break;
      }
      case 'board_cards':
        patchRows(event.cards);
        if (!event.more) paintList();
        break;
      case 'board_changed': {
        settle(rid);
        // Two shapes arrive under this name (the desktop's bridge forwards both): a write's own
        // notice, whose `upserts` are bare card ids and which has no `rev`, and then the worker's
        // diff with the rows themselves, the `rev` and the board's `config`. Revisions are not
        // consecutive here — the bridge drops a refresh that found nothing — so a gap is not a
        // missed change, and a reconnect re-opens the board anyway.
        rev = Math.max(rev, Number(event.rev) || 0);
        if (event.config && typeof event.config === 'object') config = event.config;
        const changed = (Array.isArray(event.upserts) ? event.upserts : [])
          .map((item) => (typeof item === 'string' ? item : str(item?.id)));
        patchRows(event.upserts);
        for (const gone of Array.isArray(event.removed) ? event.removed : []) rows.delete(str(gone));
        loadedAt = clock();
        if (openId && Array.isArray(event.removed) && event.removed.map(str).includes(openId)) {
          const was = openId;
          closeCard();
          say(line, `#${was} was deleted on the desktop.`, { error: true });
        } else if (openId && changed.includes(openId) && !cardAsked) {
          getCard(openId);
        }
        paintTabs();
        if (event.more !== true) paintList();
        break;
      }
      case 'board_search':
        settle(rid);
        searchFor = str(event.query).trim();
        searchIds = new Set((Array.isArray(event.ids) ? event.ids : []).map(str));
        if (searchFor === query.trim()) paintList();
        break;
      case 'board_card': {
        settle(rid);
        if (rid === cardAsked) cardAsked = 0;
        for (const [waitingRid, entry] of queuedEntries) {
          if (entry.sent && entry.card === cardId) queuedEntries.delete(waitingRid);
        }
        if (cardId && cardId === openId) {
          card = event;
          if (offlineLine) { offlineLine = false; say(cardLine, ''); }
          paintCard();
        }
        break;
      }
      case 'board_thread_appended': {
        const owner = str(event.author) !== 'agent';
        // The question landing on the thread is the ask being accepted: from here the words are
        // on the card, and a turn that fails later must not put them back in the box.
        if (rid && asked.has(rid)) asked.get(rid).accepted = true;
        if (cardId && owner && str(event.mode)) busy.set(cardId, str(event.mode));
        if (cardId && !owner) busy.delete(cardId);
        if (cardId === openId && card) {
          const thread = Array.isArray(card.thread) ? card.thread : [];
          const entryId = str(event.entry_id);
          const last = thread[thread.length - 1];
          // Heard twice (by its rid, and fanned out) it is still one entry: by its id when it has
          // one, and otherwise by being the same words from the same author as the entry before.
          const repeat = entryId ? thread.some((entry) => str(entry.entry_id) === entryId)
            : !!last && str(last.author) === str(event.author) && str(last.text) === str(event.text);
          if (!repeat) {
            card.thread = [...thread, { entry_id: entryId, author: str(event.author), kind: str(event.kind) || 'comment',
              attrs: { ...(event.mode ? { mode: str(event.mode) } : {}) }, text: str(event.text) }];
            card.thread_total = (Number(card.thread_total) || thread.length) + 1;
          }
          paintCard();
        }
        paintList();
        break;
      }
      case 'board_written': {
        const note = settle(rid);
        queuedEntries.delete(rid);
        paintOffline();
        if (!note) break;
        const target = str(event.card_id || note.card);
        if (note.type === 'board_create') {
          const made = str(event.card_id);
          say(line, made ? `Filed as #${made}.` : 'The card was filed.');
          // Open what was just filed — unless the person has moved on to another card since.
          if (made && note.opens && !openId) openCard(made);
        } else if (note.type === 'board_move') {
          say(lineFor(target), `Moved to ${statusTitle(str(event.status) || note.status)}.`);
        } else if (note.type === 'board_comment') {
          say(lineFor(target), note.word === 'Answer' ? 'Answer recorded.' : 'Comment added.');
        }
        if (target && target === openId && !cardAsked) getCard(openId);
        break;
      }
      case 'board_activity': {
        // What an agent (or the desktop's own pane) just did to a card, in the desktop's words.
        const about = str(event.id || event.card_id);
        if (str(event.actor) && str(event.actor) !== 'owner' && str(event.summary)) {
          say(line, `◆ #${about} · ${str(event.summary)}`, about && rows.has(about)
            ? { action: { label: 'Open', run: () => openCard(about) } } : {});
        }
        break;
      }
      case 'board_cancelled': {
        settle(rid);
        const still = new Set((Array.isArray(event.cards) ? event.cards : []).map(str));
        // The card that was stopped is not running, whatever the list says: a worker answers
        // while that turn's thread is still unwinding and used to name it here, and none of a
        // card turn's own events reach a device to put the lamp out later (protocol 17.4).
        if (cardId && event.stopped !== false) still.delete(cardId);
        for (const id of [...busy.keys()]) if (!still.has(id)) busy.delete(id);
        say(lineFor(cardId), event.stopped === false ? 'Nothing was running on this card.' : 'Stopped.');
        paintCard();
        paintList();
        break;
      }
      case 'board_resumed': {
        settle(rid);
        say(lineFor(cardId), event.resumed === false
            ? 'Nothing was waiting on this card.'
            : 'Resumed — what was queued is running again.');
        break;
      }
      case 'board_action_result': {
        settle(rid);
        const about = cardId || str(event.id);
        const pane = str(event.pane);
        const failed = event.ok === false || !!event.error;
        const verb = str(event.action) === 'verify' ? 'Verifying' : 'Executing';
        const text = str(event.text || event.message || (failed ? str(event.error) : ''))
          || (failed ? 'The desktop could not start that.' : `${verb} in a new pane`);
        // The hub cuts a pane's session token to the eight characters the desktop's chip draws,
        // and the inbox's pane ids are those tokens whole: the pane is the one that starts with it.
        const goToPane = () => {
          const found = panes().find((item) => str(item?.id).startsWith(pane));
          if (!found) { say(lineFor(about), `${text} · that pane is not in your inbox yet.`, { keep: true, action: { label: 'Open pane', run: goToPane } }); return; }
          hide();
          openPane(found.id);
        };
        say(lineFor(about), text, { error: failed, keep: true,
          action: !failed && pane ? { label: 'Open pane', run: goToPane } : null });
        break;
      }
      case 'board_busy':
      case 'board_conflict':
      case 'error': {
        const note = settle(rid);
        const code = event.event === 'error' ? str(event.code) : event.event;
        const about = cardId || str(note?.card);
        if (rid && rid === cardAsked) cardAsked = 0;
        if (note?.type === 'board_open') {
          openAsked = false;
          // The desktop's project has no board (yet): that is a state of the board, drawn
          // where the cards would be, not an error to dismiss.
          if (code === 'board_not_found' || code === 'board_not_initialized') {
            loaded = true;
            exists = false;
            rows.clear();
            paintList();
            break;
          }
        }
        if (Array.isArray(event.cards)) {
          // `board_busy` names what is running right now: the lamps follow it.
          const running = new Set(event.cards.map(str));
          for (const id of [...busy.keys()]) if (!running.has(id)) busy.delete(id);
          for (const id of running) if (!busy.has(id)) busy.set(id, 'discuss');
        } else if (note?.type === 'board_ask' && about && code !== 'board_busy') {
          busy.delete(about);
        }
        if (note && !note.accepted && (note.type === 'board_ask' || note.type === 'board_comment')) restoreReply(about, str(note.text));
        queuedEntries.delete(rid);
        if (code === 'board_conflict') {
          say(lineFor(about), 'This card changed on the desktop while you were writing. It has been reloaded — try again.', { error: true });
          if (about && about === openId) getCard(about);
        } else if (event.event === 'error' && cardId && !note && !rid) {
          // A card turn that failed on its own (a provider error): the lamp goes out.
          busy.delete(cardId);
          say(lineFor(cardId), errorText(event), { error: true });
        } else {
          say(lineFor(about), errorText(event), { error: true });
        }
        paintCard();
        paintList();
        paintOffline();
        break;
      }
      default:
        // `board_chat_*` and anything newer: not this view's to draw. A card turn's own stream
        // (`delta`, `done`) is never forwarded to a device: a turn shows as the lamp, and ends
        // as the agent's entry on the thread.
        break;
    }
  }

  // A refusal that came from the hub rather than the board worker (`t: "error"`): not permitted,
  // rate limited, the desktop has no board bridge. True when it was about a request of ours.
  function onRefused(detail) {
    if (voiceId && detail?.id === voiceId) {
      const tell = voiceTell;
      voiceOver();
      if (tell) tell(str(detail.message) || 'The clip could not be transcribed.', true);
      return true;
    }
    const rid = typeof detail?.rid === 'number' ? detail.rid : typeof detail?.id === 'number' ? detail.id : 0;
    if (rid && asked.has(rid)) {
      onEvent({ rid, event: { event: 'error', code: str(detail.code), text: str(detail.message) } });
      return true;
    }
    if (!visible) return false;
    say(openId ? cardLine : line, str(detail?.message) || 'Refused.', { error: true });
    return true;
  }

  // ---- the controller -----------------------------------------------------------------------

  function setAccess(features, capability) {
    const list = Array.isArray(features) ? features : [];
    const was = enabled;
    enabled = list.includes('board') && capability === 'full';
    voiceOffered = list.includes('voice');
    if (!enabled) {
      if (was) reset();
      paintInboxRow();
      onCount();
      return;
    }
    // Every (re)connect is a new session on the hub: ask again, which also resubscribes.
    openAsked = false;
    openBoard();
    paintInboxRow();
    paintOffline();
    if (visible) { paintList(); paintCard(); }
  }

  function open(cardId = '') {
    const wanted = str(cardId).replace(/^#/, '').toUpperCase();
    if (!enabled || !loaded) {
      // A notification's tap usually *is* what woke the app: hold the card until the board is in.
      if (wanted) wantCard = wanted;
      if (!enabled) return;
    }
    visible = true;
    show('board');
    placeDevice();
    if (!loaded) openBoard();
    paintTabs();
    paintList();
    paintOffline();
    if (wanted && loaded) openCard(wanted);
    else paintCard();
  }

  function hide() {
    if (openId) drafts.set(openId, replyBox.value);
    closeSheet();
    stopVoice();
    visible = false;
    show('inbox');
  }

  function onLink() {
    paintOffline();
    if (!online()) { openAsked = false; cardAsked = 0; }
    if (visible && !loaded) paintList();
  }

  // What was waiting has gone out. Its `board_written` normally takes the waiting entry off the
  // thread; when the desktop had already applied it (a replay it recognised by `msg_id` answers
  // nothing), the next read of the card does, since the entry is then in the thread for real.
  function onOutbox() {
    const waitingRids = new Set(outbox.pending.filter((message) => message.t === 'board_request')
      .map((message) => message.rid));
    for (const [rid, entry] of queuedEntries) if (!waitingRids.has(rid)) entry.sent = true;
    paintOffline();
  }

  function reset() {
    rows.clear();
    busy.clear();
    asked.clear();
    queuedEntries.clear();
    drafts.clear();
    config = null;
    loaded = false;
    openAsked = false;
    openId = '';
    card = null;
    wantCard = '';
    rev = 0;
    root.dataset.card = 'closed';
    if (visible) hide();
    paintInboxRow();
  }

  // ---- wiring -----------------------------------------------------------------------------

  back.addEventListener('click', hide);
  cardBack.addEventListener('click', closeCard);
  refresh.addEventListener('click', refreshBoard);
  add.addEventListener('click', openCreateSheet);
  sendComment.addEventListener('click', () => sendReply('comment'));
  sendDiscuss.addEventListener('click', () => sendReply('discuss'));
  sendPlan.addEventListener('click', () => sendReply('plan'));
  replyStop.addEventListener('click', stopTurn);
  replyMic.addEventListener('click', () => toggleVoice(replyBox, replyMic, null));
  replyBox.addEventListener('input', growReply);
  // The keyboard takes most of the screen: what is left above the box should be the end of the
  // thread — the thing being answered — not the card's title. Again once the keyboard has
  // finished arriving, which is when the strip gets its final height.
  const toThreadEnd = () => { if (document.activeElement === replyBox) cardScroll.scrollTop = cardScroll.scrollHeight; };
  replyBox.addEventListener('focus', () => { toThreadEnd(); setTimeout(toThreadEnd, 350); });
  window.visualViewport?.addEventListener('resize', () => requestAnimationFrame(toThreadEnd));

  search.addEventListener('input', () => {
    query = search.value;
    paintList();
    clearTimeout(searchTimer);
    const words = query.trim();
    if (!words) { searchIds = null; searchFor = ''; return; }
    // Debounced, so a burst of typing is one question; the rows' own fields answer meanwhile.
    searchTimer = setTimeout(() => {
      if (words !== query.trim() || !online()) return;
      request({ type: 'board_search', query: words }).catch(() => {});
    }, 300);
  });

  // Pull to refresh: a drag down from the top of the list, past the threshold, then let go.
  let pullFrom = null;
  let pulled = 0;
  const PULL_AT = 64;
  list.addEventListener('touchstart', (event) => {
    pullFrom = list.scrollTop <= 0 && event.touches.length === 1 ? event.touches[0].clientY : null;
    pulled = 0;
  }, { passive: true });
  list.addEventListener('touchmove', (event) => {
    if (pullFrom === null) return;
    pulled = event.touches[0].clientY - pullFrom;
    if (pulled <= 8 || list.scrollTop > 0) { pull.hidden = true; return; }
    pull.hidden = false;
    pull.textContent = pulled >= PULL_AT ? 'Release to refresh' : 'Pull to refresh';
  }, { passive: true });
  const endPull = () => {
    const go = pullFrom !== null && pulled >= PULL_AT && list.scrollTop <= 0;
    pullFrom = null;
    pulled = 0;
    if (go) refreshBoard(); else pull.hidden = true;
  };
  list.addEventListener('touchend', endPull, { passive: true });
  list.addEventListener('touchcancel', () => { pullFrom = null; pulled = 0; pull.hidden = true; }, { passive: true });

  window.addEventListener('resize', placeDevice);
  document.addEventListener('keydown', (event) => { if (event.key === 'Escape' && sheet) closeSheet(); });

  paintInboxRow();

  return {
    setAccess, onEvent, onRefused, onAgent, onLink, onOutbox, open, close: hide, reset, waiting,
    get visible() { return visible; },
  };
}

// SPDX-License-Identifier: GPL-3.0-or-later
// The guest client: what somebody handed an invite link sees (docs/REMOTE-PROTOCOL.md section 10).
//
// This is a different session from the owner's phone, not the same one with buttons hidden. A
// guest has a **role**, never a capability; their stored record holds a participant id and no
// device id; and this file builds only a guest's controls. The password field, the microphone,
// push notifications, the inbox of the desktop's other panes and the device controls are not
// hidden here — they are never constructed and none of their handlers is ever wired, so there is
// nothing for a mistake in a stylesheet to reveal. The editor's controls are built the moment the
// role first says `editor` and not before, so a viewer's session has no key handler at all.
//
// Everything from the wire is somebody else's: a program prints what it likes into the terminal
// and another guest chooses their own name. Every string goes in through textContent, as in
// app.js, and there is no innerHTML in this file.

import { Rrp, loadGuest, forgetGuest, fingerprint, b64 } from './rrp.js';
import { ScreenView, KEYS, controlByte, keyEventBytes } from './screen.js';
import { cleanCode, cleanPin, validCode, validPin, joinWithCode, meetProblem } from './meet.js';

const rrp = new Rrp();

let invite = null;        // the parsed link, until it is spent
let guest = null;         // the stored guest record: participant, role, panes, expires
let role = 'viewer';
let panes = [];           // the pane ids of the invite; `pane` is the one being watched
let pane = '';
let paneItems = [];
let features = [];
let screenView = null;
let historySeq = 0;
let historyRequest = null;
let holder = '';          // the `control` broadcast's holder: owner | agent | participant:<id>
let holderName = '';
let driving = false;
let waitingControl = false;
let directKeys = false;
let paused = false;
let pauseReason = '';
let presence = [];
let ended = false;
let editorBuilt = false;
let reconnectTimer = null;
let knockTimer = null;
let controlTimer = null;
let expiryTimer = null;
let wired = false;
let noteUntil = 0;     // a held note stands until this moment (see `note`)
const sticky = { ctrl: false, alt: false };
const paneState = new Map();  // pane -> its presence, its driver and whether it is paused
const sent = [];          // composes awaiting `prompt_pending`, oldest first
const prompts = new Map();  // the hub's prompt id -> the row it is showing

const MAX_PENDING = 3;            // section 10.4: three may wait at once
const PROMPT_LAPSE = 600000;      // ten minutes, as PROMPT_LIFETIME in remote/guests.py
const CONTROL_LAPSE = 60000;      // a minute, as CONTROL_LIFETIME
const KNOCK_WAIT = 120;           // seconds, as KNOCK_TIMEOUT
const HOLD = 8000;                // how long a note about a handoff or a pause stands

const $ = (id) => document.getElementById(id);

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

// The app's screens are one set, owner's and guest's alike, so exactly one is ever drawn.
function show(name) {
  for (const screen of document.querySelectorAll('.screen')) {
    screen.hidden = screen.id !== `screen-${name}`;
  }
}

function setStatus(text, kind = '') {
  const chip = $('guest-status');
  chip.textContent = text;
  chip.className = `chip ${kind}`;
}

// One status line, with a way to hold it. A handoff, a pause or a role change is the kind of
// thing a person has to see, and the agent's routine "working…" arrives a moment later and would
// otherwise wipe it off the screen before it had been read.
function note(text, hold = 0) {
  if (!hold && Date.now() < noteUntil) return;
  $('guest-note').textContent = text || '';
  noteUntil = hold ? Date.now() + hold : 0;
}

function platformName() {
  const agent = navigator.userAgent;
  const platform = /Android/i.test(agent) ? 'Android'
    : /iPhone|iPad/i.test(agent) ? 'iOS'
    : /Mac/i.test(agent) ? 'macOS'
    : /Windows/i.test(agent) ? 'Windows' : 'Linux';
  const browser = /Firefox/i.test(agent) ? 'Firefox'
    : /Edg/i.test(agent) ? 'Edge'
    : /Chrome/i.test(agent) ? 'Chrome'
    : /Safari/i.test(agent) ? 'Safari' : 'browser';
  return `${platform} ${browser}`;
}

// ---- getting in ---------------------------------------------------------------------------------

// True when this page is a guest's. An invite link lands on /join, and that is where a guest's
// session stays: the owner's own pairing flow lives at / and /pair and never looks here.
export function guestRoute() {
  return /\/join\/?$/.test(location.pathname);
}

// For the app's start_url, which is '/': a browser with no paired device but a guest record is
// still somebody's session, and offering to pair instead would be wrong.
export async function startGuestIfInvited() {
  const record = await loadGuest();
  if (!record) return false;
  startGuest();
  return true;
}

export function startGuest() {
  wire();
  if (location.hash.includes('v=1')) {
    let link;
    try {
      link = Rrp.parseInviteFragment(location.hash);
    } catch (error) {
      problem(error.message);
      return;
    }
    invite = link;
    // The secret leaves the address bar at once, exactly as pairing does. A fragment never
    // reaches a server, but it is in the history, in a screenshot and in whatever the browser
    // syncs between the person's machines.
    history.replaceState(null, '', location.pathname);
    offerToJoin(link);
    return;
  }
  loadGuest().then((record) => {
    if (record) {
      resume(record);
      return;
    }
    // Nothing in the address and nobody remembered: somebody came to type a meeting code. A `#`
    // with nothing usable after it is different: a link was opened and lost its code on the way
    // (a messenger that cut the fragment off), so say that rather than greet them as a stranger.
    // A link stripped of the `#` as well is the same address as a bare /join and cannot be told
    // apart from one; the form's own sentence covers that case.
    offerCodeForm(location.href.includes('#')
      ? 'This invitation link arrived without its code: part of it was lost on the way. Ask them '
        + 'to send it again, or to read you a meeting code.'
      : '');
  }).catch(() => problem('This browser could not read its own storage.'));
}

async function offerToJoin(link) {
  show('join');
  $('join-note').textContent = '';
  $('join-knock').hidden = false;
  $('join-fingerprint').textContent = '…';
  let remembered = '';
  try {
    remembered = localStorage.getItem('relay-guest-name') || '';
  } catch {
    /* a private window with storage blocked: the field simply starts empty */
  }
  $('join-name').value = remembered;
  // The key is all the link tells us about who this is: the desktop's own name arrives with the
  // admission, because nothing before it has been authenticated by anybody.
  $('join-fingerprint').textContent = await fingerprint(link.desktopPublic);
}

// Back to the join screen with one plain sentence and, when there is still a link to try, the
// button to try it with.
function problem(text) {
  show('join');
  $('join-note').textContent = text;
  $('join-knock').hidden = !invite;
  if (invite) $('join-fingerprint').textContent = $('join-fingerprint').textContent || '…';
  else $('join-fingerprint').textContent = 'unknown';
}

// ---- joining with a meeting code and a PIN (card #97EG) -----------------------------------------
// Somebody who was told `BQRT` and `4829` rather than sent a link. The code phase (app/meet.js)
// runs CPace with the PIN against the desktop and comes back with an ordinary invite fragment,
// which goes down exactly the road a link does: parse, `offerToJoin`, and the same knock, with the
// same five-digit code to compare on the waiting screen.
//
// The PIN stays in its field and in the one call that uses it. It is not put in the address,
// history or storage, and is not logged; the field is emptied once the attempt is over, whichever
// way it went. The screen is built here rather than in index.html, from the classes the join
// screen already uses, and its few layout rules are set through the CSSOM, which the app's CSP
// (style-src 'self') allows where a style attribute would not be.

let codeBusy = false;

function styled(node, rules) {
  Object.assign(node.style, rules);
  return node;
}

// The two fields, styled like `#join-name` but big and monospaced: they are read off another
// screen or out of a message, letter by letter.
function codeField(id, placeholder) {
  const input = styled(el('input'), {
    width: '100%', boxSizing: 'border-box', background: 'var(--bg)', color: 'var(--text)',
    border: '1px solid var(--line)', borderRadius: '10px', padding: '10px 12px',
    font: '600 24px/1.2 ui-monospace, "SF Mono", Menlo, monospace', letterSpacing: '6px',
    textAlign: 'center', marginBottom: '14px',
  });
  input.id = id;
  input.type = 'text';
  input.placeholder = placeholder;
  input.maxLength = 4;
  input.spellcheck = false;
  input.setAttribute('autocorrect', 'off');
  // No `name`: should the page ever submit this form by itself, nothing in it goes anywhere.
  return input;
}

function buildCodeScreen() {
  if ($('screen-meet')) return $('screen-meet');
  const screen = el('section', 'screen');
  screen.id = 'screen-meet';
  screen.hidden = true;
  // With a phone's keyboard up the page is only as tall as what is left above it; the form
  // scrolls inside its screen rather than off the bottom of the page.
  styled(screen, { overflowY: 'auto' });
  const pad = el('div', 'pad');
  pad.append(el('h1', '', 'Join with a meeting code'));
  pad.append(el('p', 'muted', 'Type the four letters and the four-digit PIN you were given by '
    + 'the person sharing their terminal.'));

  const form = document.createElement('form');
  form.id = 'meet-form';
  form.noValidate = true;
  form.autocomplete = 'off';

  const codeLabel = el('label', 'field-label', 'Meeting code');
  codeLabel.htmlFor = 'meet-code';
  const code = codeField('meet-code', 'BQRT');
  code.autocapitalize = 'characters';
  code.setAttribute('autocapitalize', 'characters');
  code.autocomplete = 'off';
  code.inputMode = 'text';
  code.enterKeyHint = 'next';

  const pinLabel = el('label', 'field-label', 'PIN');
  pinLabel.htmlFor = 'meet-pin';
  const pin = codeField('meet-pin', '4829');
  pin.inputMode = 'numeric';
  pin.pattern = '[0-9]*';
  pin.autocomplete = 'one-time-code';
  pin.enterKeyHint = 'go';

  const status = el('p', 'note');
  status.id = 'meet-note';
  status.setAttribute('role', 'status');
  status.setAttribute('aria-live', 'polite');

  const join = el('button', 'plan-run', 'Join');
  join.id = 'meet-join';
  join.type = 'submit';
  styled(join, { width: '100%', marginTop: '2px' });

  form.append(codeLabel, code, pinLabel, pin, status, join);
  pad.append(form);
  pad.append(styled(el('p', 'muted small', 'Sent a link instead? Open the link itself — it has '
    + 'everything in it. If you opened one and ended up here, part of it was lost on the way: '
    + 'ask them to send it again, or to read you a meeting code.'), { marginTop: '16px' }));
  screen.append(pad);
  $('screen-join').after(screen);

  code.addEventListener('input', () => {
    const clean = cleanCode(code.value);
    if (clean !== code.value) code.value = clean;
    if (clean.length === 4 && document.activeElement === code) pin.focus();
  });
  pin.addEventListener('input', () => {
    const clean = cleanPin(pin.value);
    if (clean !== pin.value) pin.value = clean;
  });
  for (const input of [code, pin]) {
    // A phone's keyboard comes up over the lower half of the page; keep the field being typed in
    // above it.
    input.addEventListener('focus', () => {
      setTimeout(() => input.scrollIntoView({ block: 'nearest' }), 300);
    });
  }
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    joinByCode();
  });
  return screen;
}

// A quiet way from the invitation screen to the code form, for whoever landed there without a
// link that works.
function addCodeLink() {
  if ($('join-by-code')) return;
  const button = el('button', 'quiet-button', 'Have a meeting code instead?');
  button.id = 'join-by-code';
  button.type = 'button';
  styled(button, { display: 'block', marginTop: '16px' });
  button.addEventListener('click', () => offerCodeForm());
  $('join-knock').after(button);
}

function codeNote(text, error = false) {
  const status = $('meet-note');
  status.textContent = text;
  status.className = error ? 'note error' : 'note';
}

function offerCodeForm(text = '') {
  buildCodeScreen();
  show('meet');
  setCodeBusy(false);
  codeNote(text, !!text);
  const code = $('meet-code');
  (validCode(code.value) ? $('meet-pin') : code).focus();
}

function setCodeBusy(busy) {
  codeBusy = busy;
  $('meet-code').disabled = busy;
  $('meet-pin').disabled = busy;
  $('meet-join').disabled = busy;
  $('meet-join').textContent = busy ? 'Joining…' : 'Join';
}

async function joinByCode() {
  if (codeBusy) return;
  const codeInput = $('meet-code');
  const pinInput = $('meet-pin');
  const code = cleanCode(codeInput.value);
  const pin = cleanPin(pinInput.value);
  codeInput.value = code;
  if (!validCode(code)) {
    codeNote('A meeting code is four letters, like BQRT.', true);
    codeInput.focus();
    return;
  }
  if (!validPin(pin)) {
    codeNote('The PIN is four digits, like 4829.', true);
    pinInput.focus();
    return;
  }
  setCodeBusy(true);
  codeNote('Checking the code and PIN with their desktop…');
  let fragment;
  try {
    fragment = await joinWithCode(code, pin);
  } catch (error) {
    pinInput.value = '';
    setCodeBusy(false);
    codeNote(meetProblem(error), true);
    const kind = error?.kind || '';
    if (kind === 'unknown_code' || kind === 'burned' || kind === 'expired') codeInput.focus();
    else pinInput.focus();
    return;
  }
  pinInput.value = '';
  setCodeBusy(false);
  codeNote('');
  let link;
  try {
    link = Rrp.parseInviteFragment(fragment);
  } catch (error) {
    offerCodeForm(error.message);
    return;
  }
  // From here it is the invitation path, unchanged: the same screen, the same name field, the
  // same knock and the same five digits to compare on the waiting screen.
  invite = link;
  offerToJoin(link);
}

function knockCountdown(seconds) {
  const left = Math.max(0, seconds);
  const minutes = Math.floor(left / 60);
  const rest = String(left % 60).padStart(2, '0');
  $('knock-countdown').textContent = left
    ? `Waiting ${minutes}:${rest} · they have two minutes to answer.`
    : 'No answer yet…';
}

function stopKnockCountdown() {
  if (knockTimer) clearInterval(knockTimer);
  knockTimer = null;
}

async function knock() {
  const name = $('join-name').value.trim();
  if (!name) {
    $('join-note').textContent = 'Type the name they will see when you knock.';
    $('join-name').focus();
    return;
  }
  if (!invite) {
    problem('There is no invitation link on this page any more. Ask for a fresh one.');
    return;
  }
  try {
    localStorage.setItem('relay-guest-name', name);
  } catch {
    /* nothing to remember it with; the field is still filled in for this knock */
  }
  show('knock');
  $('knock-code').textContent = '·····';
  let left = KNOCK_WAIT;
  knockCountdown(left);
  stopKnockCountdown();
  knockTimer = setInterval(() => { left -= 1; knockCountdown(left); }, 1000);
  const link = invite;
  try {
    const record = await rrp.knock(link, { name, platform: platformName() });
    invite = null;
    stopKnockCountdown();
    await began(record);
  } catch (error) {
    stopKnockCountdown();
    problem(refusal(error));
  }
}

// Every way a knock can fail, in a sentence that says what happened and what to do. The hub
// answers a refusal and a two-minute silence with the same `not_admitted`, so this says both.
function refusal(error) {
  const code = error?.code || '';
  const text = String(error?.message || '');
  if (code === 'not_admitted') {
    return 'They did not let you in — either they said no, or nobody answered the knock. '
      + 'You can knock again, or ask them for a new link.';
  }
  if (code === 'not_permitted') {
    return 'That invitation has been used up, revoked or has expired. Ask whoever invited you '
      + 'for a fresh link.';
  }
  if (code === 'rate_limited') {
    return 'Several people are knocking at this link at once. Wait a moment and knock again.';
  }
  if (/timed out/i.test(text)) {
    return 'Nobody answered. The link may still be good — knock again when they are at their '
      + 'desk.';
  }
  return text || 'That did not work. Knock again in a moment.';
}

async function began(record) {
  guest = record;
  applySession({ role: record.role, panes: record.panes, expires: record.expires,
                 features: record.features });
  show('guest');
  setStatus('connected', 'ok');
  note('');
  openPane(panes[0] || '');
}

async function resume(record) {
  guest = record;
  applySession({ role: record.role, panes: record.panes, expires: record.expires });
  show('guest');
  if (expired()) {
    await endSession('The invitation you joined with has expired.');
    return;
  }
  setStatus('connecting…');
  try {
    await rrp.rejoin(record);
    setStatus('connected', 'ok');
  } catch (error) {
    setStatus('offline', 'warn');
    note(error?.message || 'Could not reach their desktop.');
    scheduleReconnect();
  }
}

function scheduleReconnect() {
  if (reconnectTimer || ended) return;
  reconnectTimer = setTimeout(async () => {
    reconnectTimer = null;
    const record = await loadGuest();
    if (!record || ended || rrp.session) return;
    try {
      await rrp.rejoin(record);
      setStatus('connected', 'ok');
      note('');
      rrp.resume();
      if (pane) rrp.send({ t: 'pane_focus', pane }).catch(() => {});
    } catch {
      scheduleReconnect();
    }
  }, 3000);
}

// ---- the session's own facts --------------------------------------------------------------------

// One place where a role, a pane scope and an expiry are applied, so `admitted`, `welcome` and a
// live role change all land the same way. The role is never cached anywhere else: a promotion or
// a demotion mid-session goes through here and the UI follows with no reload.
function applySession({ role: newRole, panes: newPanes, expires, features: newFeatures }) {
  if (newRole && newRole !== role) role = newRole;
  if (Array.isArray(newPanes) && newPanes.length) panes = newPanes;
  if (Array.isArray(newFeatures)) features = newFeatures;
  if (guest) {
    if (typeof expires === 'number' && expires) guest.expires = expires;
    guest.role = role;
    guest.panes = panes;
  }
  if (role === 'editor') buildEditorControls();
  if (driving && role !== 'editor') {
    // Demoted while holding the keyboard: stop typing before the hub's next refusal, not after.
    driving = false;
    directKeys = false;
  }
  applyIdentity();
  updateDriveUi();
  startExpiryClock();
}

function applyIdentity() {
  $('guest-desktop').textContent = guest?.desktopName || 'their desktop';
  const parts = [`guest · ${role}`];
  const ends = expiryText();
  if (ends) parts.push(ends);
  $('guest-role').textContent = parts.join(' · ');
}

function expired() {
  return !!guest?.expires && guest.expires * 1000 <= Date.now();
}

function expiryText() {
  if (!guest?.expires) return '';
  const left = guest.expires * 1000 - Date.now();
  if (left <= 0) return 'access has ended';
  const minutes = Math.round(left / 60000);
  if (minutes < 60) return `ends in ${Math.max(1, minutes)} min`;
  const hours = Math.round(left / 3600000);
  if (hours < 48) return `ends in ${hours} h`;
  return `ends in ${Math.round(hours / 24)} days`;
}

function startExpiryClock() {
  if (expiryTimer) return;
  expiryTimer = setInterval(() => {
    if (!guest) return;
    applyIdentity();
    if (expired()) endSession('The invitation you joined with has expired.');
  }, 30000);
}

// The end of a guest's session, from whichever direction it came: the owner removed them, the
// share ended, the invite expired. The record goes — `bye` carrying `discard` is the instruction
// to forget it rather than retry — and the screen stops where it was.
async function endSession(text) {
  ended = true;
  if (reconnectTimer) clearTimeout(reconnectTimer);
  reconnectTimer = null;
  if (expiryTimer) clearInterval(expiryTimer);
  expiryTimer = null;
  driving = false;
  await forgetGuest().catch(() => {});
  guest = null;
  rrp.close();
  if (screenView) {
    screenView.destroy();
    screenView = null;
  }
  $('ended-text').textContent = `${text} If you are sent a new link, open it to knock again.`;
  show('ended');
}

// A reason from the wire, made into a sentence rather than pasted mid-sentence.
function sentence(text) {
  const trimmed = String(text || '').trim();
  if (!trimmed) return '';
  return trimmed[0].toUpperCase() + trimmed.slice(1)
    + (/[.!?]$/.test(trimmed) ? '' : '.');
}

// ---- the pane ------------------------------------------------------------------------------------

// The pane chips, at the head of the one context strip. An invite may name several panes and all
// of them are this guest's; with one, the chip is a label rather than a choice.
function paneChips(row) {
  for (const id of panes) {
    const item = paneItems.find((entry) => entry.id === id);
    const title = item?.title || id;
    if (panes.length === 1) {
      row.append(el('span', 'guest-pane-chip is-on', title));
      continue;
    }
    const chip = el('button', `guest-pane-chip${id === pane ? ' is-on' : ''}`, title);
    chip.type = 'button';
    chip.addEventListener('click', () => openPane(id));
    row.append(chip);
  }
}

function ensureScreen() {
  if (screenView) return screenView;
  screenView = new ScreenView($('guest-screen-wrap'));
  screenView.onBehind = (behind) => { $('guest-new-output').hidden = !behind; };
  // Scrollback is reading, which a viewer may do (`history_get` is a viewer's in GUEST_TYPES).
  // It is asked for unconditionally because `admitted` carries no feature list; a desktop with
  // no scrollback behind it refuses the page and the view stops asking.
  screenView.onNeedHistory = (beforeRow, count) => {
    if (!pane) {
      screenView.pending = false;
      return;
    }
    historySeq += 1;
    historyRequest = `h${historySeq}`;
    const message = { t: 'history_get', pane, count, id: historyRequest };
    if (beforeRow !== null) message.before_row = beforeRow;
    rrp.send(message).catch(() => { screenView.pending = false; });
  };
  return screenView;
}

// Presence, who is driving and whether the share is paused, kept per pane. The hub sends all
// three the moment a guest is admitted, which can be before this client has settled on which
// pane it is watching, so they are remembered rather than dropped and read back here.
function stateOf(id) {
  if (!paneState.has(id)) {
    paneState.set(id, { presence: [], holder: '', holderName: '', paused: false, reason: '' });
  }
  return paneState.get(id);
}

function openPane(id) {
  if (!id) return;
  const changed = id !== pane;
  if (changed && pane) rrp.send({ t: 'pane_blur', pane }).catch(() => {});
  pane = id;
  const state = stateOf(pane);
  presence = state.presence;
  holder = state.holder;
  holderName = state.holderName;
  paused = state.paused;
  pauseReason = state.reason;
  driving = !!(guest && holder === `participant:${guest.participant}`);
  ensureScreen();
  if (changed) screenView.resetHistory();
  screenView.fit();
  renderPresence();
  updateDriveUi();
  if (paused) note(pauseText());
  rrp.send({ t: 'pane_focus', pane }).catch(() => {});
  rrp.send({ t: 'screen_get', pane }).catch(() => {});
}

function onScreen(message) {
  if (message.pane !== pane || !screenView) return;
  screenView.apply(message);
}

function onHistory(message) {
  if (!screenView || message.pane !== pane) return;
  if (message.id && message.id !== historyRequest) return;
  screenView.applyHistory(message);
}

function toLive() {
  if (screenView) screenView.toLive();
}

// ---- presence (section 10.3) ---------------------------------------------------------------------

function renderPresence() {
  const row = $('guest-presence');
  row.replaceChildren();
  paneChips(row);
  const owner = el('span', `who owner${holder === 'owner' ? ' is-driving' : ''}`);
  owner.append(el('span', 'who-name', guest?.desktopName || 'their desktop'));
  owner.append(el('span', 'who-role', holder === 'owner' ? 'driving' : 'owner'));
  row.append(owner);
  if (holder === 'agent') {
    const agent = el('span', 'who is-driving');
    agent.append(el('span', 'who-name', 'their agent'));
    agent.append(el('span', 'who-role', 'driving'));
    row.append(agent);
  }
  for (const item of presence) {
    const away = item.online === false;
    const chip = el('span', `who${item.you ? ' you' : ''}${item.driving ? ' is-driving' : ''}`
      + `${away ? ' is-away' : ''}`);
    chip.append(el('span', 'who-name', item.you ? `${item.name} (you)` : item.name));
    chip.append(el('span', 'who-role',
      item.driving ? 'driving' : away ? `${item.role} · away` : item.role));
    row.append(chip);
  }
}

function onParticipants(message) {
  const items = Array.isArray(message.items) ? message.items : [];
  stateOf(message.pane).presence = items;
  // The presence list is also where a live role change arrives: `participants` is sent on join,
  // leave, role change and handoff, and the row marked `you` is this session's own record. A
  // promotion or a demotion therefore follows here, with no reload and no second message — and
  // whichever pane it came for, because a role belongs to the person, not to the pane.
  const me = items.find((item) => item.you);
  if (me && me.role && me.role !== role) {
    const promoted = me.role === 'editor';
    applySession({ role: me.role });
    note(promoted
      ? 'They made you an editor: you can ask to type, and ask their agent.'
      : 'They made you a viewer: you can watch this pane.', HOLD);
  }
  if (message.pane !== pane) return;
  presence = items;
  renderPresence();
}

// ---- control (section 10.3) ------------------------------------------------------------------------

function onControl(message) {
  const state = stateOf(message.pane);
  state.holder = String(message.holder || '');
  state.holderName = String(message.name || '');
  if (message.pane !== pane) return;
  holder = state.holder;
  holderName = state.holderName;
  const mine = guest && holder === `participant:${guest.participant}`;
  const was = driving;
  const asked = waitingControl;
  // The answer to *this* guest's own request carries a reason; the broadcast everyone gets does
  // not. `refused` is the owner saying no, `lapsed` is the minute running out with no answer.
  const reason = String(message.reason || '');
  driving = !!mine;
  waitingControl = false;
  stopControlTimer();
  if (!driving) directKeys = false;
  if (driving && !was) {
    note('What you type now goes to the program on their screen.', HOLD);
  } else if (asked && !driving && reason) {
    note(reason === 'lapsed'
      ? 'Nobody answered. You can ask to type again.'
      : `${ownerName()} said no for now. You can ask again.`, HOLD);
  } else if (was && !driving) {
    // The owner's own keystroke takes control back without asking. Say so at once — and leave
    // whatever is half-typed in the line box alone, because it is still theirs to send later.
    note(holder === 'owner'
      ? `${holderName || ownerName()} took the keyboard back — what you had typed is still here.`
      : holder === 'agent' ? 'Their agent is driving this pane now.'
      : `${holderName || 'Somebody else'} is driving this pane now.`, HOLD);
  }
  renderPresence();
  updateDriveUi();
}

function stopControlTimer() {
  if (controlTimer) clearTimeout(controlTimer);
  controlTimer = null;
}

function askToType() {
  if (!pane || role !== 'editor') return;
  waitingControl = true;
  updateDriveUi();
  rrp.send({ t: 'control_request', pane }).catch((error) => {
    waitingControl = false;
    updateDriveUi();
    note(error.message);
  });
}

function onControlPending(message) {
  if (message.pane !== pane) return;
  waitingControl = true;
  updateDriveUi();
  stopControlTimer();
  // The hub drops an unanswered request after a minute without saying so, so the waiting state
  // has to end by itself rather than sit there for ever.
  controlTimer = setTimeout(() => {
    if (!driving && waitingControl) {
      waitingControl = false;
      updateDriveUi();
      note('Nobody answered. You can ask to type again.');
    }
  }, CONTROL_LAPSE + 2000);
}

function handBack() {
  driving = false;
  directKeys = false;
  waitingControl = false;
  stopControlTimer();
  updateDriveUi();
  if (pane) rrp.send({ t: 'control_release', pane }).catch(() => {});
}

// ---- pause (section 10.5) ---------------------------------------------------------------------------

function onShareState(message) {
  const state = stateOf(message.pane);
  state.paused = !!message.paused;
  state.reason = String(message.reason || '');
  if (message.pane && message.pane !== pane) return;
  paused = state.paused;
  pauseReason = state.reason;
  if (paused) {
    driving = false;
    directKeys = false;
    waitingControl = false;
    stopControlTimer();
  }
  updateDriveUi();
  note(paused ? pauseText() : '', HOLD);
}

function pauseText() {
  if (pauseReason === 'away') return 'The owner stepped away, so typing is paused for now.';
  return 'The owner paused guests, so typing is paused for now.';
}

// ---- the editor's controls -------------------------------------------------------------------------
// Built the first time this session is an editor, and never for a viewer: a viewer's page has no
// key handler, no extra-keys row and no prompt box to go wrong.

function buildEditorControls() {
  if (editorBuilt) return;
  editorBuilt = true;
  $('guest-ask').addEventListener('click', askToType);
  $('guest-hand-back').addEventListener('click', handBack);
  $('guest-direct').addEventListener('click', () => setDirectKeys(!directKeys));
  $('guest-capture').addEventListener('keydown', onDirectKey);
  $('guest-capture').addEventListener('input', (event) => { event.target.value = ''; });
  $('guest-capture').addEventListener('paste', (event) => {
    event.preventDefault();
    const text = event.clipboardData?.getData('text') || '';
    if (text && pane && driving) {
      toLive();
      rrp.send({ t: 'paste', pane, text }).catch((error) => note(error.message));
    }
  });
  $('guest-screen-wrap').addEventListener('click', () => {
    if (driving && directKeys) $('guest-capture').focus();
  });
  $('guest-line-send').addEventListener('click', sendLine);
  $('guest-line').addEventListener('keydown', (event) => {
    if (event.key !== 'Enter' || event.shiftKey) return;
    event.preventDefault();
    sendLine();
  });
  $('guest-prompt-send').addEventListener('click', sendPrompt);
  $('guest-prompt-text').addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      sendPrompt();
    }
  });
  $('guest-prompt-text').addEventListener('input', (event) => {
    event.target.style.height = 'auto';
    event.target.style.height = `${Math.min(event.target.scrollHeight, 140)}px`;
  });
  buildKeyRow();
}

function buildKeyRow() {
  const row = $('guest-keys');
  row.replaceChildren();
  const add = (label, action, name) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = label;
    if (name) button.dataset.sticky = name;
    button.addEventListener('click', action);
    row.append(button);
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
  for (const button of $('guest-keys').querySelectorAll('[data-sticky]')) {
    button.classList.toggle('sticky-on', !!sticky[button.dataset.sticky]);
  }
}

function setDirectKeys(on) {
  directKeys = on;
  updateDriveUi();
  if (on) {
    const capture = $('guest-capture');
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
  if (!driving || !pane) return;
  toLive();
  rrp.send({ t: 'keys', pane, bytes: b64(new TextEncoder().encode(text)) })
    .catch((error) => note(error.message));
}

// The line box. What is in it survives losing the keyboard: half a command is still the person's
// to finish, and only a send that left this device clears it.
function sendLine() {
  const box = $('guest-line');
  const text = box.value;
  if (!text || !pane) return;
  if (!driving) {
    note('You are not driving this pane. Ask to type first — what you typed is still here.');
    return;
  }
  if (sticky.ctrl && text.length === 1) {
    const byte = controlByte(text);
    if (byte) sendKeys(byte);
  } else if (sticky.ctrl || sticky.alt) {
    sendKeys(sticky.alt ? `\x1b${text}` : text);
  } else {
    toLive();
    rrp.send({ t: 'line', pane, text }).catch((error) => note(error.message));
  }
  sticky.ctrl = false;
  sticky.alt = false;
  for (const button of $('guest-keys').querySelectorAll('[data-sticky]')) {
    button.classList.remove('sticky-on');
  }
  box.value = '';
}

function updateDriveUi() {
  const editor = role === 'editor';
  $('guest-drive-bar').hidden = !editor;
  $('guest-ask').hidden = !editor || driving || waitingControl || paused;
  $('guest-ask').disabled = paused;
  $('guest-hand-back').hidden = !driving;
  $('guest-direct').hidden = !driving;
  $('guest-direct').classList.toggle('is-on', directKeys);
  $('guest-keys').hidden = !driving;
  // The line box stays while it holds something, even once the keyboard has gone: half a command
  // is still the person's to finish, and hiding it would look exactly like losing it.
  const line = $('guest-line');
  $('guest-line-row').hidden = !(driving || (editor && line.value.trim()));
  line.disabled = !driving;
  $('guest-line-send').disabled = !driving;
  $('guest-capture').hidden = !driving || !directKeys;
  $('guest-composer').hidden = !editor;
  const mode = $('guest-drive-mode');
  mode.textContent = paused ? 'Paused'
    : driving ? (directKeys ? 'Typing directly' : 'You have the keyboard')
    : waitingControl ? 'Asked to type…'
    : 'Watching';
  mode.className = `chip ${driving ? 'ok' : waitingControl || paused ? 'warn' : ''}`;
  const box = $('guest-prompt-text');
  const full = pendingCount() >= MAX_PENDING;
  box.disabled = paused;
  $('guest-prompt-send').disabled = paused || full;
  // Short, because a phone clips a long placeholder: the qualifier is the caption above it.
  box.placeholder = paused ? 'Paused by the owner…'
    : full ? 'Three are already waiting…'
    : 'Ask their agent…';
  if (!editor) {
    note(paused ? pauseText() : 'Watching. Only the owner and their editors can type.');
  }
}

// ---- prompts (section 10.4) ------------------------------------------------------------------------
// A guest's prompt is a request. It goes nowhere until the owner approves it, and the row says so
// at every step: waiting, approved, declined with the reason, or lapsed because nobody answered.

function pendingCount() {
  let count = sent.length;
  for (const item of prompts.values()) if (item.state === 'waiting') count += 1;
  return count;
}

function sendPrompt() {
  const box = $('guest-prompt-text');
  const text = box.value.trim();
  if (!text || !pane) return;
  if (paused) {
    note(pauseText());
    return;
  }
  if (pendingCount() >= MAX_PENDING) {
    note(`Three of your prompts are already waiting for ${ownerName()}. They lapse after ten `
      + 'minutes if nobody answers.');
    return;
  }
  const id = `p${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`;
  const item = newPrompt(text);
  item.id = id;
  sent.push(item);
  // `agent: false` — the composer's shell route — is never sent by a guest and is refused by the
  // hub whatever the role. A guest's words reach the agent or nothing.
  rrp.send({ t: 'compose', pane, text, when: 'queue', id,
             msg_id: b64(crypto.getRandomValues(new Uint8Array(9))) })
    .catch((error) => {
      const at = sent.indexOf(item);
      if (at >= 0) sent.splice(at, 1);
      settle(item, 'declined', error.message || 'That could not be sent.');
    });
  box.value = '';
  box.style.height = 'auto';
  updateDriveUi();
}

function newPrompt(text) {
  const row = el('div', 'guest-prompt is-sending');
  row.append(el('div', 'guest-prompt-text', text));
  const stateEl = el('div', 'guest-prompt-state', 'sending…');
  row.append(stateEl);
  const list = $('guest-prompts');
  list.append(row);
  list.scrollTop = list.scrollHeight;
  return { text, row, stateEl, state: 'sending', id: '', promptId: '', timer: null };
}

function settle(item, kind, text) {
  if (item.timer) clearTimeout(item.timer);
  item.timer = null;
  item.state = kind;
  item.row.className = `guest-prompt is-${kind}`;
  item.stateEl.textContent = text;
  updateDriveUi();
}

function ownerName() {
  return guest?.desktopName || 'the owner';
}

function onPromptPending(message) {
  const item = sent.shift();
  if (!item) return;
  item.promptId = String(message.id || '');
  if (item.promptId) prompts.set(item.promptId, item);
  settle(item, 'waiting', `waiting for ${ownerName()} to approve this`);
  // Ten minutes and a pending prompt is gone on the desktop, silently. The row has to say so
  // itself rather than sit on "waiting" for the rest of the session.
  item.timer = setTimeout(() => {
    if (item.state === 'waiting') {
      settle(item, 'lapsed', 'nobody answered in ten minutes — you can ask again');
    }
  }, PROMPT_LAPSE + 5000);
}

// Why a prompt did not run, as the hub says it, in words a guest can act on. Anything the hub
// grows later falls through to its own text rather than being swallowed.
function declineText(reason) {
  switch (reason) {
    case 'lapsed':
      return 'nobody answered in ten minutes — you can ask again';
    case 'paused':
      return `${ownerName()} paused guests before this could run`;
    case 'removed':
      return 'your access to this pane ended first';
    case 'refused':
      return `${ownerName()} declined this`;
    default:
      return reason ? `${ownerName()} declined this · ${reason}`
        : `${ownerName()} declined this`;
  }
}

function onPromptDecided(message) {
  const item = prompts.get(String(message.id || ''));
  if (!item) return;
  prompts.delete(item.promptId);
  if (message.approved) {
    settle(item, 'approved', `${ownerName()} approved this · it went to their agent`);
    return;
  }
  const reason = String(message.reason || '').trim();
  settle(item, reason === 'lapsed' ? 'lapsed' : 'declined', declineText(reason));
}

// ---- the agent, under the guest's allow-list --------------------------------------------------------
// The turn lifecycle, the pane's status and its queue — nothing else reaches a guest, and the
// agent's words arrive on the terminal screen because Relay prints them into the pane.

function onAgent(message) {
  if (message.pane !== pane) return;
  const event = message.event || {};
  switch (event.event) {
    case 'agent_started':
      note('Their agent is working…');
      break;
    case 'queued':
      // `author` is the hub's own word for whose row this is — "you", another guest's name, or
      // "the owner". The raw `origin` is a participant id and means nothing to a reader.
      note(event.author === 'you' ? 'Your prompt is queued for their agent.'
        : event.author ? `Queued · ${event.author}`
        : 'Queued');
      break;
    case 'agent_finished':
    case 'agent_stopped':
    case 'cancelled':
      note(event.event === 'agent_finished' ? '' : 'Their agent stopped.');
      break;
    case 'status':
      if (event.text) note(event.text);
      break;
    case 'error':
      note(event.message || 'Their agent reported an error.');
      break;
    default:
      break;
  }
}

function onError(detail) {
  const code = String(detail.code || '');
  if (screenView && detail.id && detail.id === historyRequest) {
    screenView.pending = false;
    screenView.more = false;
    return;
  }
  const at = sent.findIndex((item) => item.id && item.id === detail.id);
  if (at >= 0) {
    const [item] = sent.splice(at, 1);
    settle(item, 'declined', detail.message || 'that was refused.');
    return;
  }
  if (code === 'not_driving') {
    driving = false;
    directKeys = false;
    waitingControl = false;
    stopControlTimer();
    updateDriveUi();
    note('Somebody else is driving this pane, so that did not go through.');
    return;
  }
  if (code === 'paused') {
    paused = true;
    updateDriveUi();
    note(pauseText());
    return;
  }
  note(detail.message || 'That was refused.');
}

// ---- wiring ------------------------------------------------------------------------------------------

function wire() {
  if (wired) return;
  wired = true;
  // The app's own header names the desktop this browser is paired with and how it is paired.
  // A guest is paired with nothing, so it goes: their bar is the one inside their own screen,
  // and it says whose computer this is instead.
  $('bar').hidden = true;

  $('join-knock').addEventListener('click', knock);
  addCodeLink();
  $('join-name').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      knock();
    }
  });
  $('knock-cancel').addEventListener('click', () => {
    stopKnockCountdown();
    rrp.close();
    problem('You stopped waiting. Knock again whenever you like.');
  });
  $('guest-new-output').addEventListener('click', () => toLive());
  $('guest-leave').addEventListener('click', async () => {
    if (pane) rrp.send({ t: 'bye', reason: 'left' }).catch(() => {});
    await endSession('You left this shared pane.');
  });

  rrp.addEventListener('authcode', (event) => {
    $('knock-code').textContent = event.detail.code;
  });
  rrp.addEventListener('welcome', (event) => {
    const detail = event.detail || {};
    applySession({ role: detail.role, panes: detail.panes, expires: detail.expires,
                   features: detail.features });
    openPane(pane || panes[0] || '');
  });
  rrp.addEventListener('panes', (event) => {
    paneItems = event.detail.items || [];
    // The hub cuts this list down to this guest's scope, so it *is* the scope. For a tab shared
    // whole that scope moves: a pane the owner adds to the tab arrives here as a new chip, and
    // one closed or moved out of it goes, taking the view with it if it was the one open.
    const ids = paneItems.map((item) => item.id).filter(Boolean);
    if (ids.length) {
      panes = ids;
      if (guest) guest.panes = panes;
      if (pane && !ids.includes(pane)) {
        pane = '';
        openPane(ids[0]);
        return;
      }
    }
    renderPresence();
  });
  rrp.addEventListener('participants', (event) => onParticipants(event.detail));
  rrp.addEventListener('control', (event) => onControl(event.detail));
  rrp.addEventListener('control_pending', (event) => onControlPending(event.detail));
  rrp.addEventListener('prompt_pending', (event) => onPromptPending(event.detail));
  rrp.addEventListener('prompt_decided', (event) => onPromptDecided(event.detail));
  rrp.addEventListener('share_state', (event) => onShareState(event.detail));
  rrp.addEventListener('screen_snapshot', (event) => onScreen(event.detail));
  rrp.addEventListener('screen_diff', (event) => onScreen(event.detail));
  rrp.addEventListener('history', (event) => onHistory(event.detail));
  rrp.addEventListener('agent', (event) => onAgent(event.detail));
  rrp.addEventListener('error', (event) => onError(event.detail || {}));
  rrp.addEventListener('bye', (event) => {
    const detail = event.detail || {};
    if (!detail.discard) return;
    // `discard` is the desktop saying to forget the record rather than retry: removed, expired,
    // or the share was ended. The heading already says it ended; this is the reason, not a
    // second copy of the heading.
    endSession(sentence(detail.reason) || 'Whoever invited you ended your access.');
  });
  rrp.addEventListener('closed', () => {
    if (ended) return;
    setStatus('offline', 'warn');
    scheduleReconnect();
  });
  rrp.addEventListener('fault', (event) => {
    setStatus('dropped', 'warn');
    note(event.detail.message);
  });

  document.addEventListener('visibilitychange', () => {
    if (ended) return;
    if (document.visibilityState === 'visible') {
      if (!rrp.session) scheduleReconnect();
      else rrp.send({ t: 'client_state', visible: true }).catch(() => {});
    } else if (rrp.session) {
      rrp.send({ t: 'client_state', visible: false }).catch(() => {});
    }
  });
}

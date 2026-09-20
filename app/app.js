// SPDX-License-Identifier: AGPL-3.0-or-later
// The phone client: pair, inbox, thread, composer.
//
// Agent output and pane titles are attacker-controlled — a program can print anything and a
// terminal title comes from OSC sequences — so every string from the wire goes in through
// textContent. There is no innerHTML in this file, and the CSP forbids inline script anyway.

import { Rrp, loadDevice, forgetDevice, fingerprint, b64, un64, storedValue, storeValue,
  dropValue } from './rrp.js';
import { ScreenView, KEYS, controlByte, keyEventBytes } from './screen.js';
// A guest invited to one pane of somebody else's desktop is a different session, not this one
// with buttons hidden (docs/REMOTE-PROTOCOL.md section 10). Their client takes the page over and
// nothing below is wired for them; their record is stored apart from this one's, so a person can
// be the owner of one desktop and a guest of another in the same browser.
import { guestRoute, startGuest, startGuestIfInvited } from './guest.js';
// The pane view (docs/REMOTE-PROTOCOL.md section 16): the desktop pane's own queue, reasoning,
// model and prompt box, drawn from the `pane_state` it publishes. Every label in it was written
// by the desktop; this file only carries messages in and out.
import { mountPane } from './pane.js';
// The page is as tall as what is visible, so an on-screen keyboard shrinks it instead of pushing it
// off the screen (owner, 2026-09-18, an iPad in landscape).
import { trackViewport } from './viewport.js';
// Renewing a push subscription made under another rendezvous's VAPID key (section 9).
import { renewIfKeyMoved } from './pushkey.js';
// Queue and replay across a drop (section 7). It sits in the transport this file hands to the
// pane view, so the view itself knows nothing about being offline.
import { Outbox, pendingLine } from './outbox.js';
// The two notification switches and what they mean in the protocol's five kinds (section 9.1).
import { NOTIFY_SWITCHES, ALL_KINDS, switchesFrom, kindsFor } from './notifykinds.js';

trackViewport();

const rrp = new Rrp();
let panes = [];
let current = null;
let reconnectTimer = null;
let connecting = false;   // a handshake is in flight: a second connect would race it
let capability = 'view';
let features = [];
let screenView = null;
let historySeq = 0;
let historyRequest = null;
// Every `history_get` sent and not yet answered or refused, by id. The view keeps one in flight,
// so this normally holds one — but a reset (a clear, a pane change) can leave one orphaned on the
// wire, and matching only the newest id is what put "too many of those; slow down." under the
// composer while the agent was merely typing (#3H5T). An answer that arrives late is still rows
// the desktop really printed, and a refusal is still ours to swallow.
const historyAsked = new Set();
let tab = 'agent';
let driving = false;
let myDevice = '';          // this device's own id, from the pairing record
// Who holds this pane's keyboard, from the last `control` (section 10.3). One holder per pane
// across the owner's own devices, the agent and any guests — this device included, which is why
// it has to be followed rather than assumed from the last button that was tapped.
let holder = 'owner';
let holderName = '';
let holderDevice = '';
let presence = [];          // the `participants` rows for the open pane
let sticky = { ctrl: false, alt: false };
let directKeys = false;
let passwordEntry = false;  // the owner's per-device switch (welcome.password_entry)
let answerNode = null;      // the answer being streamed, without a terminal to print into
let recorder = null;        // the MediaRecorder while a voice clip is being recorded
let voiceRequest = '';      // the id of the clip waiting for the desktop's transcript
const models = new Map();   // pane -> {model, waiting}: the model indicator (issue 3ES1)
// When this device first saw a pane in the status it is in now, so a running pane can say how
// long it has been running even from a desktop that sends no `updated` (see paneSince).
const statusSince = new Map();
let inboxTimer = null;      // repaints the elapsed time on running rows
let pendingOpen = '';       // a pane a notification asked for, before the pane list has arrived

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

// The chip in the header, and what is waiting behind it. The link's own word is kept apart from
// the count so that either can change without the other being recomputed by its caller: the
// count is the one thing that has to stay readable from the inbox, where the line under the
// prompt box is not on screen.
let linkState = { text: 'starting', kind: '' };

function setStatus(text, kind = '') {
  linkState = { text, kind };
  paintStatus();
}

function paintStatus() {
  const chip = $('link-status');
  const waiting = outbox.pending.length;
  chip.textContent = waiting && !linkUp() ? `${linkState.text} · ${waiting} waiting` : linkState.text;
  chip.className = `chip ${linkState.kind}`;
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

// ---- sending, across a drop (section 7) -------------------------------------------------------

// iOS closes the socket within seconds of the app going to the background, so "send" on a phone
// has to mean "send, or hold it until the link is back". Everything this file and the pane view
// send goes through here; `app/outbox.js` holds the rule about which types may wait (a prompt and
// a Stop) and which may never (`keys`, `secret_input`), and `msg_id` is what makes a replay after
// a drop land at most once on the desktop.
const linkUp = () => Boolean(rrp.session);

const outbox = new Outbox({
  send: (message) => rrp.send(message),
  online: linkUp,
  onChange: () => renderOutboxNote(),
});

// A line under the pane, never a sheet: the person is typing on a bus and a modal over the box
// takes the sentence with it.
function renderOutboxNote() {
  const node = $('outbox-note');
  if (!node) return;
  const text = pendingLine(outbox.counts());
  node.textContent = text;
  node.hidden = !text;
  paintStatus();        // and in the header, where it is readable from the inbox too
}

// One place that knows a message may have been kept rather than sent, so every caller — the pane
// view's transport, the client's own composer, Stop — says the same thing about it.
function post(message, onError) {
  return outbox.post(message)
    .then((what) => { if (what === 'queued') renderOutboxNote(); return what; })
    .catch((error) => { if (onError) onError(error); });
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
  myDevice = record.deviceId || '';
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
    // No desktop of your own here. A guest record from an invitation is still somebody's live
    // session, and the app's start_url is '/', so it is checked before offering to pair.
    if (await startGuestIfInvited()) return;
    show('welcome');
    return;
  }
  setStatus('connecting…');
  connecting = true;
  try {
    await rrp.connect(record);
    setStatus('connected', 'ok');
    await afterConnect(record);
  } catch (error) {
    setStatus('offline', 'warn');
    show('inbox');
    $('inbox-empty').textContent = error.message || 'Could not reach your desktop.';
    scheduleReconnect();
  } finally {
    connecting = false;
  }
}

function scheduleReconnect(delay = 3000) {
  if (reconnectTimer) {
    if (delay >= 3000) return;         // one already pending, and this is not more urgent
    clearTimeout(reconnectTimer);      // coming back from the background: do not sit out the wait
  }
  reconnectTimer = setTimeout(async () => {
    reconnectTimer = null;
    const record = await loadDevice();
    // `connecting` as well as `session`: a handshake that has begun has no session yet, and a
    // second `connect` on top of it opens a second channel to the desktop whose frames the first
    // one's cipherstate cannot read. That is one socket per wake-up event, which is what a phone
    // coming out of a pocket produces several of at once.
    if (!record || rrp.session || connecting) return;
    connecting = true;
    try {
      await rrp.connect(record);
      setStatus('connected', 'ok');
      // What was typed while the link was down goes out **after** the resume, and only if the
      // resume itself went out: a `compose` that arrives before the streams are back would be
      // answered into a stream this client has not caught up on. A failed resume keeps it.
      const resumed = await rrp.resume();
      if (current) rrp.send({ t: 'pane_focus', pane: current }).catch(() => {});
      if (resumed) await outbox.flush();
    } catch {
      scheduleReconnect();
    } finally {
      connecting = false;
    }
  }, delay);
}

// The link is back, or the page is: try what is waiting. It does nothing before this device has
// ever connected — during pairing, or on a page that is only being looked at — because there is
// then nothing to come back to and a reconnect would race the first handshake.
function wakeUp() {
  if (!rrp.record) return;
  if (!rrp.session) scheduleReconnect(0);
  else outbox.flush();
}

// ---- inbox ------------------------------------------------------------------------------------

// The five statuses of section 6.3, in the words the inbox uses. Lower case, because the chip is
// read as part of the row rather than as a heading, and because "Waiting for input" is the
// desktop's own phrasing of a machine state while the phone's question is only ever whether this
// one wants you.
const STATUS_LABEL = {
  idle: 'idle', running: 'running', waiting_input: 'waiting for you',
  password: 'password', finished: 'finished', failed: 'failed',
};

// The three that are a reason to take the phone out of your pocket. `waiting_input` is
// best-effort (section 6.3) and `password` may be a program the agent started, but both are
// things only the person can answer, and a failed turn is one nobody else will pick up.
const NEEDS_YOU = ['waiting_input', 'password', 'failed'];

function needsYou(pane) {
  return NEEDS_YOU.includes(pane.status);
}

// A pane's place in the list: what needs you, then what is working, then the rest. Within a band
// the desktop's own order stands — it is the order of the panes on the screen the person knows,
// and re-sorting it by "most recently changed" makes rows swap places under a thumb.
function band(pane) {
  if (needsYou(pane)) return 0;
  if (pane.status === 'running') return 1;
  return 2;
}

// When this pane entered the status it is in, as a millisecond clock, or 0 when nothing knows.
//
// `updated` is on the wire for a pane (section 6.3 via remote/panes.py) and is the desktop's own
// answer, so it is preferred — but the GUI bridge does not fill it in yet (`RemoteShare::sendPane`
// sends no `updated`, so `remote/gui_host.py` stores 0), and a wrong epoch would print "running ·
// 20204h". So it is used only when it is a plausible recent wall-clock second, and otherwise the
// moment this device first saw the status, which is right from the phone's point of view: it has
// been running at least that long.
const UPDATED_SANE_MS = 7 * 24 * 3600 * 1000;

function paneSince(pane) {
  const updated = Number(pane.updated) || 0;
  const now = Date.now();
  if (updated > 0) {
    const millis = updated > 1e12 ? updated : updated * 1000;   // seconds or already millis
    if (millis <= now + 60000 && now - millis < UPDATED_SANE_MS) return millis;
  }
  const seen = statusSince.get(pane.id);
  return seen && seen.status === pane.status ? seen.at : 0;
}

// "3m", "1h 4m", "18s" — the same ladder remote/notify.py's `elapsed_text` uses on a lock screen,
// so a push and the inbox row it belongs to read the same.
function elapsedText(millis) {
  const seconds = Math.max(0, Math.floor(millis / 1000));
  if (seconds >= 3600) return `${Math.floor(seconds / 3600)}h ${Math.floor(seconds % 3600 / 60)}m`;
  if (seconds >= 60) return `${Math.floor(seconds / 60)}m`;
  return `${seconds}s`;
}

function statusText(pane) {
  const label = STATUS_LABEL[pane.status] || pane.status || 'idle';
  if (pane.status !== 'running') return label;
  const since = paneSince(pane);
  return since ? `${label} · ${elapsedText(Date.now() - since)}` : label;
}

// Remember when each pane entered its current status, and forget the panes that have gone.
function trackStatuses() {
  const live = new Set();
  for (const pane of panes) {
    live.add(pane.id);
    const seen = statusSince.get(pane.id);
    if (!seen || seen.status !== pane.status) {
      statusSince.set(pane.id, { status: pane.status, at: Date.now() });
    }
  }
  for (const id of [...statusSince.keys()]) if (!live.has(id)) statusSince.delete(id);
}

// The count on the app icon: how many panes want you. Guarded on every side — the Badging API is
// Chrome and an installed PWA only, `setAppBadge` rejects rather than throwing in some versions,
// and a browser that has it may still refuse an unpinned page.
function updateBadge(count) {
  try {
    if (count > 0) navigator.setAppBadge?.(count)?.catch?.(() => {});
    else navigator.clearAppBadge?.()?.catch?.(() => {});
  } catch {
    /* a browser without the Badging API: the chip in the list is the whole story there */
  }
}

function renderInbox() {
  const list = $('pane-list');
  list.replaceChildren();
  trackStatuses();
  // Decorated, so the tie-break is the desktop's order and not the engine's idea of a stable sort.
  const sorted = panes.map((pane, at) => ({ pane, at }))
    .sort((a, b) => (band(a.pane) - band(b.pane)) || (a.at - b.at))
    .map((entry) => entry.pane);
  for (const pane of sorted) {
    const row = el('button', 'pane-row');
    row.type = 'button';
    row.dataset.paneId = pane.id;
    const head = el('div', 'pane-head');
    head.append(el('span', 'pane-title', pane.title || pane.id));
    const chip = el('span', `chip status-${pane.status}`, statusText(pane));
    chip.dataset.paneChip = pane.id;
    head.append(chip);
    row.append(head);
    row.append(el('div', 'pane-cwd', pane.cwd || ''));
    if (pane.queue) row.append(el('div', 'pane-queue', `${pane.queue} queued`));
    row.addEventListener('click', () => openPane(pane.id));
    list.append(row);
  }
  $('inbox-empty').textContent = panes.length ? '' : 'No panes yet.';
  updateBadge(panes.filter(needsYou).length);
  // A running pane's elapsed time is the one thing in the list that changes without a message
  // from the desktop, so it is ticked here rather than left to say "running · 0s" for an hour.
  if (inboxTimer) clearInterval(inboxTimer);
  inboxTimer = panes.some((pane) => pane.status === 'running')
    ? setInterval(tickElapsed, 15000) : null;
}

// Only the chips move: repainting the list would take the row out from under a thumb.
function tickElapsed() {
  for (const pane of panes) {
    if (pane.status !== 'running') continue;
    const chip = document.querySelector(`[data-pane-chip="${CSS.escape(pane.id)}"]`);
    if (chip) chip.textContent = statusText(pane);
  }
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
// Two switches, not five checkboxes: `app/notifykinds.js` holds the mapping onto section 9.1's
// five kinds and why it is the split it is. This file only draws it.
let notifyKinds = null;                    // what the desktop last told us it has stored

function renderNotifyKinds(on) {
  const list = $('notify-kinds');
  list.replaceChildren();
  list.hidden = !on;
  if (!on) return;
  const state = switchesFrom(notifyKinds);
  for (const group of NOTIFY_SWITCHES) {
    const row = el('div', 'notify-switch');     // a div, so each sits on its own line
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.setAttribute('role', 'switch');
    box.id = `notify-switch-${group.name}`;
    box.checked = state[group.name];
    box.dataset.kinds = group.kinds.join(' ');
    box.addEventListener('change', chooseNotifyKinds);
    const text = el('label', 'notify-switch-label', group.label);
    text.htmlFor = box.id;
    row.append(box, text);
    list.append(row);
  }
}

async function chooseNotifyKinds() {
  const chosen = kindsFor(Object.fromEntries(NOTIFY_SWITCHES.map((group) =>
    [group.name, !!$(`notify-switch-${group.name}`)?.checked])));
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
  // iOS delivers Web Push only to a PWA on the Home Screen. In Safari it usually has no
  // PushManager at all — no error, no prompt — but some iPadOS versions expose one that then
  // refuses, so the check is "not installed on iOS", not "no PushManager": what would follow is a
  // permission sheet that cannot lead anywhere. One line saying what to do instead, and no button
  // to tap that will fail.
  $('notify-kinds').hidden = true;
  if (IOS && !installedApp()) {
    button.hidden = true;
    note.textContent = 'To be notified on iPhone or iPad, add Relay to your Home Screen first: '
      + 'tap the Share button, choose “Add to Home Screen”, and open it from there. iOS only '
      + 'delivers notifications to an installed app.';
    return;
  }
  if (!pushUsable()) {
    button.hidden = true;
    note.textContent = 'This browser cannot show notifications.';
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
  await storeValue('push-vapid', vapid);   // which rendezvous's key, for renewPushIfMoved
  // The seal key: generated here, kept where the service worker reads it, and sent to the
  // desktop and nowhere else.
  const key = await crypto.subtle.generateKey({ name: 'AES-GCM', length: 256 }, true,
    ['encrypt', 'decrypt']);
  await storeValue('push-key', key);
  // Everything on to begin with: one tap turns off what you did not want, and a notification you
  // never saw is not something you can decide about.
  await sendSubscription(subscription, (await storedValue('push-kinds'))
    || ALL_KINDS);
}

async function disableNotifications() {
  const subscription = await currentSubscription();
  if (subscription) await subscription.unsubscribe();
  await dropValue('push-key');
  await dropValue('push-kinds');
  await dropValue('push-vapid');
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

// The desktop pushes this phone through the rendezvous it subscribed through. When it is reached
// through another one now (its owner moved it to relay-terminal.ai, or back), the key this
// subscription was made under is not that rendezvous's: renew it under the new key, with the same
// seal key and kinds, and send it again, which moves this device's push origin on the desktop.
// Permission is already granted, so this asks nothing; any failure leaves the row to say so.
async function renewPushIfMoved() {
  if (!pushUsable() || Notification.permission !== 'granted') return;
  const subscription = await currentSubscription();
  if (!subscription || !(await storedValue('push-key'))) return;
  await renewIfKeyMoved({
    subscription,
    stored: await storedValue('push-vapid'),
    fetchVapid: async () => (await (await fetch(`${rrp.origin}/v1/push/key`)).json()).vapid,
    subscribe: async (key) => {
      await subscription.unsubscribe();      // a browser refuses a second key otherwise
      const registration = await navigator.serviceWorker.ready;
      return registration.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: key });
    },
    report: async (fresh, vapid) => {
      await storeValue('push-vapid', vapid);
      await sendSubscription(fresh, (await storedValue('push-kinds'))
        || ALL_KINDS);
    },
  });
}

// ---- thread -----------------------------------------------------------------------------------

// A line under the terminal, where a refusal or a dropped link is actually read: #thread-body is
// the fallback transcript and is hidden whenever the pane has a screen, which is the normal case
// (card #W5N2's owner, 2026-09-18). Both callers used to reach for an append() that never existed.
function threadNote(text, isError = false) {
  const node = $('thread-note');
  node.textContent = text || '';
  node.classList.toggle('error', !!text && isError);
}

// ---- the pane view ------------------------------------------------------------------------

let paneView = null;

// Mounted on the first pane_state for a pane and torn down when the pane closes, so a device that
// is only watching a terminal (or a guest, who is never sent pane_state) is unchanged.
function ensurePaneView() {
  if (paneView) return paneView;
  paneView = mountPane($('pane-view'), {
    // The view's whole transport. It never learns that the link can be down: a prompt it hands
    // over while the socket is dead is kept here and goes out on the next resume, and the line
    // under the pane says so (section 7, app/outbox.js).
    send: (message) => { post(message); },
  });
  $('pane-view').hidden = false;   // mountPane has already appended its root here
  // The terminal belongs inside the view, above the reasoning and the queue, exactly as the
  // desktop pane stacks them. It keeps working where it is if the view is never mounted.
  paneView.terminalSlot.append($('terminal-pane'));
  // The view has the pane's own prompt box, with the desktop's placeholder, its mode chip and its
  // three-way send. The client's older one would be a second composer under it saying the same
  // thing, so it stands down while the view is up (the voice button moves, see below).
  $('composer').hidden = true;
  // …except the microphone, which is the client's own and has no equivalent in the pane: it moves
  // into the strip beside the model, so voice still works while the view is up.
  paneView.hostSlot.append($('composer-mic'));
  return paneView;
}

function closePaneView() {
  if (!paneView) return;
  $('screen-thread').insertBefore($('terminal-pane'), $('thread-note'));   // back where it was
  $('composer').insertBefore($('composer-mic'), $('composer-stop'));   // back where it was
  paneView.destroy();
  paneView = null;
  $('composer').hidden = false;
  $('pane-view').hidden = true;
  $('pane-view').replaceChildren();
}

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
  holder = 'owner';
  holderName = '';
  holderDevice = '';
  presence = [];
  renderPresence();
  openTerminal();
  show('thread');
  rrp.send({ t: 'pane_focus', pane: paneId }).catch(() => {});
  // Ask for this pane's state, when the desktop says it publishes one. Without the feature the
  // view is never mounted and the terminal stays where it is.
  if (features.includes('pane_state')) rrp.send({ t: 'pane_state_get', pane: paneId }).catch(() => {});
}

// A notification was tapped, on this pane (section 9.2: the sealed body carries the pane id and
// nothing else that names it). The pane list may not have arrived yet — the tap is usually what
// woke the app — so the id is held until it does, and the thread opens then rather than on an
// empty title.
function requestOpenPane(paneId) {
  if (!paneId) return;
  if (panes.some((pane) => pane.id === paneId)) {
    pendingOpen = '';
    openPane(paneId);
    return;
  }
  pendingOpen = paneId;
}

function closePane() {
  if (current) {
    if (driving) rrp.send({ t: 'control_release', pane: current }).catch(() => {});
    rrp.send({ t: 'pane_blur', pane: current }).catch(() => {});
  }
  closePaneView();
  current = null;
  driving = false;
  presence = [];
  renderPresence();
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
      historyAsked.add(historyRequest);
      // A handful at most: the orphans above, not a log.
      while (historyAsked.size > 4) historyAsked.delete(historyAsked.values().next().value);
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
  if (message.id && !historyAsked.has(message.id)) return;
  historyAsked.delete(message.id);
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
  // The pane view has the pane's own prompt box, so the client's older one stays down while the
  // view is up — otherwise a device gets two boxes that both say "send" (found by #W5N2's QA).
  $('composer').hidden = !!paneView || !canCompose || (driving && directKeys);
  updateVoiceUi();
  $('composer-text').placeholder = driving ? 'Type a line for the program…' : 'Ask or run…';
  if (!allowed) {
    $('term-note').textContent = 'This device is paired for viewing only.';
  } else if (!driving) {
    // Said here rather than once when the handoff arrives, because the row is repainted from the
    // pane list about once a second and a note written beside it would not survive.
    $('term-note').textContent = someoneElseHasIt()
      ? `${holderLabel()} has the keyboard.` : 'Read only until you take over.';
  } else {
    $('term-note').textContent = '';
  }
}

// Somebody other than this device holds the pane: the desktop itself, the agent, a guest, or
// another phone of the owner's. "Nobody" is never the answer — the desktop holds it by default.
function someoneElseHasIt() {
  if (driving) return false;
  if (holder === 'agent' || holder.startsWith('participant:')) return true;
  return holder === 'owner' && !!holderDevice && holderDevice !== myDevice;
}

function holderLabel() {
  if (holder === 'agent') return 'The agent';
  if (holder.startsWith('participant:')) return holderName || 'A guest';
  return holderName || 'The desktop';
}

// `control` (section 10.3): one holder per pane, and this device is it only when the pane is held
// by one of the owner's devices and that device is this one — a guest sees `owner` for the whole
// of the owner's side, so the id is what tells the phone its own take-over from the desktop's.
// Whatever is half-typed stays in the box: losing the keyboard must not lose the words, which is
// the rule the guest client already follows.
function onControl(message) {
  if (!message || !current || message.pane !== current) return;
  holder = String(message.holder || 'owner');
  holderName = String(message.name || '');
  holderDevice = String(message.device || '');
  const held = holder === 'owner' && !!holderDevice && holderDevice === myDevice;
  const lost = driving && !held;
  driving = held;
  if (!driving) directKeys = false;
  updateDriveUi();
  if (lost) {
    threadNote(holder === 'owner' && !holderDevice
      ? 'The desktop took the keyboard back. What you typed is still here.'
      : `${holderLabel()} has the keyboard now. What you typed is still here.`);
  }
  renderPresence();
}

// Who else is on this pane (section 10.3). One line under the terminal: the desktop's guests,
// what each of them is doing, and nothing about the owner's own devices — a device is not a
// participant, so every row here is somebody else.
function renderPresence() {
  const row = $('term-presence');
  if (!row) return;
  const words = presence.map((item) => {
    const name = item.name || 'someone';
    if (item.driving) return `${name} is typing`;
    if (item.online === false) return `${name} is away`;
    return `${name} is watching`;
  });
  row.textContent = words.join(' · ');
  row.hidden = words.length === 0;
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
      const plan = el('div', 'plan');
      plan.append(el('div', 'plan-title', event.title || 'Plan'));
      plan.append(el('pre', 'plan-body', event.preview || ''));
      const run = el('button', 'plan-run', 'Execute');
      run.type = 'button';
      run.addEventListener('click', () => {
        run.disabled = true;
        // By id, never by path: the desktop minted it and resolves it against its own table.
        rrp.send({ t: 'plan_execute', pane: current, plan_id: event.plan_id }).catch(() => {});
      });
      plan.append(run);
      body.append(plan);
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
  if (paneView) {
    // The pane view's box is the one on screen; the client's is hidden behind it.
    paneView.appendText(text);
    voiceNote('Transcribed · check it, then send.');
    return;
  }
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
  // Through the outbox: offline, this is kept with its `msg_id` and goes out once on resume,
  // rather than being rejected into a `catch` the person never sees.
  post({
    t: 'compose', pane: current, text, when: running ? 'queue' : 'now',
    // `agent: false` asks the desktop to route it, which only a device trusted with typing may
    // do. Anything else reaches the agent and nothing more.
    ...(capability === 'full' ? { agent: false } : {}),
    msg_id: b64(crypto.getRandomValues(new Uint8Array(9))),
  }, fail);
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

rrp.addEventListener('welcome', () => {
  renewPushIfMoved().catch(() => {}).finally(() => updateNotifyRow().catch(() => {}));
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
  if (pendingOpen && panes.some((pane) => pane.id === pendingOpen)) {
    const wanted = pendingOpen;
    pendingOpen = '';
    if (wanted !== current) openPane(wanted);
  }
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

rrp.addEventListener('control', (event) => onControl(event.detail));

rrp.addEventListener('participants', (event) => {
  const message = event.detail || {};
  if (!current || message.pane !== current) return;
  presence = Array.isArray(message.items) ? message.items : [];
  renderPresence();
});

// pane_state and the text of a row taken back for editing (section 16). Both name a pane; one
// for a pane this device is not looking at is ignored rather than drawn over the open one.
rrp.addEventListener('pane_state', (event) => {
  const message = event.detail || {};
  if (!current || message.pane !== current) return;
  ensurePaneView().update(message);
});

rrp.addEventListener('queue_edit_text', (event) => {
  const message = event.detail || {};
  if (!paneView || !current || message.pane !== current) return;
  paneView.onEditText(message);
});

rrp.addEventListener('error', (event) => {
  const detail = event.detail || {};
  if (voiceRequest && detail.id === voiceRequest) { voiceFailed(detail.message); return; }
  // A refused page must not leave the view waiting for one for ever; a later drag asks again.
  // It must not reach the reader either: `rate_limited` on a `history_get` means this client
  // asked faster than the desktop's budget, which is a bug here and nothing the person can do
  // anything about. It goes to the console, the view backs off, and the composer stays clean.
  if (screenView && detail.id && historyAsked.has(detail.id)) {
    historyAsked.delete(detail.id);
    console.warn(`history_get refused (${detail.code || 'error'})`);
    screenView.refused(detail.code);
    return;
  }
  // A refused `queue_edit` belongs to the pane view: it has the row and the keys typed on it, and
  // says so over the terminal where the note below cannot be read (see threadNote).
  if (paneView && paneView.onRefused(detail)) return;
  if (current) threadNote(detail.message || 'Refused.', true);
});

rrp.addEventListener('revoked', async () => {
  await forgetDevice();
  outbox.clear();            // nothing staged for a desktop that will not take it
  setStatus('revoked', 'warn');
  show('welcome');
  $('welcome-note').textContent = 'This device was revoked from the desktop.';
});

rrp.addEventListener('closed', (event) => {
  setStatus('offline', 'warn');
  // Stop stays where it is. Hiding it here meant that the moment the socket dropped — which on
  // iOS is seconds after the app leaves the foreground — the one button whose whole purpose is
  // "stop it now" was the one button that had gone. The turn it belongs to is still running on
  // the desktop; tapping it queues the stop, and the desktop applies it once (section 7).
  if (!event.detail.clean) scheduleReconnect();
});

rrp.addEventListener('fault', (event) => {
  setStatus('dropped', 'warn');
  threadNote(event.detail.message, true);
});

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') {
    if (!rrp.session) scheduleReconnect(0);
    else {
      rrp.send({ t: 'client_state', visible: true }).catch(() => {});
      outbox.flush();
    }
  } else if (rrp.session) {
    rrp.send({ t: 'client_state', visible: false }).catch(() => {});
  }
});

// `visibilitychange` is not enough on a phone. Safari restores this page from the back/forward
// cache — from the app switcher, from a back gesture, from a notification tap onto a page that
// was never unloaded — and the page comes back with its JavaScript state intact and its
// WebSocket long dead, without firing `visibilitychange` at all. `pageshow` with `persisted` is
// the event that says so. And a phone that has just walked back into signal fires `online` while
// it was visible the whole time.
window.addEventListener('pageshow', (event) => { if (event.persisted) wakeUp(); });
window.addEventListener('online', () => wakeUp());

window.addEventListener('DOMContentLoaded', () => {
  // An invite link lands on /join, and that is where a guest's session stays. Handing the page
  // over here, before anything else is wired, is what makes "a guest never even constructs those
  // handlers" true rather than a claim about a stylesheet: the composer's routing, the password
  // field, the microphone, notifications and the unpair button below are never connected at all.
  if (guestRoute()) {
    startGuest();
    return;
  }
  // A notification tapped while the app was not running opens it with `?pane=…`: a window that
  // has only just been created has no `message` handler yet, so the id comes in the URL instead
  // and the query is dropped again so a reload does not re-open it.
  const asked = new URLSearchParams(location.search).get('pane');
  if (asked) {
    pendingOpen = asked;
    history.replaceState(null, '', location.pathname + location.hash);
  }
  renderOutboxNote();
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
    // A Stop waits for the link the same way a prompt does: tapping it on a bus means "stop it",
    // not "stop it if the socket happens to be up". The `msg_id` keeps a replayed one harmless.
    post({ t: 'agent_stop', pane: current });
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
  // Both of these set the local flag as well as sending: the desktop's `control` is what makes
  // it true, and it follows immediately, but the button must not look dead until it arrives.
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
    outbox.clear();
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
  // Tapping a notification opens that pane. The worker focuses this page and posts the pane id
  // (app/sw.js `notificationclick`); it is only an id, and one this client already has in its
  // pane list, so nothing about the notification is drawn — the thread it opens is built from
  // the desktop's own `panes` and `pane_state` as it always is.
  navigator.serviceWorker.addEventListener('message', (event) => {
    const data = event.data;
    if (data && data.t === 'open_pane' && typeof data.pane === 'string') {
      requestOpenPane(data.pane);
    }
  });
}

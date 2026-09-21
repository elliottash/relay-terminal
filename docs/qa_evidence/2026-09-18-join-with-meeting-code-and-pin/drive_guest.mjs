// SPDX-License-Identifier: AGPL-3.0-or-later
// The guest half of the #97EG live drive: a real Chrome on the join page a Relay desktop's own
// sidecar serves, driven over the DevTools protocol exactly as a friend would use it — "Have a
// meeting code instead?", the four letters, the four digits, Join, a name, Knock. Prints one
// compact JSON line per stage so live.sh can orchestrate the desktop half:
//
//   {"stage":"form"}                        the meeting-code form is up
//   {"stage":"invited"}                     the sealed invite opened into the ordinary invitation screen
//   {"stage":"knocking","code":"12345"}     waiting on the owner; the five digits are on screen
//   {"stage":"joined","status":"connected"} the owner admitted this guest
//   {"stage":"refused"|"error", ...}        exit 2/3
//
//   node drive_guest.mjs <chrome user-data-dir> <join url> <CODE> <PIN> <name> [shot prefix]
//
// With a shot prefix, each stage also writes <prefix><stage>.png (Page.captureScreenshot), the
// guest half's evidence: X captures of a Chrome window on a bare Xvfb come out blank.
import { readFileSync, writeFileSync } from 'node:fs';

const [userDataDir, joinUrl, code, pin, guestName, shotPrefix] = process.argv.slice(2);
const say = (obj) => console.log(JSON.stringify(obj));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let ws = null;
let seq = 0;
const pending = new Map();

async function connect() {
  for (const p of pending.values()) p(new Error('the page dropped the DevTools socket'));
  pending.clear();
  const [port] = readFileSync(`${userDataDir}/DevToolsActivePort`, 'utf8').split('\n');
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = targets.find((t) => t.type === 'page' && t.url.startsWith(joinUrl))
    || targets.find((t) => t.type === 'page');
  if (!target) throw new Error('no page target');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  ws.onmessage = (event) => {
    const m = JSON.parse(event.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result ? m : m); pending.delete(m.id); }
  };
  ws.onclose = () => { for (const p of pending.values()) p(new Error('socket closed')); pending.clear(); };
  await send('Runtime.enable');
await send('Page.enable').catch(() => {});
}

function send(method, params = {}) {
  return new Promise((res, rej) => {
    const id = ++seq;
    pending.set(id, (m) => (m instanceof Error ? rej(m) : res(m)));
    ws.send(JSON.stringify({ id, method, params }));
  });
}

async function evaluate(expression) {
  const r = await send('Runtime.evaluate', { expression, returnByValue: true });
  if (r.result?.exceptionDetails) {
    throw new Error(`page evaluation failed: ${r.result.exceptionDetails.text}`);
  }
  return r.result?.result?.value;
}

async function shot(name) {
  if (!shotPrefix) return;
  try {
    const r = await send('Page.captureScreenshot', { format: 'png' });
    if (r.result?.data) writeFileSync(`${shotPrefix}${name}.png`, Buffer.from(r.result.data, 'base64'));
  } catch { /* a shot is evidence, never the thing under test */ }
}

// The page can swap its DevTools target when the session begins; ride over it.
async function js(expression) {
  let last;
  for (let attempt = 0; attempt < 4; attempt++) {
    try {
      return await evaluate(expression);
    } catch (error) {
      last = error;
      await sleep(400);
      try { await connect(); } catch { /* next attempt retries */ }
    }
  }
  throw last;
}

await connect();

const PROBE = `(() => {
  const vis = (id) => { const el = document.getElementById(id); return !!el && !el.hidden; };
  const text = (id) => (document.getElementById(id)?.textContent || '').trim();
  return JSON.stringify({
    ready: document.readyState,
    screens: ['screen-welcome', 'screen-pair', 'screen-join', 'screen-meet', 'screen-knock',
              'screen-guest', 'screen-ended'].filter(vis),
    codeLink: !!document.getElementById('join-by-code'),
    meetForm: !!document.getElementById('meet-form'),
    meetNote: text('meet-note'),
    joinNote: text('join-note'),
    knock: text('knock-code'),
    guestStatus: text('guest-status'),
    body: document.body.innerText.slice(0, 300),
  });
})()`;
const probe = async () => JSON.parse(await js(PROBE));

async function waitFor(label, pred, seconds) {
  for (let i = 0; i < seconds * 2; i++) {
    let s;
    try { s = await probe(); } catch { await sleep(500); continue; }
    if (pred(s)) return s;
    await sleep(500);
  }
  let last = {};
  try { last = await probe(); } catch { /* reported empty */ }
  say({ stage: 'error', note: `timed out waiting for ${label}`, last });
  process.exit(2);
}

// The app shell is up (any screen showing, or the code link on the invitation screen).
await waitFor('the app shell', (x) => x.ready === 'complete' && (x.screens.length > 0 || x.codeLink), 30);

// "Have a meeting code instead?", the quiet way from the invitation screen to the code form.
await waitFor('the code link', (x) => x.codeLink || x.meetForm, 30);
await js(`(() => { const b = document.getElementById('join-by-code');
  if (b) b.click(); return true; })()`);
await waitFor('the meeting-code form', (x) => x.screens.includes('screen-meet') && x.meetForm, 15);
say({ stage: 'form' });
await shot('02-guest-code-form');

// The two secrets, set the way typing sets them: focus, value, an input event.
await js(`(() => {
  const set = (id, v) => {
    const el = document.getElementById(id);
    el.focus();
    el.value = v;
    el.dispatchEvent(new Event('input', { bubbles: true }));
  };
  set('meet-code', ${JSON.stringify(code)});
  set('meet-pin', ${JSON.stringify(pin)});
  document.getElementById('meet-join').click();
  return true;
})()`);

// CPace against the desktop, then the sealed invite opens into the ordinary invitation screen —
// or the form's note says why not (wrong PIN, unknown/burned/expired code).
const s = await waitFor('the invitation screen', (x) =>
  (x.screens.includes('screen-join') && !x.screens.includes('screen-meet'))
  || (x.meetNote && !x.meetNote.startsWith('Checking')), 90);
if (!s.screens.includes('screen-join')) {
  say({ stage: 'error', note: s.meetNote || 'the code phase did not reach the invitation screen', last: s });
  process.exit(2);
}
say({ stage: 'invited' });

await js(`(() => {
  const name = document.getElementById('join-name');
  name.focus();
  name.value = ${JSON.stringify(guestName)};
  name.dispatchEvent(new Event('input', { bubbles: true }));
  document.getElementById('join-knock').click();
  return true;
})()`);

const k = await waitFor('the knock screen', (x) =>
  (x.screens.includes('screen-knock') && /^[0-9]{5}$/.test(x.knock))
  || (x.joinNote && !x.screens.includes('screen-meet') && !x.screens.includes('screen-knock')), 30);
if (!k.screens.includes('screen-knock')) {
  say({ stage: 'error', note: k.joinNote || 'no knock screen', last: k });
  process.exit(2);
}
say({ stage: 'knocking', code: k.knock });
await shot('03-guest-waiting-to-be-let-in');

// The owner's answer: admitted to the guest screen, or told no.
const a = await waitFor("the owner's answer", (x) =>
  x.screens.includes('screen-guest') || x.screens.includes('screen-ended'), 180);
if (a.screens.includes('screen-ended')) {
  say({ stage: 'refused', last: a });
  process.exit(3);
}
say({ stage: 'joined', status: a.guestStatus });
await shot('04-guest-joined');
process.exit(0);

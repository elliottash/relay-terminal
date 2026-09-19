// SPDX-License-Identifier: AGPL-3.0-or-later
// The browser's code phase (app/meet.js), driven by tests/test_web_meet_code.py (card #97EG).
//
// Two modes:
//
//   node meet_code_peer.mjs cases
//     Runs app/meet.js's guest side against a desktop written here from the card's wire spec, one
//     scenario per case, and prints what each one came to as JSON.
//
//   node meet_code_peer.mjs wire '{"code":…,"pin":…,"room":…}'
//     Runs the guest side with its frames on stdout and the desktop's on stdin, one JSON object
//     per line, so the Python test can be the desktop with remote/cpace.py. The last line out is
//     {"done": …}.
import { createInterface } from 'node:readline';
import { CPace } from '../app/cpace.js';
import { b64, un64 } from '../app/rrp.js';
import {
  cleanCode, cleanPin, codePhase, lookupCode, meetKeys, meetProblem, MeetError, openCodeRoom,
} from '../app/meet.js';

const utf8 = (text) => new TextEncoder().encode(text);

// ---- a desktop, from the card ------------------------------------------------------------------

// The responder's half: answer meet_a with meet_b, check meet_confirm, seal the fragment. `tweak`
// lets a case misbehave in one specific way.
function desktop({ code, pin, room, fragment, tweak = {} }) {
  const seen = [];
  let cpace = null;
  let keys = null;
  let confirmed = null;
  return {
    seen,
    get confirmed() { return confirmed; },
    async answer(message) {
      seen.push(message.t);
      if (tweak.error) return { t: 'meet_error', error: tweak.error };
      if (message.t === 'meet_a') {
        cpace = await CPace.create({ prs: utf8(pin), ci: utf8('relay/meet/v1'), sid: utf8(room),
                                     initiator: false, ad: utf8('desktop') });
        const ya = un64(message.y);
        const isk = await cpace.finish(ya, utf8('guest'));
        keys = await meetKeys(isk, ya, cpace.message);
        const tag = keys.tagB.slice();
        if (tweak.flipTag) tag[0] ^= 1;
        const y = tweak.zeroPoint ? new Uint8Array(32) : cpace.message;
        if (tweak.silent) return null;
        return { t: 'meet_b', y: b64(y), tag: b64(tag) };
      }
      if (message.t === 'meet_confirm') {
        confirmed = b64(keys.tagA) === message.tag;
        if (!confirmed) return { t: 'meet_error', error: 'wrong_pin' };
        const key = await crypto.subtle.importKey('raw', keys.sealKey, 'AES-GCM', false, ['encrypt']);
        const nonce = crypto.getRandomValues(new Uint8Array(12));
        const aad = utf8(tweak.aad ?? code);
        const sealed = new Uint8Array(await crypto.subtle.encrypt(
          { name: 'AES-GCM', iv: nonce, additionalData: aad }, key, utf8(fragment)));
        return { t: 'meet_invite', nonce: b64(nonce), sealed: b64(sealed) };
      }
      return null;
    },
  };
}

// A channel with the desktop at the other end, in memory.
function pipe(peer) {
  const queue = [];
  const waiting = [];
  const deliver = (message) => {
    if (waiting.length) waiting.shift()(message);
    else queue.push(message);
  };
  const sent = [];
  return {
    sent,
    async send(message) {
      sent.push(JSON.stringify(message));
      const reply = await peer.answer(message);
      if (reply) deliver(reply);
    },
    next(timeout) {
      if (queue.length) return Promise.resolve(queue.shift());
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new MeetError('no_answer')), timeout);
        waiting.push((message) => { clearTimeout(timer); resolve(message); });
      });
    },
  };
}

const FRAGMENT = 'v=1&d=' + b64(new Uint8Array(32).fill(0x33)) + '&i=' + b64(new Uint8Array(16).fill(0x44))
  + '&r=invite-room-7';

async function scenario({ pin = '4829', desktopPin = '4829', code = 'BQRT', room = 'code-room-1',
                          tweak = {}, wait = 2000 } = {}) {
  const peer = desktop({ code, pin: desktopPin, room, fragment: FRAGMENT, tweak });
  const channel = pipe(peer);
  const out = { sent: [], seen: peer.seen };
  try {
    out.fragment = await codePhase({ code, pin, room, channel, wait });
    out.ok = true;
  } catch (error) {
    out.ok = false;
    out.kind = error instanceof MeetError ? error.kind : `unexpected: ${error}`;
    out.sentence = meetProblem(error);
  }
  out.sent = channel.sent;
  out.confirmed = peer.confirmed;
  out.expected = FRAGMENT;
  return out;
}

async function lookups() {
  const asked = [];
  const answer = (status, body) => async (url) => {
    asked.push(url);
    return { status, ok: status >= 200 && status < 300, json: async () => body };
  };
  const result = {};
  for (const [name, status, body] of [['found', 200, { room: 'code-room-1' }],
                                      ['missing', 404, {}], ['limited', 429, {}],
                                      ['broken', 500, {}], ['empty', 200, {}]]) {
    try {
      result[name] = { room: await lookupCode('BQRT', { origin: 'https://relay.example',
                                                        fetcher: answer(status, body) }) };
    } catch (error) {
      result[name] = { kind: error.kind };
    }
  }
  try {
    await lookupCode('BQRT', { origin: 'https://relay.example',
                               fetcher: async () => { throw new TypeError('offline'); } });
  } catch (error) {
    result.offline = { kind: error.kind };
  }
  result.asked = asked;
  return result;
}

// A rendezvous that accepts the connection and then says nothing: the socket fires no `open`, no
// `error` and no `close`, which is the case that used to hang the join for ever. `openCodeRoom`
// has its own deadline, so it comes back `unreachable` — the sentence that ends "try again", with
// the form enabled again behind it (app/guest.js).
async function connectTimeout() {
  const sockets = [];
  class DeadSocket {
    constructor(url) { this.url = url; this.readyState = 0; this.closed = null; sockets.push(this); }
    close(code, reason) { this.closed = { code, reason }; this.readyState = 3; }
  }
  const real = globalThis.WebSocket;
  globalThis.WebSocket = DeadSocket;
  const started = Date.now();
  try {
    await openCodeRoom('code-room-1', { origin: 'https://relay.example', connectWait: 150 });
    return { ok: true };
  } catch (error) {
    return { ok: false, kind: error.kind, sentence: meetProblem(error),
             url: sockets[0] && sockets[0].url, closed: !!(sockets[0] && sockets[0].closed),
             waited: Date.now() - started >= 150 };
  } finally {
    globalThis.WebSocket = real;
  }
}

async function cases() {
  return {
    connectTimeout: await connectTimeout(),
    right: await scenario(),
    wrongPin: await scenario({ pin: '4828' }),
    flippedTag: await scenario({ tweak: { flipTag: true } }),
    zeroPoint: await scenario({ tweak: { zeroPoint: true } }),
    burned: await scenario({ tweak: { error: 'burned' } }),
    expired: await scenario({ tweak: { error: 'expired' } }),
    desktopWrongPin: await scenario({ tweak: { error: 'wrong_pin' } }),
    otherAad: await scenario({ tweak: { aad: 'bqrt' } }),
    silent: await scenario({ tweak: { silent: true }, wait: 200 }),
    otherRoom: await scenario({ room: 'code-room-1', tweak: {} }).then(async () => {
      // The room id is CPace's sid: the same PIN in a different room is a different key.
      const peer = desktop({ code: 'BQRT', pin: '4829', room: 'code-room-2', fragment: FRAGMENT });
      const channel = pipe(peer);
      try {
        await codePhase({ code: 'BQRT', pin: '4829', room: 'code-room-1', channel, wait: 2000 });
        return { ok: true };
      } catch (error) {
        return { ok: false, kind: error.kind };
      }
    }),
    badInput: await Promise.all([
      codePhase({ code: 'BQR', pin: '4829', room: 'r', channel: pipe(desktop({})) }),
      codePhase({ code: 'BQRI', pin: '4829', room: 'r', channel: pipe(desktop({})) }),
      codePhase({ code: 'BQRT', pin: '482', room: 'r', channel: pipe(desktop({})) }),
    ].map((attempt) => attempt.then(() => 'ok', (error) => error.kind))),
    clean: {
      code: ['bqrt', 'b-q r t', 'BILOQRTX', 'a1b2', 'abcdefg', '', 'ilo'].map(cleanCode),
      pin: ['4829', '48 29', '4a8b2c9d', '482917', 'abcd'].map(cleanPin),
    },
    sentences: Object.fromEntries(
      ['unknown_code', 'wrong_pin', 'burned', 'expired', 'no_answer', 'rate_limited',
       'unreachable', 'protocol', 'bad_code', 'bad_pin']
        .map((kind) => [kind, meetProblem(new MeetError(kind))])),
    lookups: await lookups(),
  };
}

// ---- the stdin/stdout mode ---------------------------------------------------------------------

async function wire(args) {
  const lines = createInterface({ input: process.stdin })[Symbol.asyncIterator]();
  const channel = {
    async send(message) { process.stdout.write(JSON.stringify(message) + '\n'); },
    async next() {
      const { value, done } = await lines.next();
      if (done) throw new MeetError('closed');
      return JSON.parse(value);
    },
  };
  let done;
  try {
    done = { ok: true, fragment: await codePhase({ ...args, channel }) };
  } catch (error) {
    done = { ok: false, kind: error instanceof MeetError ? error.kind : String(error) };
  }
  process.stdout.write(JSON.stringify({ done }) + '\n');
  process.exit(0);
}

const [mode, argument] = process.argv.slice(2);
if (mode === 'wire') {
  await wire(JSON.parse(argument));
} else {
  process.stdout.write(JSON.stringify(await cases()));
}

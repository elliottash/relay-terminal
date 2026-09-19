// SPDX-License-Identifier: AGPL-3.0-or-later
// Joining with a meeting code and a PIN (card #97EG): the browser's half of the code phase.
//
// A friend is told two short things — `BQRT` and `4829` — instead of being sent a long link. The
// code is public: the rendezvous uses it only to find a room. The PIN is secret and never leaves
// this page: it goes into CPace (app/cpace.js) as the password, so every guess is a live attempt
// against the desktop, which burns the code after three. What the code phase earns is an ordinary
// invite fragment (`v=1&d=…&i=…&r=…`), sealed under the CPace key; app/guest.js hands it to the
// same parse → offer → knock path a link takes, so there is one way in and it is the old one.
//
// The PIN is never put in a URL, in history, in storage, or in any console or log line. Nothing in
// this file keeps it past the one attempt it was typed for.

import { CPace, CPaceError } from './cpace.js';
import { b64, un64 } from './rrp.js';
import { concat, equalBytes } from './noise.js';

const subtle = globalThis.crypto.subtle;
const utf8 = (text) => new TextEncoder().encode(text);

// 23 letters: no I, L or O, which are read as 1, 1 and 0.
export const CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ';
export const CODE_LENGTH = 4;
export const PIN_LENGTH = 4;

const CI = utf8('relay/meet/v1');
const AD_GUEST = utf8('guest');
const AD_DESKTOP = utf8('desktop');
const LABEL_TAG_B = utf8('relay/meet/v1 desktop');
const LABEL_TAG_A = utf8('relay/meet/v1 guest');
const LABEL_SEAL = utf8('relay/meet/v1 seal');

// The desktop allows 30 s from the socket opening to a valid `meet_confirm`; waiting for either of
// its two answers longer than that only postpones the same news.
export const ANSWER_WAIT = 20000;

// And a deadline for the socket itself. A WebSocket that is never answered — a captive portal, a
// dropped Wi-Fi, a rendezvous whose TCP connection is accepted and then goes quiet — fires neither
// `open` nor `error` nor `close`, so `openCodeRoom` never settled: the join sat on "Checking the
// code and PIN with their desktop…" for as long as the page was open, with the form disabled and
// nothing to press. Past this it fails `unreachable`, which app/guest.js shows with the form back
// and Join enabled — the same "try again" a dropped link gets.
export const CONNECT_WAIT = 10000;

// ---- what the person typed ----------------------------------------------------------------------

// Case-insensitive on input, and anything outside the alphabet is simply not typed, so the field
// can never hold a code that could not exist.
export function cleanCode(text) {
  let out = '';
  for (const ch of String(text || '').toUpperCase()) {
    if (CODE_ALPHABET.includes(ch)) out += ch;
    if (out.length === CODE_LENGTH) break;
  }
  return out;
}

export function cleanPin(text) {
  return String(text || '').replace(/\D/g, '').slice(0, PIN_LENGTH);
}

export const validCode = (code) => cleanCode(code) === code && code.length === CODE_LENGTH;
export const validPin = (pin) => /^\d{4}$/.test(pin);

// ---- failures -----------------------------------------------------------------------------------

// Every way this can fail, by kind, so the page can say the one sentence that fits. The kinds the
// desktop sends (`wrong_pin`, `burned`, `expired`) keep its names.
export class MeetError extends Error {
  constructor(kind, message) {
    super(message || kind);
    this.kind = kind;
  }
}

// ---- the crypto ---------------------------------------------------------------------------------

async function hmac(key, message) {
  const k = await subtle.importKey('raw', key, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return new Uint8Array(await subtle.sign('HMAC', k, message));
}

// The three values the card derives from ISK. `ya` is always the guest's point, `yb` the desktop's.
export async function meetKeys(isk, ya, yb) {
  return {
    tagB: await hmac(isk, concat(LABEL_TAG_B, ya, yb)),
    tagA: await hmac(isk, concat(LABEL_TAG_A, ya, yb)),
    sealKey: await hmac(isk, LABEL_SEAL),
  };
}

export async function openSealed(sealKey, code, nonce, sealed) {
  const key = await subtle.importKey('raw', sealKey, 'AES-GCM', false, ['decrypt']);
  const params = { name: 'AES-GCM', iv: nonce, additionalData: utf8(code), tagLength: 128 };
  return new TextDecoder().decode(await subtle.decrypt(params, key, sealed));
}

function bytesField(message, field, length) {
  let value;
  try {
    value = un64(String(message?.[field] ?? ''));
  } catch {
    value = null;
  }
  if (!value || value.length !== length) {
    throw new MeetError('protocol', `the desktop's ${message?.t || 'answer'} was malformed.`);
  }
  return value;
}

// ---- the code phase -----------------------------------------------------------------------------

// Run the guest's (initiator's) side of the code phase over `channel`, which has
// `send(object)` and `next(timeout)` → the next frame as an object. Returns the invite fragment.
//
// The order is the card's and it matters: `meet_confirm` goes only after the desktop has proved
// it knows the PIN, so a server sitting in the middle learns nothing it can test a guess against.
export async function codePhase({ code, pin, room, channel, wait = ANSWER_WAIT, scalar }) {
  if (!validCode(code)) throw new MeetError('bad_code');
  if (!validPin(pin)) throw new MeetError('bad_pin');
  const cpace = await CPace.create({
    prs: utf8(pin), ci: CI, sid: utf8(room), initiator: true, ad: AD_GUEST,
    ...(scalar ? { scalar } : {}),
  });
  const ya = cpace.message;
  await channel.send({ t: 'meet_a', y: b64(ya) });

  const answer = expect(await channel.next(wait), 'meet_b');
  const yb = bytesField(answer, 'y', 32);
  const tagB = bytesField(answer, 'tag', 32);
  let isk;
  try {
    isk = await cpace.finish(yb, AD_DESKTOP);
  } catch (error) {
    // An invalid point is not something a desktop with any PIN sends; somebody is interfering.
    if (error instanceof CPaceError) throw new MeetError('protocol', 'the desktop sent an invalid point.');
    throw error;
  }
  const keys = await meetKeys(isk, ya, yb);
  // A tag that does not check out is how a wrong PIN shows up: the two sides hold different keys.
  // Nothing is sent back — closing is itself the failure the desktop counts.
  if (!equalBytes(tagB, keys.tagB)) throw new MeetError('wrong_pin');

  await channel.send({ t: 'meet_confirm', tag: b64(keys.tagA) });
  const invite = expect(await channel.next(wait), 'meet_invite');
  const nonce = bytesField(invite, 'nonce', 12);
  let sealed;
  try {
    sealed = un64(String(invite.sealed || ''));
  } catch {
    throw new MeetError('protocol', "the desktop's meet_invite was malformed.");
  }
  try {
    return await openSealed(keys.sealKey, code, nonce, sealed);
  } catch {
    throw new MeetError('protocol', 'the invitation did not open.');
  }
}

function expect(message, kind) {
  if (message?.t === 'meet_error') {
    const reported = String(message.error || '');
    throw new MeetError(['wrong_pin', 'burned', 'expired'].includes(reported) ? reported : 'refused');
  }
  if (message?.t !== kind) throw new MeetError('protocol', `expected ${kind}.`);
  return message;
}

// ---- the wire -----------------------------------------------------------------------------------

// Find the code room. The code is public — it is in this URL on purpose — the PIN never is.
export async function lookupCode(code, { origin = location.origin, fetcher = fetch } = {}) {
  let response;
  try {
    response = await fetcher(`${origin}/v1/codes/${encodeURIComponent(code)}`,
                             { cache: 'no-store', credentials: 'omit' });
  } catch {
    throw new MeetError('unreachable');
  }
  if (response.status === 404) throw new MeetError('unknown_code');
  if (response.status === 429) throw new MeetError('rate_limited');
  if (!response.ok) throw new MeetError('unreachable');
  let body;
  try {
    body = await response.json();
  } catch {
    throw new MeetError('unreachable');
  }
  if (!body || typeof body.room !== 'string' || !body.room) throw new MeetError('unknown_code');
  return body.room;
}

// A code room, opened the way rrp.js opens any room, carrying the card's JSON frames. They go as
// binary frames holding UTF-8 JSON: the rendezvous forwards a client's binary frames to the desktop
// and drops text frames (rendezvous/server.py, `_client_socket`).
export function openCodeRoom(room, { origin = location.origin, connectWait = CONNECT_WAIT } = {}) {
  const url = `${origin.replace(/^http/, 'ws')}/v1/connect?room=${encodeURIComponent(room)}`;
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    socket.binaryType = 'arraybuffer';
    const queue = [];
    const waiters = [];
    let closed = null;
    let opened = false;
    // The socket's own deadline (CONNECT_WAIT). Closing it is part of giving up: a socket left
    // connecting would open later into a room nobody is listening to.
    let connectTimer = connectWait > 0 ? setTimeout(() => {
      connectTimer = 0;
      closed = new MeetError('unreachable');
      try { socket.close(1000, 'timeout'); } catch { /* already closing */ }
      reject(closed);                 // nothing can be waiting yet: `next` exists only once open
    }, connectWait) : 0;
    const arrived = () => { if (connectTimer) { clearTimeout(connectTimer); connectTimer = 0; } };

    const settle = () => {
      while (waiters.length && (queue.length || closed)) {
        const waiter = waiters.shift();
        clearTimeout(waiter.timer);
        if (queue.length) waiter.resolve(queue.shift());
        else waiter.reject(closed);
      }
    };

    socket.onopen = () => {
      if (closed) { try { socket.close(1000, 'bye'); } catch { /* already closing */ } return; }
      arrived();
      opened = true;
      resolve({
        send(message) {
          if (socket.readyState !== WebSocket.OPEN) return Promise.reject(new MeetError('no_answer'));
          socket.send(utf8(JSON.stringify(message)));
          return Promise.resolve();
        },
        next(timeout = ANSWER_WAIT) {
          return new Promise((ok, fail) => {
            const waiter = { resolve: ok, reject: fail, timer: null };
            waiter.timer = setTimeout(() => {
              const at = waiters.indexOf(waiter);
              if (at >= 0) waiters.splice(at, 1);
              fail(new MeetError('no_answer'));
            }, timeout);
            waiters.push(waiter);
            settle();
          });
        },
        close() {
          if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) {
            socket.close(1000, 'bye');
          }
        },
      });
    };
    socket.onmessage = (event) => {
      const text = typeof event.data === 'string' ? event.data
        : new TextDecoder().decode(new Uint8Array(event.data));
      let message;
      try {
        message = JSON.parse(text);
      } catch {
        return;
      }
      if (message && typeof message === 'object') {
        queue.push(message);
        settle();
      }
    };
    socket.onerror = () => {
      if (opened) return;
      arrived();
      reject(new MeetError('unreachable'));
    };
    socket.onclose = (event) => {
      arrived();
      // 4404 is the rendezvous saying the room or its desktop is gone: the code outlived it, or
      // the desktop that made it is asleep or offline. A close after the deadline has already been
      // reported keeps the deadline's answer: the socket never opened.
      if (!closed) closed = new MeetError(event.code === 4404 ? 'no_answer' : 'closed');
      if (!opened) reject(closed);
      settle();
    };
  });
}

// The whole code phase from two typed strings: find the room, run the exchange, close the room.
export async function joinWithCode(code, pin, { origin = location.origin, fetcher,
                                                 connectWait = CONNECT_WAIT } = {}) {
  const clean = cleanCode(code);
  if (!validCode(clean)) throw new MeetError('bad_code');
  if (!validPin(pin)) throw new MeetError('bad_pin');
  const room = await lookupCode(clean, { origin, ...(fetcher ? { fetcher } : {}) });
  const channel = await openCodeRoom(room, { origin, connectWait });
  try {
    return await codePhase({ code: clean, pin, room, channel });
  } catch (error) {
    // The room closing before the desktop's first answer means nobody was there to run it.
    if (error instanceof MeetError && error.kind === 'closed') throw new MeetError('no_answer');
    throw error;
  } finally {
    channel.close();
  }
}

// ---- sentences ----------------------------------------------------------------------------------

// What to tell the person, in plain words, for each kind of failure. Never echoes the PIN.
export function meetProblem(error) {
  switch (error?.kind) {
    case 'bad_code':
      return 'A meeting code is four letters, like BQRT.';
    case 'bad_pin':
      return 'The PIN is four digits, like 4829.';
    case 'unknown_code':
      return 'There is no meeting with that code. Check the four letters — codes last ten '
        + 'minutes, so if it is older than that, ask for a new one.';
    case 'wrong_pin':
      return 'That PIN is not right. Wrong PINs count: after three, this code stops working. '
        + 'Check the PIN with the person who gave it to you before trying again.';
    case 'burned':
      return 'This code has stopped working — it has been used, or too many wrong PINs were '
        + 'tried. Ask the person who gave it to you for a new code.';
    case 'expired':
      return 'This code has expired: codes last ten minutes. Ask the person who gave it to you '
        + 'for a new one.';
    case 'no_answer':
      return 'Their desktop is not answering. Relay has to be open on their computer for the '
        + 'code to work — ask them to check, then try again.';
    case 'rate_limited':
      return 'Too many codes have been tried from this network in the last minute. Wait a '
        + 'minute and try again.';
    case 'unreachable':
      return 'Could not reach the relay. Check your connection and try again.';
    default:
      return 'Joining with that code did not work — the connection was broken or interfered '
        + 'with. Try again, and if it keeps happening, ask for a new code.';
  }
}

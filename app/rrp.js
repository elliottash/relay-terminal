// SPDX-License-Identifier: AGPL-3.0-or-later
// The RRP/1 client: pairing, the Noise session over a WebSocket, and stored device keys.
// The UI (app.js) never sees a key or a frame; it sees messages.

import { Initiator, exportPublic, importPublic, concat, equalBytes } from './noise.js';

const ENC_JSON = 0;
const DB_NAME = 'relay-remote';
const STORE = 'device';

export function b64(bytes) {
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export function un64(text) {
  const padded = text.replace(/-/g, '+').replace(/_/g, '/') + '='.repeat((4 - text.length % 4) % 4);
  const binary = atob(padded);
  return Uint8Array.from(binary, (c) => c.charCodeAt(0));
}

export function fingerprint(bytes) {
  return crypto.subtle.digest('SHA-256', bytes).then((digest) => {
    const hex = [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
    const short = hex.slice(0, 12).toUpperCase();
    return `${short.slice(0, 4)} ${short.slice(4, 8)} ${short.slice(8, 12)}`;
  });
}

// A pairing or invite fragment, in whatever shape it reached this device. `secretField` is the
// only difference between the two links, and each caller brings its own two sentences, because
// "scan the QR code again" and "ask whoever invited you" are different things to do.
function parseLinkFragment(fragment, secretField, incomplete, damagedText) {
  let raw = String(fragment).replace(/^#/, '');
  // Some QR readers and link handlers percent-encode the fragment, which turns the separators
  // into %26 and leaves one field called `v` holding the whole rest of the link. Undo that
  // before parsing rather than reporting a missing field the user cannot do anything about.
  if (!raw.includes('&') && /%26/i.test(raw)) {
    try {
      raw = decodeURIComponent(raw);
    } catch {
      /* keep the original and let the checks below report it */
    }
  }
  const fields = new URLSearchParams(raw);
  for (const required of ['v', 'd', secretField, 'r']) {
    if (!fields.get(required)) throw new Error(incomplete);
  }
  if (fields.get('v') !== '1') throw new Error('This link needs a newer version of the app.');
  const damaged = new Error(damagedText);
  let desktopPublic;
  let secret;
  try {
    desktopPublic = un64(fields.get('d'));
    secret = un64(fields.get(secretField));
  } catch {
    throw damaged;              // not base64: the link was mangled, not merely truncated
  }
  if (desktopPublic.length !== 32 || secret.length < 16) throw damaged;
  return { desktopPublic, secret, room: fields.get('r') };
}

// ---- stored identity --------------------------------------------------------------------------
// The private key is generated non-extractable and kept as a CryptoKey, so it is never in reach of
// script that can read it — including a later, hostile version of this app.

function openDb() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => request.result.createObjectStore(STORE);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function dbGet(key) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const request = db.transaction(STORE, 'readonly').objectStore(STORE).get(key);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function dbPut(key, value) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const request = db.transaction(STORE, 'readwrite').objectStore(STORE).put(value, key);
    request.onsuccess = () => resolve();
    request.onerror = () => reject(request.error);
  });
}

async function dbDelete(key) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const request = db.transaction(STORE, 'readwrite').objectStore(STORE).delete(key);
    request.onsuccess = () => resolve();
    request.onerror = () => reject(request.error);
  });
}

// A paired-device record and a guest record (section 10.2) sit side by side under different keys,
// and neither loader will ever return the other's row. The shapes differ on purpose — a device has
// a `deviceId` and a `capability`, a guest a `participant` and a `role`, and there is no field
// leading from one to the other — so one browser can be the owner of one desktop and somebody's
// guest on another without either flow reading, overwriting or promoting the wrong record. It is
// the client-side half of the rule `remote/guests.py` makes structural on the desktop.
const isDeviceRecord = (record) =>
  !!record && typeof record.deviceId === 'string' && record.participant === undefined;
const isGuestRecord = (record) =>
  !!record && typeof record.participant === 'string' && record.deviceId === undefined;

export async function loadDevice() {
  const record = await dbGet('paired');
  return isDeviceRecord(record) ? record : null;
}

export async function loadGuest() {
  const record = await dbGet('guest');
  return isGuestRecord(record) ? record : null;
}

// Any value beside the device record — today, the push seal key the service worker shares.
export async function storedValue(key) {
  return (await dbGet(key)) || null;
}

export async function storeValue(key, value) {
  await dbPut(key, value);
}

export async function dropValue(key) {
  await dbDelete(key);
}

export async function saveDevice(record) {
  if (!isDeviceRecord(record)) throw new Error('that is not a device record.');
  await dbPut('paired', record);
}

export async function forgetDevice() {
  await dbDelete('paired');
}

export async function saveGuest(record) {
  if (!isGuestRecord(record)) throw new Error('that is not a guest record.');
  await dbPut('guest', record);
}

export async function forgetGuest() {
  await dbDelete('guest');
}

// ---- the session ------------------------------------------------------------------------------

export class Rrp extends EventTarget {
  constructor(origin = location.origin) {
    super();
    this.origin = origin;
    this.socket = null;
    this.session = null;
    this.record = null;
    this.authCode = '';
    this.streams = new Map();
    this.hubEpoch = null;
    this.closing = false;
  }

  get socketBase() {
    return this.origin.replace(/^http/, 'ws');
  }

  emit(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  // -- pairing ----------------------------------------------------------------------------------

  static parsePairFragment(fragment) {
    return parseLinkFragment(fragment, 's',
      'That pairing link is incomplete — part of it was lost on the way here. '
        + 'Scan the QR code on your desktop again.',
      'That pairing link is damaged — scan the QR code on your desktop again.');
  }

  // An invite link (section 10.2) has the same shape and a different secret field: `i`, not `s`,
  // so a link that admits a guest can never be fed to the pairing handler by accident. The role
  // is deliberately not in it — enforcement reads the invite record on the desktop, and a role in
  // the fragment would only be a claim its holder could edit.
  static parseInviteFragment(fragment) {
    return parseLinkFragment(fragment, 'i',
      'That invitation link is incomplete — part of it was lost on the way here. '
        + 'Ask whoever invited you to send it again.',
      'That invitation link is damaged. Ask whoever invited you to send it again.');
  }

  async pair(link, { name, platform }) {
    const pair = await crypto.subtle.generateKey({ name: 'X25519' }, false, ['deriveBits']);
    const devicePublic = await exportPublic(pair.publicKey);
    await this.#open(`${this.socketBase}/v1/connect?room=${encodeURIComponent(link.room)}`);
    await this.#handshake(pair.privateKey, devicePublic, link.desktopPublic, true);

    this.send({ t: 'pair_prove', secret: b64(link.secret), name, platform });
    const reply = await this.once('paired', 180000);
    const record = {
      desktopPublic: link.desktopPublic,
      devicePrivate: pair.privateKey,
      devicePublic,
      deviceId: reply.device_id,
      capability: reply.capability,
      desktopId: reply.desktop?.id || '',
      desktopName: reply.desktop?.name || 'desktop',
      pairedAt: Date.now(),
    };
    await saveDevice(record);
    this.record = record;
    // A pairing channel is for pairing. Drop it and come back as a paired device, so there is one
    // path into a working session and it is the one every later connection uses.
    this.close();
    this.session = null;
    return record;
  }

  // -- multiplayer: knocking with an invite link (section 10.2) ---------------------------------

  // Follow an invite: join its room, knock, and wait for the owner to answer. What comes back is
  // a **guest** record — a participant id and a role, no device id and no capability — because a
  // participant is not a device and this channel can never produce one (`pair_prove` on an invite
  // room is refused by the hub).
  async knock(link, { name, platform }) {
    const pair = await crypto.subtle.generateKey({ name: 'X25519' }, false, ['deriveBits']);
    const guestPublic = await exportPublic(pair.publicKey);
    await this.#open(`${this.socketBase}/v1/connect?room=${encodeURIComponent(link.room)}`);
    await this.#handshake(pair.privateKey, guestPublic, link.desktopPublic);

    this.send({ t: 'knock', invite: b64(link.secret), name, platform });
    const waiting = await this.once('knock_pending', 30000);
    // The five digits come out of the handshake hash, which neither side chose alone, so both
    // ends derive the same code independently. A desktop that sends a different one is not the
    // desktop this session is with, and there would be nothing useful to compare on two screens.
    if (waiting.code !== this.authCode) {
      this.close();
      throw new Error('That invitation could not be verified. Ask whoever invited you for a '
        + 'fresh link.');
    }
    this.emit('authcode', { code: this.authCode });
    // The hub gives up on an unanswered knock after two minutes (KNOCK_TIMEOUT) and answers
    // `error not_admitted`, which `once` turns into a rejection carrying that code.
    const admitted = await this.once('admitted', 150000);
    const record = {
      desktopPublic: link.desktopPublic,
      devicePrivate: pair.privateKey,
      devicePublic: guestPublic,
      participant: admitted.participant,
      role: admitted.role,
      panes: Array.isArray(admitted.panes) ? admitted.panes : [],
      expires: Number(admitted.expires) || 0,
      desktopId: admitted.desktop?.id || '',
      desktopName: admitted.desktop_name || admitted.desktop?.name || 'their desktop',
      guestName: name,
      joinedAt: Date.now(),
    };
    await saveGuest(record);
    this.record = record;
    // Unlike pairing, the channel stays: the hub follows `admitted` with this guest's scoped
    // `panes` and the `participants` list, on this session.
    return record;
  }

  // Come back as an admitted participant. The channel id is the participant id, never a device
  // id, and the `welcome` that answers carries a role and no capability.
  async rejoin(record) {
    this.record = record;
    this.closing = false;
    const url = `${this.socketBase}/v1/connect?desktop=${encodeURIComponent(record.desktopId)}`
      + `&device=${encodeURIComponent(record.participant)}`;
    await this.#open(url);
    await this.#handshake(record.devicePrivate, record.devicePublic, record.desktopPublic);
    this.send({ t: 'hello', client: 'relay-web/1', proto: 1 });
    const welcome = await this.once('welcome');
    if (this.hubEpoch && this.hubEpoch !== welcome.hub_epoch) this.streams.clear();
    this.hubEpoch = welcome.hub_epoch;
    return welcome;
  }

  // -- connecting -------------------------------------------------------------------------------

  async connect(record) {
    this.record = record;
    this.closing = false;
    const url = `${this.socketBase}/v1/connect?desktop=${encodeURIComponent(record.desktopId)}`
      + `&device=${encodeURIComponent(record.deviceId)}`;
    await this.#open(url);
    // The pinned key is the only one this device will ever talk to. There is deliberately no
    // branch that accepts a different desktop key: a changed key means re-pairing from a QR code.
    await this.#handshake(record.devicePrivate, record.devicePublic, record.desktopPublic);
    this.send({ t: 'hello', client: 'relay-web/1', proto: 1 });
    const welcome = await this.once('welcome');
    this.emit('welcome', welcome);
    if (this.hubEpoch && this.hubEpoch !== welcome.hub_epoch) this.streams.clear();
    this.hubEpoch = welcome.hub_epoch;
    return welcome;
  }

  async #open(url) {
    this.socket = new WebSocket(url);
    this.socket.binaryType = 'arraybuffer';
    await new Promise((resolve, reject) => {
      this.socket.onopen = resolve;
      this.socket.onerror = () => reject(new Error('could not reach the relay.'));
    });
    this.socket.onclose = (event) => {
      this.session = null;
      this.emit('closed', { code: event.code, reason: event.reason, clean: this.closing });
    };
  }

  async #handshake(privateKey, publicRaw, desktopPublic, announce = false) {
    const initiator = new Initiator(privateKey, publicRaw, desktopPublic);
    const first = await initiator.writeMessage1(new Uint8Array(0));
    const reply = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('the desktop did not answer.')), 20000);
      this.socket.onmessage = (event) => { clearTimeout(timer); resolve(new Uint8Array(event.data)); };
      this.socket.addEventListener('close', () => {
        clearTimeout(timer);
        reject(new Error('the desktop refused this device.'));
      }, { once: true });
      // Sent only once the reply handler is in place, so a fast desktop cannot answer into a gap.
      this.socket.send(first);
    });
    const { session } = await initiator.readMessage2(reply);
    this.session = session;
    this.authCode = await this.#authCode(session.handshakeHash);
    // Only at pairing: the code is what the person compares against the desktop's dialog, so it
    // must be on screen before the desktop is asked, and must not change afterwards.
    if (announce) this.emit('authcode', { code: this.authCode });
    // Frames are handled strictly in arrival order: decryption is async, and a screen stream
    // delivers them faster than one can finish.
    this.socket.onmessage = (event) => {
      const bytes = new Uint8Array(event.data);
      this.receiving = (this.receiving || Promise.resolve())
        .then(() => this.#frame(bytes))
        .catch(() => {});
    };
  }

  async #authCode(handshakeHash) {
    const label = new TextEncoder().encode('RRP/1 pairing auth');
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', concat(label, handshakeHash)));
    const value = new DataView(digest.buffer).getUint32(0, false) % 100000;
    return String(value).padStart(5, '0');
  }

  async #frame(bytes) {
    let plaintext;
    try {
      plaintext = await this.session.decrypt(bytes);
    } catch {
      this.emit('fault', { message: 'a frame failed to authenticate; the link was dropped.' });
      this.close();
      return;
    }
    if (!plaintext.length || plaintext[0] !== ENC_JSON) return;
    let message;
    try {
      message = JSON.parse(new TextDecoder().decode(plaintext.subarray(1)));
    } catch {
      return;
    }
    if (typeof message.seq === 'number' && message.t) {
      this.streams.set(Rrp.streamOf(message), message.seq);
    }
    this.emit('message', message);
    this.emit(message.t, message);
  }

  // -- sending ----------------------------------------------------------------------------------

  // Encrypt and write as one step. The Noise cipherstate is a counter, so two sends racing would
  // arrive out of order and the desktop would reject the second one.
  send(message) {
    if (!this.session || this.socket?.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error('not connected.'));
    }
    this.sending = (this.sending || Promise.resolve()).then(async () => {
      if (!this.session || this.socket?.readyState !== WebSocket.OPEN) {
        throw new Error('not connected.');
      }
      const body = new TextEncoder().encode(JSON.stringify(message));
      const payload = concat(new Uint8Array([ENC_JSON]), body);
      this.socket.send(await this.session.encrypt(payload));
    });
    const result = this.sending;
    this.sending = this.sending.catch(() => {});   // one failure must not poison the chain
    return result;
  }

  once(kind, timeout = 20000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.removeEventListener(kind, onMessage);
        this.removeEventListener('error', onError);
        reject(new Error(`timed out waiting for ${kind}.`));
      }, timeout);
      const done = () => {
        clearTimeout(timer);
        this.removeEventListener(kind, onMessage);
        this.removeEventListener('error', onError);
      };
      const onMessage = (event) => { done(); resolve(event.detail); };
      const onError = (event) => {
        done();
        const failure = new Error(event.detail.message || 'the desktop refused that.');
        // The code as well as the sentence: a refused knock (`not_admitted`) and a knock that
        // never got an answer read the same to `catch` otherwise, and they are different things
        // to tell someone.
        failure.code = event.detail.code || '';
        reject(failure);
      };
      this.addEventListener(kind, onMessage, { once: true });
      this.addEventListener('error', onError, { once: true });
    });
  }

  // The hub's own stream names (remote/host.py): `panes`, `agent:<pane>` and `screen:<pane>`.
  // A resume that names a stream the hub does not keep is silently ignored, which is how the
  // screen stream went unresumed for a while: it was recorded here under the message types
  // `screen_snapshot` / `screen_diff` instead of the one name the hub files both under.
  static streamOf(message) {
    if (message.t === 'agent') return `agent:${message.pane}`;
    if (message.t === 'screen_snapshot' || message.t === 'screen_diff') return `screen:${message.pane}`;
    return message.t;
  }

  resume() {
    const streams = Object.fromEntries(this.streams);
    if (Object.keys(streams).length) {
      this.send({ t: 'resume', streams, hub_epoch: this.hubEpoch }).catch(() => {});
    }
  }

  close() {
    this.closing = true;
    if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.close(1000, 'bye');
  }
}

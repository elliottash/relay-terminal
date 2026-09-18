// SPDX-License-Identifier: GPL-3.0-or-later
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

export async function loadDevice() {
  return (await dbGet('paired')) || null;
}

export async function saveDevice(record) {
  await dbPut('paired', record);
}

export async function forgetDevice() {
  await dbDelete('paired');
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
    const fields = new URLSearchParams(fragment.replace(/^#/, ''));
    for (const required of ['v', 'd', 's', 'r']) {
      if (!fields.get(required)) throw new Error(`the pairing link is missing '${required}'.`);
    }
    if (fields.get('v') !== '1') throw new Error('this link needs a newer version of the app.');
    const desktopPublic = un64(fields.get('d'));
    if (desktopPublic.length !== 32) throw new Error('the desktop key in the link is malformed.');
    return { desktopPublic, secret: un64(fields.get('s')), room: fields.get('r') };
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
      const stream = message.t === 'agent' ? `agent:${message.pane}` : message.t;
      this.streams.set(stream, message.seq);
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
        reject(new Error(event.detail.message || 'the desktop refused that.'));
      };
      this.addEventListener(kind, onMessage, { once: true });
      this.addEventListener('error', onError, { once: true });
    });
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

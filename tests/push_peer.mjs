// SPDX-License-Identifier: AGPL-3.0-or-later
// app/sw.js's push handler, run under Node so the Python seal and the service worker that opens
// it are checked against each other rather than each against itself — the same arrangement as
// tests/noise_peer.mjs does for the Noise handshake.
//
// `node tests/push_peer.mjs <seal key, base64> <sealed blob, base64>` prints the notifications
// the worker decided to show, as JSON. A blob it cannot open prints `[]`, which is the rule that
// makes a forged push harmless.
import { readFileSync } from 'node:fs';
import { webcrypto } from 'node:crypto';
import { createContext, runInContext } from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const source = readFileSync(`${here}../app/sw.js`, 'utf8');
const un64 = (text) => new Uint8Array(Buffer.from(text, 'base64'));

const [, , keyArg, blobArg] = process.argv;
const key = await webcrypto.subtle.importKey('raw', un64(keyArg), { name: 'AES-GCM' }, false,
  ['decrypt']);
const blob = un64(blobArg);

// Just enough of a service worker's globals. Each IndexedDB request answers on the next tick,
// the way the real one does, so sw.js's callback wiring is exercised and not bypassed.
function request(result) {
  const handle = { result, onsuccess: null, onerror: null, onupgradeneeded: null };
  setTimeout(() => handle.onsuccess && handle.onsuccess(), 0);
  return handle;
}

const shown = [];
const pending = [];
const listeners = new Map();
const objectStore = { get: (name) => request(name === 'push-key' ? key : undefined) };
const database = { transaction: () => ({ objectStore: () => objectStore }) };

const context = createContext({
  self: {
    addEventListener: (name, handler) => listeners.set(name, handler),
    skipWaiting: () => {},
    clients: { claim: () => {}, matchAll: async () => [], openWindow: async () => {} },
    registration: {
      showNotification: async (title, options) => { shown.push({ title, options }); },
    },
  },
  indexedDB: { open: () => request(database) },
  crypto: webcrypto,
  TextEncoder,
  TextDecoder,
  setTimeout,
  console,
});
runInContext(source, context);

const handler = listeners.get('push');
handler({
  data: { arrayBuffer: async () => blob.buffer.slice(blob.byteOffset, blob.byteOffset + blob.byteLength) },
  waitUntil: (promise) => pending.push(promise),
});
await Promise.all(pending);
process.stdout.write(JSON.stringify(shown));

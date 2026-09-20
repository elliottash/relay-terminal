// SPDX-License-Identifier: AGPL-3.0-or-later
// app/sw.js's `notificationclick`, run under Node against a body remote/notify.py sealed — the
// same arrangement as tests/push_peer.mjs, one step further down the path: the push is opened,
// shown, and then tapped.
//
// `node tests/sw_click_peer.mjs <seal key, base64> <sealed blob, base64> [open|focus]` prints
// what the worker did with the tap, as JSON:
//
//   {"shown": [...], "posted": [...], "focused": n, "opened": "<url|null>"}
//
// `focus` (the default) gives it a window that is already open — the app in the app switcher.
// `open` gives it none, which is the cold case: the pane id has to travel in the URL, because a
// window `openWindow` has only just created has no `message` listener yet.
import { readFileSync } from 'node:fs';
import { webcrypto } from 'node:crypto';
import { createContext, runInContext } from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const source = readFileSync(`${here}../app/sw.js`, 'utf8');
const un64 = (text) => new Uint8Array(Buffer.from(text, 'base64'));

const [, , keyArg, blobArg, mode = 'focus'] = process.argv;
const key = await webcrypto.subtle.importKey('raw', un64(keyArg), { name: 'AES-GCM' }, false,
  ['decrypt']);
const blob = un64(blobArg);

function request(result) {
  const handle = { result, onsuccess: null, onerror: null, onupgradeneeded: null };
  setTimeout(() => handle.onsuccess && handle.onsuccess(), 0);
  return handle;
}

const shown = [];
const posted = [];
const pending = [];
const listeners = new Map();
let focused = 0;
let opened = null;

const objectStore = { get: (name) => request(name === 'push-key' ? key : undefined) };
const database = { transaction: () => ({ objectStore: () => objectStore }) };

// One window already open, or none: the two cases `notificationclick` has to tell apart.
const client = {
  focus: async () => { focused += 1; return client; },
  postMessage: (message) => { posted.push(message); },
};

const context = createContext({
  self: {
    addEventListener: (name, handler) => listeners.set(name, handler),
    skipWaiting: () => {},
    clients: {
      claim: () => {},
      matchAll: async () => (mode === 'focus' ? [client] : []),
      openWindow: async (url) => { opened = url; return null; },
    },
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

listeners.get('push')({
  data: { arrayBuffer: async () => blob.buffer.slice(blob.byteOffset, blob.byteOffset + blob.byteLength) },
  waitUntil: (promise) => pending.push(promise),
});
await Promise.all(pending.splice(0));

// Tap whatever was shown, with exactly the `data` the worker attached to it — nothing about the
// notification is invented here.
let closed = 0;
for (const notification of shown) {
  listeners.get('notificationclick')({
    notification: { ...notification.options, close: () => { closed += 1; } },
    waitUntil: (promise) => pending.push(promise),
  });
}
await Promise.all(pending);

process.stdout.write(JSON.stringify({ shown, posted, focused, opened, closed }));

// SPDX-License-Identifier: AGPL-3.0-or-later
// Enough of a service worker to make the app installable, which is what iOS requires before it
// will deliver Web Push, and to receive those pushes. It deliberately does not cache: a stale
// copy of a client that holds cryptographic keys is not something to keep around.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

// One push, opened or dropped. The body arrives sealed to a key only this device and the
// desktop hold (docs/REMOTE-PROTOCOL.md section 9): a compromised rendezvous cannot forge a
// "password prompt", because a push this worker cannot open is discarded, never shown.
const DB_NAME = 'relay-remote';
const STORE = 'device';

function stored(key) {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => request.result.createObjectStore(STORE);
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const db = request.result;
      const get = db.transaction(STORE, 'readonly').objectStore(STORE).get(key);
      get.onerror = () => reject(get.error);
      get.onsuccess = () => resolve(get.result || null);
    };
  });
}

async function openSealed(blob) {
  const key = await stored('push-key');
  if (!key) return null;
  try {
    const plain = await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv: blob.slice(0, 12), additionalData: new TextEncoder().encode('relay-push-v1') },
      key, blob.slice(12));
    const body = JSON.parse(new TextDecoder().decode(plain));
    return body && typeof body === 'object' ? body : null;
  } catch {
    return null;               // not ours to show: a push we cannot open is discarded
  }
}

self.addEventListener('push', (event) => {
  event.waitUntil((async () => {
    const body = event.data ? await openSealed(await event.data.arrayBuffer()) : null;
    if (!body) return;
    await self.registration.showNotification(body.title || 'Relay', {
      body: body.body || '',
      tag: body.kind || 'relay',      // a second "agent finished" replaces the first
      renotify: false,
      // Relay's own mark rather than the browser's default glyph. Both files are local to this
      // origin and are the ones the deploy ships (app/icons/, rendered from app/icon.svg); a
      // notification must never fetch an image from anywhere else, because the URL would be a
      // lock-screen-triggered request out of this app's control.
      icon: './icons/icon-192.png',
      badge: './icons/icon-192.png',
      data: { pane: body.pane },
    });
  })());
});

// Tapping it opens that pane, not the inbox. The pane id is the one thing in the sealed body
// that names anything (section 9.2), and it is an opaque desktop id — no title, no cwd, no
// command — so passing it on carries nothing further than the notification already did.
//
// Two ways in, because a service worker has two cases. A page that is already open is focused and
// **posted** the id. A page that is not gets `?pane=<id>`: a window created by `openWindow` has no
// `message` listener for however long its module takes to load, so posting to it is a race, and
// app.js drops the query from the URL as soon as it has read it.
self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const data = event.notification.data || {};
  const pane = typeof data.pane === 'string' ? data.pane : '';
  event.waitUntil((async () => {
    const all = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    for (const client of all) {
      if ('focus' in client) {
        if (pane) client.postMessage({ t: 'open_pane', pane });
        return client.focus();
      }
    }
    return self.clients.openWindow(pane ? `./?pane=${encodeURIComponent(pane)}` : './');
  })());
});

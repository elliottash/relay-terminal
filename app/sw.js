// SPDX-License-Identifier: GPL-3.0-or-later
// Enough of a service worker to make the app installable, which is what iOS requires before it
// will deliver Web Push. It deliberately does not cache: a stale copy of a client that holds
// cryptographic keys is not something to keep around, and the app is tiny.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

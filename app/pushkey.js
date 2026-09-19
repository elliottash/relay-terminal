// SPDX-License-Identifier: AGPL-3.0-or-later
// A push subscription is made under one rendezvous's VAPID key, and the desktop sends each
// device's pushes through the rendezvous that device subscribed through (docs/REMOTE-PROTOCOL.md
// section 9). When this phone reaches its desktop through a rendezvous whose key is not the one
// its subscription was made under — the desktop moved to relay-terminal.ai, or back, or the local
// server started with a new key — the subscription is renewed under the new key and sent again,
// which moves the device's push origin. Permission is already granted, so nothing is asked.
//
// No DOM and no imports, so tests/push_key_peer.mjs runs it under Node with fakes.

export function keyBytes(text) {
  const base64 = String(text).replace(/-/g, '+').replace(/_/g, '/');
  const padded = base64 + '='.repeat((4 - (base64.length % 4)) % 4);
  const raw = atob(padded);
  return Uint8Array.from(raw, (c) => c.charCodeAt(0));
}

function same(a, b) {
  if (!a || !b || a.length !== b.length) return false;
  for (let i = 0; i < a.length; i += 1) if (a[i] !== b[i]) return false;
  return true;
}

// The key a subscription was made under: what this page stored when it subscribed, or failing
// that what the browser reports. null when neither says, and then nothing is renewed: guessing
// would renew on every connection.
export function madeUnder(subscription, stored) {
  if (stored) return keyBytes(stored);
  const key = subscription && subscription.options && subscription.options.applicationServerKey;
  return key ? new Uint8Array(key) : null;
}

// `fetchVapid()` → the rendezvous's key now (base64url); `subscribe(bytes)` → a fresh
// subscription (the caller unsubscribes the old one first, which a browser requires before a
// different key); `report(subscription, vapid)` stores the key and sends `push_subscribe`.
// Resolves to true when it renewed.
export async function renewIfKeyMoved({ subscription, stored, fetchVapid, subscribe, report }) {
  if (!subscription) return false;
  const before = madeUnder(subscription, stored);
  if (!before) return false;
  const vapid = await fetchVapid();
  if (!vapid || same(before, keyBytes(vapid))) return false;
  const fresh = await subscribe(keyBytes(vapid));
  await report(fresh, vapid);
  return true;
}

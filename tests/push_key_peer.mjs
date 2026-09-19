// SPDX-License-Identifier: AGPL-3.0-or-later
// app/pushkey.js under Node: the phone's half of per-device push origins (section 9).
//
// `node tests/push_key_peer.mjs <old vapid, base64url> <new vapid, base64url>` runs the self-heal
// against fakes and prints what happened, as JSON, for tests/test_remote_hosted_address.py.
import { renewIfKeyMoved, keyBytes, madeUnder } from '../app/pushkey.js';

const [, , oldVapid, newVapid] = process.argv;
const results = {};

function fakeSubscription(vapid, withOptions = true) {
  return {
    endpoint: `https://push.example/${vapid.slice(0, 8)}`,
    options: withOptions ? { applicationServerKey: keyBytes(vapid).buffer } : {},
  };
}

async function scenario(name, { subscription, stored, current }) {
  const calls = { fetched: 0, subscribed: [], reported: [] };
  const renewed = await renewIfKeyMoved({
    subscription,
    stored,
    fetchVapid: async () => { calls.fetched += 1; return current; },
    subscribe: async (key) => {
      calls.subscribed.push(Buffer.from(key).toString('base64url'));
      return { endpoint: 'https://push.example/fresh' };
    },
    report: async (fresh, vapid) => { calls.reported.push({ endpoint: fresh.endpoint, vapid }); },
  });
  results[name] = { renewed, ...calls };
}

// The desktop moved: the rendezvous it is reached through has another key. Renewed under it.
await scenario('moved', { subscription: fakeSubscription(oldVapid), stored: oldVapid,
  current: newVapid });
// Same rendezvous: nothing happens beyond the one key fetch.
await scenario('same', { subscription: fakeSubscription(oldVapid), stored: oldVapid,
  current: oldVapid });
// A subscription made before this page stored the key: the browser's own record is used.
await scenario('browser_only', { subscription: fakeSubscription(oldVapid), stored: null,
  current: newVapid });
// Nothing says which key it was made under: never renew on a guess.
await scenario('unknown', { subscription: fakeSubscription(oldVapid, false), stored: null,
  current: newVapid });
// Not subscribed at all: nothing, not even a fetch.
await scenario('none', { subscription: null, stored: null, current: newVapid });

results.made_under = Buffer.from(madeUnder(fakeSubscription(oldVapid), null)).toString('base64url');
process.stdout.write(JSON.stringify(results));

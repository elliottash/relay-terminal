// SPDX-License-Identifier: AGPL-3.0-or-later
// app/outbox.js under Node: the queue-and-replay rules of REMOTE-PROTOCOL.md section 7, without a
// browser. The browser test (tests/test_remote_browser.py) proves the same thing end to end over
// a real drop; this one pins the rules that are cheap to get subtly wrong — what may wait, what
// may not, and that a replay is one send and not two.
//
// `node tests/outbox_peer.mjs` prints the scenarios as JSON for tests/test_pane_view.py.
import { webcrypto } from 'node:crypto';
import { Outbox, QUEUEABLE, pendingLine } from '../app/outbox.js';

// Node 18+ already has `crypto` as a getter-only global; older ones do not.
if (!globalThis.crypto) {
  Object.defineProperty(globalThis, 'crypto', { value: webcrypto, configurable: true });
}

const results = {};

// A transport that can be switched off, and that records every message it actually carried.
function transport() {
  const sent = [];
  let up = true;
  return {
    sent,
    up: () => up,
    drop: () => { up = false; },
    restore: () => { up = true; },
    send: async (message) => {
      if (!up) throw new Error('not connected.');
      sent.push(message);
    },
  };
}

// A compose typed while the socket is down waits, and goes out once on flush — with the same
// msg_id it was given, which is what makes the desktop's at-most-once cache work.
{
  const link = transport();
  const notes = [];
  const outbox = new Outbox({ send: link.send, online: link.up, onChange: (p) => notes.push(p.length) });
  link.drop();
  const first = await outbox.post({ t: 'compose', pane: 'p1', text: 'hello', msg_id: 'm1' });
  const line = pendingLine(outbox.counts());
  link.restore();
  const flushed = await outbox.flush();
  results.queued_then_sent = {
    first, line, flushed, notes,
    sent: link.sent, pendingAfter: outbox.pending.length,
  };
}

// Two flushes racing — a `pageshow` and an `online` in the same tick, which is exactly what a
// phone coming out of a pocket does — carry the message once, not twice.
{
  const link = transport();
  const outbox = new Outbox({ send: link.send, online: link.up });
  link.drop();
  await outbox.post({ t: 'compose', pane: 'p1', text: 'once', msg_id: 'm1' });
  await outbox.post({ t: 'agent_stop', pane: 'p1', msg_id: 'm2' });
  link.restore();
  const counts = await Promise.all([outbox.flush(), outbox.flush(), outbox.flush()]);
  results.a_double_flush_sends_once = {
    counts, sent: link.sent.map((m) => m.msg_id), pendingAfter: outbox.pending.length,
  };
}

// Retrying the same message while still offline is the same message, not a second prompt.
{
  const link = transport();
  const outbox = new Outbox({ send: link.send, online: link.up });
  link.drop();
  await outbox.post({ t: 'compose', pane: 'p1', text: 'hello', msg_id: 'm1' });
  await outbox.post({ t: 'compose', pane: 'p1', text: 'hello', msg_id: 'm1' });
  results.a_repeat_is_not_two = { pending: outbox.pending.length };
}

// Never queued: a password line and a keystroke (sections 6.6 and 6.7). They fail where they
// stand, because a password answered into a prompt that ended, or a ^C replayed into whatever is
// in the foreground twenty minutes later, is worse than a failure the person can see.
{
  const link = transport();
  const outbox = new Outbox({ send: link.send, online: link.up });
  link.drop();
  const refused = {};
  for (const message of [{ t: 'secret_input', pane: 'p1', nonce: 'n' }, { t: 'keys', pane: 'p1' },
    { t: 'line', pane: 'p1' }, { t: 'pane_focus', pane: 'p1' }]) {
    try {
      await outbox.post(message);
      refused[message.t] = 'sent';
    } catch (error) {
      refused[message.t] = error.message;
    }
  }
  results.never_queued = { refused, pending: outbox.pending.length,
    queueable: [...QUEUEABLE].sort() };
}

// A message posted with no msg_id of its own is given one, so anything that waits can be
// de-duplicated on the desktop.
{
  const link = transport();
  const outbox = new Outbox({ send: link.send, online: link.up });
  link.drop();
  await outbox.post({ t: 'agent_stop', pane: 'p1' });
  results.an_id_is_minted = { msg_id: typeof outbox.pending[0].msg_id,
    length: outbox.pending[0].msg_id.length };
}

// A flush that cannot get the first one out keeps the order: nothing behind it jumps the queue.
{
  const link = transport();
  const outbox = new Outbox({ send: link.send, online: link.up });
  link.drop();
  await outbox.post({ t: 'compose', pane: 'p1', text: 'one', msg_id: 'm1' });
  await outbox.post({ t: 'compose', pane: 'p1', text: 'two', msg_id: 'm2' });
  const flushed = await outbox.flush();          // still down
  link.restore();
  const later = await outbox.flush();
  results.order_is_kept = { flushed, later, sent: link.sent.map((m) => m.text) };
}

// The words under the prompt box.
results.lines = {
  none: pendingLine({}),
  one: pendingLine({ compose: 1 }),
  many: pendingLine({ compose: 3 }),
  mixed: pendingLine({ compose: 2, agent_stop: 1 }),
};

process.stdout.write(JSON.stringify(results));

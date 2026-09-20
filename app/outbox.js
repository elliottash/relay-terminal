// SPDX-License-Identifier: AGPL-3.0-or-later
// Queue and replay across a drop (docs/REMOTE-PROTOCOL.md section 7).
//
// iOS closes the WebSocket within seconds of the app leaving the foreground, and it does it
// without telling the page anything a person would notice. What used to happen then is that a
// prompt typed on the bus was handed to `rrp.send`, rejected with "not connected", swallowed by
// the `.catch(() => {})` every caller had, and simply never happened. The person had watched
// themselves send it.
//
// So sending goes through here instead. A message that cannot go out now is **kept**, the caller
// is told it was queued rather than sent, and it goes out once on the next resume. "Once" is what
// `msg_id` is for: section 7 says the desktop applies the last 256 ids per device at most once, so
// a message that reached the socket just as the link died and is sent again on reconnect is
// applied a single time. Every queued message therefore carries one, minted here if the caller
// did not bring its own.
//
// Two rules that are not about plumbing:
//
// * **Only `compose` and `agent_stop` are ever queued.** They are the two the person means to
//   happen whenever the desktop next hears from us — a prompt, and stopping a turn. Everything
//   else is about *now*: `keys` and `secret_input` in particular must never be queued (section
//   6.6, 6.7 — a password typed at a prompt that has since ended, or a control byte replayed into
//   whatever program is in the foreground twenty minutes later, are both worse than a failure),
//   and `pane_focus`, `resume`, `screen_get` and their kind describe a session that no longer
//   exists by the time the queue drains. Anything not on the list is sent straight through and
//   fails when it fails.
// * **Nothing is dropped from the queue until its send has resolved.** A flush that overlaps
//   another would otherwise take the same head twice.
//
// The queue is in memory: a reload is a new session and re-sending what a previous page had
// staged, without the box that shows it, is not something the person could have checked.

// The only two message types that mean anything after the link comes back.
export const QUEUEABLE = new Set(['compose', 'agent_stop']);

// 72 random bits, the shape app.js's composer and app/pane.js both already send.
function randomId() {
  const bytes = crypto.getRandomValues(new Uint8Array(9));
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export class Outbox {
  // `send` is the transport (rrp.send), `online` says whether it is worth trying, `onChange` is
  // called whenever what is waiting changes so the caller can draw its status line, and `newId`
  // exists so a test can make the ids predictable.
  constructor({ send, online, onChange = () => {}, newId = randomId }) {
    this.sendNow = send;
    this.online = online;
    this.onChange = onChange;
    this.newId = newId;
    this.queue = [];
    this.flushing = null;
  }

  // What is waiting, oldest first. A copy: the caller draws it, it does not own it.
  get pending() {
    return this.queue.map((message) => ({ ...message }));
  }

  // How many of each type are waiting, for the status line.
  counts() {
    const counts = {};
    for (const message of this.queue) counts[message.t] = (counts[message.t] || 0) + 1;
    return counts;
  }

  // Send, or keep. Resolves 'sent' or 'queued'; a message that may not be queued rejects with
  // whatever the transport said, exactly as a direct `rrp.send` would.
  async post(message) {
    if (!QUEUEABLE.has(message?.t)) return this.sendNow(message).then(() => 'sent');
    const staged = message.msg_id ? { ...message } : { ...message, msg_id: this.newId() };
    if (this.online()) {
      try {
        await this.sendNow(staged);
        return 'sent';
      } catch {
        // It never reached the socket, or it reached a socket that was already dead. Either way
        // it is kept, and `msg_id` is what makes the second attempt safe.
      }
    }
    this.keep(staged);
    return 'queued';
  }

  keep(message) {
    // A retry of something already waiting is the same message, not a second one.
    if (this.queue.some((waiting) => waiting.msg_id === message.msg_id)) return;
    this.queue.push(message);
    this.onChange(this.pending);
  }

  // Drain, oldest first, stopping at the first one that will not go. Concurrent calls share the
  // one pass, so nothing is taken twice.
  flush() {
    if (this.flushing) return this.flushing;
    this.flushing = this.#drain().finally(() => { this.flushing = null; });
    return this.flushing;
  }

  async #drain() {
    let sent = 0;
    while (this.queue.length && this.online()) {
      const head = this.queue[0];
      try {
        await this.sendNow(head);
      } catch {
        break;                        // still ours; the next resume tries again
      }
      // Only now, and only if it is still the head: nothing else removes from the queue, but
      // this is the invariant the "exactly once" claim rests on, so it is checked rather than
      // assumed.
      if (this.queue[0] === head) this.queue.shift();
      sent += 1;
    }
    if (sent) this.onChange(this.pending);
    return sent;
  }

  // Unpairing, or a desktop that revoked this device: what was staged is not going anywhere.
  clear() {
    if (!this.queue.length) return;
    this.queue = [];
    this.onChange(this.pending);
  }
}

// The status line's words, or '' when nothing is waiting. It is a line and not a modal on
// purpose: the person is mid-sentence on a bus, and a sheet over the box they are typing in to
// tell them the socket dropped is the thing that loses the sentence.
export function pendingLine(counts) {
  const composes = counts.compose || 0;
  const stops = counts.agent_stop || 0;
  const parts = [];
  if (composes) parts.push(composes === 1 ? '1 message' : `${composes} messages`);
  if (stops) parts.push(stops === 1 ? '1 Stop' : `${stops} Stops`);
  return parts.length ? `Sends when back online · ${parts.join(', ')}` : '';
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// A stand-in for app/rrp.js, for tests/test_board_view.py.
//
// The test's static server answers `/app/rrp.js` with this file and `/app/rrp-real.js` with the
// real one, so the page under test is the real client — app/index.html, app/app.js, app/board.js,
// the outbox, the viewport — paired to a desktop that is this object. It is "connected" without a
// socket: `send` records what the client would have put on the wire (and fails with the real
// transport's words while the link is "down"), and the test plays the hub's side with `emit`.
//
//   ?features=panes,agent,board   what `welcome` offers (default: no `board`)
//   ?capability=full|agent|view   this device's level (default: full)
export { b64, un64, fingerprint, storedValue, storeValue, dropValue, saveDevice, loadGuest,
  saveGuest, forgetGuest } from './rrp-real.js';

const params = new URLSearchParams(location.search);
const FEATURES = (params.get('features') || 'panes,agent,compose,pane_state').split(',').filter(Boolean);
const CAPABILITY = params.get('capability') || 'full';

export async function loadDevice() {
  return { deviceId: 'dev-test', desktopName: 'spark', capability: CAPABILITY };
}

export async function forgetDevice() {}

export class Rrp extends EventTarget {
  constructor() {
    super();
    this.origin = location.origin;
    this.session = null;
    this.record = null;
    this.sent = [];
    this.refuseNext = null;
    window.fakeRrp = this;
  }

  static parsePairFragment() { throw new Error('the fake transport does not pair.'); }

  emit(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  async connect(record) {
    this.record = record;
    this.session = { fake: true };
    this.emit('welcome', { capability: CAPABILITY, features: FEATURES, password_entry: false });
    this.emit('panes', { items: window.fakePanes || [] });
    return record;
  }

  async resume() { return true; }

  async send(message) {
    if (!this.session) throw new Error('not connected.');
    this.sent.push(JSON.parse(JSON.stringify(message)));
  }

  once(name, timeout = 15000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('timed out.')), timeout);
      this.addEventListener(name, (event) => { clearTimeout(timer); resolve(event.detail); }, { once: true });
    });
  }

  close() { this.session = null; }

  // ---- the test's side ------------------------------------------------------------------------

  // The link drops the way iOS drops it: no session, and the app is told. It comes back the way
  // the app brings it back: `online` fires, app.js reconnects through `connect` above, resumes and
  // flushes what was waiting. `clean`, so the app does not start that on its own mid-test.
  drop() {
    this.session = null;
    this.emit('closed', { code: 1006, reason: '', clean: true });
  }

  // A board event as the hub would send it.
  board(rid, event) {
    this.emit('board_event', { t: 'board_event', rid, event });
  }

  boardRequests() {
    return this.sent.filter((message) => message.t === 'board_request');
  }
}

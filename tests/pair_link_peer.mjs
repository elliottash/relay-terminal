// SPDX-License-Identifier: AGPL-3.0-or-later
// Parse pairing fragments with the browser's own code, driven by tests/test_remote_wire.py.
// A pairing link is the one piece of input the client gets from the outside world, and the
// shapes it arrives in depend on whichever QR reader or link handler opened it.
import { Rrp } from '../app/rrp.js';

const b64 = (bytes) => Buffer.from(bytes).toString('base64')
  .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');

const cases = JSON.parse(process.argv[2]);
const results = cases.map((fragment) => {
  try {
    const link = Rrp.parsePairFragment(fragment);
    return { ok: true, room: link.room, desktop: b64(link.desktopPublic),
             secret: b64(link.secret) };
  } catch (error) {
    return { ok: false, error: String(error.message) };
  }
});
process.stdout.write(JSON.stringify(results));

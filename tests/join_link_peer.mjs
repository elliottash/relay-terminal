// SPDX-License-Identifier: AGPL-3.0-or-later
// Parse invite fragments with the browser's own code, driven by tests/test_remote_wire.py.
// An invite link (docs/REMOTE-PROTOCOL.md section 10.2) reaches a guest through whatever chat app
// or mail client the owner sent it in, so the shapes it arrives in are not the owner's to choose.
// The second half of each case asks the pairing parser the same question: `s` and `i` name
// different secrets, and neither link may parse as the other kind.
import { Rrp } from '../app/rrp.js';

const b64 = (bytes) => Buffer.from(bytes).toString('base64')
  .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');

const parse = (how, fragment) => {
  try {
    const link = how(fragment);
    return { ok: true, room: link.room, desktop: b64(link.desktopPublic),
             secret: b64(link.secret) };
  } catch (error) {
    return { ok: false, error: String(error.message) };
  }
};

const cases = JSON.parse(process.argv[2]);
const results = cases.map((fragment) => ({
  ...parse(Rrp.parseInviteFragment, fragment),
  asPairing: parse(Rrp.parsePairFragment, fragment).ok,
}));
process.stdout.write(JSON.stringify(results));

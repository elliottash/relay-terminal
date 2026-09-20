// SPDX-License-Identifier: AGPL-3.0-or-later
// Put connect tokens on `/v1/connect` URLs with the browser's own code, driven by
// tests/test_remote_security.py. The token (protocol section 8) is minted in Python and checked
// in Python; what the web client adds is the URL it puts the token in, and `Rrp.withConnectToken`
// is the one place every socket the client opens goes through.
import { Rrp } from '../app/rrp.js';

const cases = JSON.parse(process.argv[2]);
const results = cases.map(({ url, record }) => {
  try {
    return { ok: true, url: Rrp.withConnectToken(url, record) };
  } catch (error) {
    return { ok: false, error: String(error.message) };
  }
});
process.stdout.write(JSON.stringify(results));

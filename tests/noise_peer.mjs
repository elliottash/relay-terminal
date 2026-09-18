// SPDX-License-Identifier: GPL-3.0-or-later
// The client half of the RRP handshake, driven from tests/test_remote_noise.py so the browser
// implementation (app/noise.js) and the desktop one (remote/noise.py) are checked against each
// other rather than each against itself. Reads one JSON command per line on stdin, writes one
// JSON reply per line on stdout.
import { createInterface } from 'node:readline';
import { Initiator, generateKeypair, exportPublic } from '../app/noise.js';

const b64 = (bytes) => Buffer.from(bytes).toString('base64');
const un64 = (text) => new Uint8Array(Buffer.from(text, 'base64'));

let initiator = null;
let session = null;
let staticKey = null;
let staticPublic = null;

const commands = {
  async keypair() {
    const pair = await generateKeypair(true);
    staticKey = pair.privateKey;
    staticPublic = await exportPublic(pair.publicKey);
    return { public: b64(staticPublic) };
  },
  async start({ desktop }) {
    initiator = new Initiator(staticKey, staticPublic, un64(desktop));
    return {};
  },
  async message1({ payload }) {
    return { message: b64(await initiator.writeMessage1(un64(payload || ''))) };
  },
  async message2({ message }) {
    const result = await initiator.readMessage2(un64(message));
    session = result.session;
    return { payload: b64(result.payload), handshake_hash: b64(session.handshakeHash) };
  },
  async encrypt({ plaintext }) {
    return { ciphertext: b64(await session.encrypt(un64(plaintext))) };
  },
  async decrypt({ ciphertext }) {
    return { plaintext: b64(await session.decrypt(un64(ciphertext))) };
  },
};

const lines = createInterface({ input: process.stdin });
for await (const line of lines) {
  if (!line.trim()) continue;
  const request = JSON.parse(line);
  try {
    const reply = await commands[request.cmd](request);
    process.stdout.write(JSON.stringify({ ok: true, ...reply }) + '\n');
  } catch (error) {
    process.stdout.write(JSON.stringify({ ok: false, error: String(error && error.message || error) }) + '\n');
  }
}

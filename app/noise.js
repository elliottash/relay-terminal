// SPDX-License-Identifier: GPL-3.0-or-later
// Noise_IK_25519_AESGCM_SHA256, initiator half, on WebCrypto alone.
//
// The client is always the initiator: it learned the desktop's static public key from the pairing
// QR code. This file has no dependencies on purpose — WebCrypto gives us X25519, AES-GCM, SHA-256
// and HMAC, which is exactly the suite, so nothing third-party ever touches a key.
// The desktop half is remote/noise.py; tests/test_remote_noise.py cross-checks the two.

const subtle = globalThis.crypto.subtle;

export const PROTOCOL = new TextEncoder().encode('Noise_IK_25519_AESGCM_SHA256');
export const PROLOGUE = new TextEncoder().encode('RRP/1');
const HASHLEN = 32, DHLEN = 32, TAGLEN = 16;
const REKEY_AFTER = 2 ** 20;

export function concat(...parts) {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}

export function equalBytes(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a[i] ^ b[i];
  return diff === 0;
}

async function sha256(data) {
  return new Uint8Array(await subtle.digest('SHA-256', data));
}

async function hmac(key, data) {
  const k = await subtle.importKey('raw', key, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return new Uint8Array(await subtle.sign('HMAC', k, data));
}

async function hkdf(chainingKey, ikm, outputs) {
  const temp = await hmac(chainingKey, ikm);
  const o1 = await hmac(temp, new Uint8Array([1]));
  const o2 = await hmac(temp, concat(o1, new Uint8Array([2])));
  if (outputs === 2) return [o1, o2];
  return [o1, o2, await hmac(temp, concat(o2, new Uint8Array([3])))];
}

// Noise's AESGCM nonce: 32 zero bits then a 64-bit big-endian counter.
function nonceBytes(counter) {
  const out = new Uint8Array(12);
  new DataView(out.buffer).setBigUint64(4, BigInt(counter), false);
  return out;
}

// ---- X25519 ---------------------------------------------------------------------------------

export async function generateKeypair(extractable = false) {
  const pair = await subtle.generateKey({ name: 'X25519' }, extractable, ['deriveBits']);
  return { privateKey: pair.privateKey, publicKey: pair.publicKey };
}

export async function exportPublic(publicKey) {
  return new Uint8Array(await subtle.exportKey('raw', publicKey));
}

export async function importPublic(raw) {
  if (raw.length !== DHLEN) throw new Error('a public key must be 32 bytes.');
  return subtle.importKey('raw', raw, { name: 'X25519' }, true, []);
}

async function dh(privateKey, publicRaw) {
  const pub = publicRaw instanceof Uint8Array ? await importPublic(publicRaw) : publicRaw;
  const bits = new Uint8Array(await subtle.deriveBits({ name: 'X25519', public: pub }, privateKey, 256));
  // RFC 7748: an all-zero output means a low-order point. Reject it.
  if (bits.every((b) => b === 0)) throw new Error('X25519 exchange produced an all-zero shared secret.');
  return bits;
}

// ---- CipherState ----------------------------------------------------------------------------

class CipherState {
  constructor(key) { this.key = key || null; this.nonce = 0; this.imported = null; }

  async #aesKey() {
    if (!this.imported) {
      this.imported = await subtle.importKey('raw', this.key, 'AES-GCM', true, ['encrypt', 'decrypt']);
    }
    return this.imported;
  }

  // The nonce is reserved synchronously, before the first await. WebCrypto is async, so two
  // overlapping calls would otherwise both read the same counter and produce two frames with the
  // same nonce — which is both a break of the cipher's contract and an instant session failure.
  async encrypt(ad, plaintext) {
    if (!this.key) return plaintext;
    const nonce = this.nonce++;
    const key = await this.#aesKey();
    const params = { name: 'AES-GCM', iv: nonceBytes(nonce), tagLength: 128 };
    if (ad && ad.length) params.additionalData = ad;
    return new Uint8Array(await subtle.encrypt(params, key, plaintext));
  }

  async decrypt(ad, ciphertext) {
    if (!this.key) return ciphertext;
    const nonce = this.nonce++;
    const key = await this.#aesKey();
    const params = { name: 'AES-GCM', iv: nonceBytes(nonce), tagLength: 128 };
    if (ad && ad.length) params.additionalData = ad;
    try {
      return new Uint8Array(await subtle.decrypt(params, key, ciphertext));
    } catch {
      throw new Error('authentication failed.');
    }
  }

  async rekey() {
    const key = await this.#aesKey();
    const iv = new Uint8Array(12).fill(0xff); iv.set([0, 0, 0, 0], 0);
    const out = new Uint8Array(await subtle.encrypt({ name: 'AES-GCM', iv, tagLength: 128 }, key, new Uint8Array(32)));
    this.key = out.slice(0, 32);
    this.imported = null;
    this.nonce = 0;
  }
}

// ---- SymmetricState -------------------------------------------------------------------------

class SymmetricState {
  constructor() {
    // The suite name is 28 bytes, so h is the name zero-padded to 32 (Noise section 5.2).
    this.h = new Uint8Array(HASHLEN);
    this.h.set(PROTOCOL);
    this.ck = this.h.slice();
    this.cipher = new CipherState(null);
  }

  async mixKey(ikm) {
    const [ck, temp] = await hkdf(this.ck, ikm, 2);
    this.ck = ck;
    this.cipher = new CipherState(temp);
  }

  async mixHash(data) { this.h = await sha256(concat(this.h, data)); }

  async encryptAndHash(plaintext) {
    const out = await this.cipher.encrypt(this.h, plaintext);
    await this.mixHash(out);
    return out;
  }

  async decryptAndHash(ciphertext) {
    const out = await this.cipher.decrypt(this.h, ciphertext);
    await this.mixHash(ciphertext);
    return out;
  }

  async split() {
    const [k1, k2] = await hkdf(this.ck, new Uint8Array(0), 2);
    return [new CipherState(k1), new CipherState(k2)];
  }
}

// ---- Session --------------------------------------------------------------------------------

export class Session {
  constructor(send, recv, handshakeHash, remoteStatic) {
    this.send = send; this.recv = recv;
    this.handshakeHash = handshakeHash; this.remoteStatic = remoteStatic;
    this.sent = 0; this.received = 0;
  }

  async encrypt(plaintext) {
    if (this.sent && this.sent % REKEY_AFTER === 0) await this.send.rekey();
    this.sent += 1;
    return this.send.encrypt(new Uint8Array(0), plaintext);
  }

  async decrypt(ciphertext) {
    if (this.received && this.received % REKEY_AFTER === 0) await this.recv.rekey();
    this.received += 1;
    return this.recv.decrypt(new Uint8Array(0), ciphertext);
  }
}

// ---- Initiator ------------------------------------------------------------------------------

export class Initiator {
  // `staticKey` is a non-extractable CryptoKey; `remoteStatic` is the desktop's 32 raw bytes.
  constructor(staticKey, staticPublic, remoteStatic) {
    if (remoteStatic.length !== DHLEN) throw new Error("the desktop's static public key must be 32 bytes.");
    this.s = staticKey;
    this.sPublic = staticPublic;
    this.rs = remoteStatic;
    this.e = null;
    this.state = new SymmetricState();
    this.ready = (async () => {
      await this.state.mixHash(PROLOGUE);
      await this.state.mixHash(this.rs);   // pre-message: "<- s"
    })();
  }

  async writeMessage1(payload = new Uint8Array(0)) {
    await this.ready;
    const pair = await subtle.generateKey({ name: 'X25519' }, true, ['deriveBits']);
    this.e = pair.privateKey;
    const epub = await exportPublic(pair.publicKey);
    await this.state.mixHash(epub);                            // e
    await this.state.mixKey(await dh(this.e, this.rs));        // es
    const spub = await this.state.encryptAndHash(this.sPublic); // s
    await this.state.mixKey(await dh(this.s, this.rs));        // ss
    return concat(epub, spub, await this.state.encryptAndHash(payload));
  }

  async readMessage2(message) {
    if (message.length < DHLEN + TAGLEN) throw new Error('handshake message 2 is too short.');
    const re = message.slice(0, DHLEN);
    await this.state.mixHash(re);                       // e
    await this.state.mixKey(await dh(this.e, re));      // ee
    await this.state.mixKey(await dh(this.s, re));      // se
    const payload = await this.state.decryptAndHash(message.slice(DHLEN));
    const [c1, c2] = await this.state.split();
    return { payload, session: new Session(c1, c2, this.state.h, this.rs) };
  }
}

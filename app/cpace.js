// SPDX-License-Identifier: GPL-3.0-or-later
// CPACE-X25519-SHA512 (draft-irtf-cfrg-cpace-21, initiator-responder mode), on WebCrypto alone.
//
// The PAKE behind joining a shared pane with a meeting code and a four-digit PIN (card #97EG).
// A PIN is too small to hash into a key — anyone holding a transcript could try all ten thousand
// offline — so CPace turns it into a curve generator and runs Diffie-Hellman on that: each live
// attempt tests one PIN, and an observer tests none. The guest's browser is always the initiator.
//
// X25519 is the only group operation, and WebCrypto has it. The one thing it lacks is Elligator2
// (hashing the PIN onto the curve), which is a few lines of BigInt arithmetic mod 2^255 - 19.
// No dependencies on purpose, like app/noise.js. The desktop half is remote/cpace.py;
// tests/cpace_vectors.mjs checks this file against the draft's vectors and against the Python.
//
// ISK is only the shared secret. The caller must check key-confirmation tags derived from it
// before trusting it; that is the wire protocol's job, not this file's.

const subtle = globalThis.crypto.subtle;
const enc = new TextEncoder();

const DSI = enc.encode('CPace255');          // G_X25519.DSI
const DSI_ISK = enc.encode('CPace255_ISK');
const FIELD_BYTES = 32;
const HASH_BLOCK = 128;                      // SHA-512's input block size, H.s_in_bytes

const P = (1n << 255n) - 19n;                // Curve25519's field
const A = 486662n;                           // v^2 = u^3 + A u^2 + u
const Z = 2n;                                // Elligator2's non-square for this field

export class CPaceError extends Error {}

function concat(...parts) {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}

// ---- string encoding (draft appendix A.1, A.2, A.3.4) ---------------------------------------

function prependLen(data) {
  const len = [];
  let n = data.length;
  do {
    const byte = n & 0x7f;
    n = Math.floor(n / 128);
    len.push(n ? byte | 0x80 : byte);
  } while (n);
  return concat(new Uint8Array(len), data);
}

function lvCat(...parts) { return concat(...parts.map(prependLen)); }

function generatorString(prs, ci, sid) {
  // The zero padding fills SHA-512's first block with DSI and PRS, so the PIN's length does not
  // change how many blocks get hashed.
  const zpad = Math.max(0, HASH_BLOCK - 1 - prependLen(prs).length - prependLen(DSI).length);
  return lvCat(DSI, prs, new Uint8Array(zpad), ci, sid);
}

// The initiator's message always goes first.
function transcriptIr(ya, ada, yb, adb) { return concat(lvCat(ya, ada), lvCat(yb, adb)); }

// ---- the group (draft section 8.2) ----------------------------------------------------------

const mod = (x) => ((x % P) + P) % P;

function powMod(base, exp) {
  let result = 1n;
  base = mod(base);
  while (exp > 0n) {
    if (exp & 1n) result = (result * base) % P;
    base = (base * base) % P;
    exp >>= 1n;
  }
  return result;
}

// P is prime, so the inverse is x^(P-2).
const inv = (x) => powMod(x, P - 2n);

function fromLittleEndian(bytes) {
  let n = 0n;
  for (let i = bytes.length - 1; i >= 0; i--) n = (n << 8n) | BigInt(bytes[i]);
  return n;
}

function toLittleEndian(n) {
  const out = new Uint8Array(FIELD_BYTES);
  for (let i = 0; i < FIELD_BYTES; i++) { out[i] = Number(n & 0xffn); n >>= 8n; }
  return out;
}

// RFC 9380 map_to_curve_elligator2 for Curve25519, u-coordinate only (draft A.5).
function elligator2(r) {
  const denominator = mod(1n + Z * r * r);
  // 1 + 2r^2 = 0 would need -1/2 to be a square; it is not mod p, but RFC 9380 defines the case.
  const x1 = denominator ? mod(-A * inv(denominator)) : mod(-A);
  const gx1 = mod(x1 * x1 * x1 + A * x1 * x1 + x1);
  const square = powMod(gx1, (P - 1n) / 2n) !== P - 1n;   // Euler's criterion; 0 counts as square
  return square ? x1 : mod(-x1 - A);
}

// G_X25519.calculate_generator: the 32-byte u-coordinate g that the PIN picks.
export async function generator(prs, ci, sid) {
  const digest = new Uint8Array(await subtle.digest('SHA-512', generatorString(prs, ci, sid)));
  // RFC 7748 decodeUCoordinate: little endian with bit 255 cleared (draft A.4).
  const hashed = digest.slice(0, FIELD_BYTES);
  hashed[31] &= 0x7f;
  return toLittleEndian(elligator2(mod(fromLittleEndian(hashed))));
}

// A raw 32-byte X25519 scalar as a PKCS#8 key: WebCrypto will not import a raw private key.
const PKCS8_PREFIX = new Uint8Array([0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b,
  0x65, 0x6e, 0x04, 0x22, 0x04, 0x20]);

async function importScalar(scalar) {
  if (!(scalar instanceof Uint8Array) || scalar.length !== FIELD_BYTES) {
    throw new CPaceError('a CPace scalar must be 32 bytes.');
  }
  return subtle.importKey('pkcs8', concat(PKCS8_PREFIX, scalar), { name: 'X25519' }, false, ['deriveBits']);
}

// G_X25519.scalar_mult_vfy: X25519(scalar, point), refusing the identity (all zeros).
// `scalar` is a CryptoKey from importScalar or 32 raw bytes.
export async function scalarMultVfy(scalar, point) {
  if (!(point instanceof Uint8Array) || point.length !== FIELD_BYTES) {
    throw new CPaceError('a CPace point must be 32 bytes.');
  }
  const key = scalar instanceof Uint8Array ? await importScalar(scalar) : scalar;
  let out;
  try {
    const pub = await subtle.importKey('raw', point, { name: 'X25519' }, false, []);
    out = new Uint8Array(await subtle.deriveBits({ name: 'X25519', public: pub }, key, 256));
  } catch {
    // Browsers (and Node) refuse a low-order point themselves; that is the same failure.
    throw new CPaceError("the peer's point is invalid.");
  }
  if (out.every((b) => b === 0)) throw new CPaceError("the peer's point is invalid.");
  return out;
}

// ---- the protocol (draft section 7.2) -------------------------------------------------------

// One side of one attempt. Make a fresh one per attempt: the scalar must never be reused.
export class CPace {
  // `scalar` is injectable only for test vectors; otherwise sample_scalar() is 32 random bytes,
  // which X25519 clamps.
  static async create({ prs, ci, sid, initiator, ad, scalar }) {
    const self = new CPace();
    self.sid = sid;
    self.initiator = !!initiator;
    self.ad = ad;
    self.key = await importScalar(scalar || globalThis.crypto.getRandomValues(new Uint8Array(FIELD_BYTES)));
    self.message = await scalarMultVfy(self.key, await generator(prs, ci, sid));
    return self;
  }

  // The 64-byte ISK. Throws CPaceError on a malformed or low-order peer point.
  async finish(peerY, peerAd) {
    if (!(peerY instanceof Uint8Array) || peerY.length !== FIELD_BYTES) {
      throw new CPaceError("the peer's point must be 32 bytes.");
    }
    const k = await scalarMultVfy(this.key, peerY);
    const transcript = this.initiator
      ? transcriptIr(this.message, this.ad, peerY, peerAd)
      : transcriptIr(peerY, peerAd, this.message, this.ad);
    return new Uint8Array(await subtle.digest('SHA-512', concat(lvCat(DSI_ISK, this.sid, k), transcript)));
  }
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// app/cpace.js under Node, driven from tests/test_cpace.py.
//
// Two jobs. First it checks the browser implementation against the CPACE-X25519-SHA512 vectors
// published in draft-irtf-cfrg-cpace-21 appendix B.1, copied verbatim (the same ones the Python
// test checks remote/cpace.py against). Then it reads a JSON list of cases on stdin — inputs with
// fixed scalars chosen by the Python side — and prints what app/cpace.js makes of each, so the
// Python test can compare them byte for byte with remote/cpace.py.
//
// Prints one JSON object: {"failures": [...], "cases": [...]}. Exits 1 if any vector failed.
import { CPace, CPaceError, generator, scalarMultVfy } from '../app/cpace.js';

const hex = (bytes) => Buffer.from(bytes).toString('hex');
const unhex = (text) => new Uint8Array(Buffer.from(text, 'hex'));
const ascii = (text) => new TextEncoder().encode(text);

// Draft-21 appendix B.1.
const V = {
  PRS: ascii('Password'),
  CI: unhex('0b415f696e69746961746f720b425f726573706f6e646572'),
  sid: unhex('7e4b4791d6a8ef019b936c79fb7f2c57'),
  g: 'd04bf6d41f6a289632a2e929fa29bebd51092512a7829fdde7d314b62f05a73f',
  ADa: ascii('ADa'),
  ya: unhex('21b4f4bd9e64ed355c3eb676a28ebedaf6d8f17bdc365995b319097153044080'),
  Ya: '1d13c89278cdadd826f6d8d7f887701430f8380ddc17611cdd6dc989ce0c9f32',
  ADb: ascii('ADb'),
  yb: unhex('848b0779ff415f0af4ea14df9dd1d3c29ac41d836c7808896c4eba19c51ac40a'),
  Yb: '248cccf6d5cdc3646f0ad593f9e6cef4e69d4945f8372e623512ecea32185623',
  K: '5b067effbdc0b2a0e1d907b21ebb25cfedb96a852179a847c37e43ee71322c6b',
  ISK_IR: '6e19b875f7a561d6b3ca3dbb9ef42ac55de3e717881018204b8922b4d5e53bb2'
        + 'aa82c300bea7b65d2b671da71922ddf6472301b79bc270adfa8bf413285f2263',
};

// Draft-21 appendix B.1.10: scalar_mult_vfy(s, uN) = qN, where an all-zero qN means "abort".
const LOW_ORDER_S = unhex('af46e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449aff');
const ZERO = '00'.repeat(32);
const LOW_ORDER = [
  ['0000000000000000000000000000000000000000000000000000000000000000', ZERO],
  ['0100000000000000000000000000000000000000000000000000000000000000', ZERO],
  ['ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f', ZERO],
  ['e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800', ZERO],
  ['5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157', ZERO],
  ['edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f', ZERO],
  ['daffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff',
    'd8e2c776bbacd510d09fd9278b7edcd25fc5ae9adfba3b6e040e8d3b71b21806'],
  ['eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f', ZERO],
  ['dbffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff',
    'c85c655ebe8be44ba9c0ffde69f2fe10194458d137f09bbff725ce58803cdb38'],
  ['d9ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff',
    'db64dafa9b8fdd136914e61461935fe92aa372cb056314e1231bc4ec12417456'],
  ['cdeb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b880',
    'e062dcd5376d58297be2618c7498f55baa07d7e03184e8aada20bca28888bf7a'],
  ['4c9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f11d7',
    '993c6ad11c4c29da9a56f7691fd0ff8d732e49de6250b6c2e80003ff4629a175'],
];

const failures = [];
const expect = (name, got, want) => { if (got !== want) failures.push(`${name}: got ${got}, want ${want}`); };

async function refused(fn) {
  try { await fn(); return false; } catch (err) { return err instanceof CPaceError; }
}

async function draftVectors() {
  expect('g', hex(await generator(V.PRS, V.CI, V.sid)), V.g);
  const a = await CPace.create({ prs: V.PRS, ci: V.CI, sid: V.sid, initiator: true, ad: V.ADa, scalar: V.ya });
  const b = await CPace.create({ prs: V.PRS, ci: V.CI, sid: V.sid, initiator: false, ad: V.ADb, scalar: V.yb });
  expect('Ya', hex(a.message), V.Ya);
  expect('Yb', hex(b.message), V.Yb);
  expect('K(ya,Yb)', hex(await scalarMultVfy(V.ya, unhex(V.Yb))), V.K);
  expect('K(yb,Ya)', hex(await scalarMultVfy(V.yb, unhex(V.Ya))), V.K);
  expect('ISK initiator', hex(await a.finish(b.message, V.ADb)), V.ISK_IR);
  expect('ISK responder', hex(await b.finish(a.message, V.ADa)), V.ISK_IR);

  for (const [i, [u, q]] of LOW_ORDER.entries()) {
    if (q === ZERO) {
      expect(`low-order u${i} refused`, await refused(() => scalarMultVfy(LOW_ORDER_S, unhex(u))), true);
      expect(`low-order u${i} refused by finish`, await refused(() => a.finish(unhex(u), V.ADb)), true);
    } else {
      expect(`u${i}`, hex(await scalarMultVfy(LOW_ORDER_S, unhex(u))), q);
    }
  }
  expect('short point refused', await refused(() => a.finish(new Uint8Array(31), V.ADb)), true);
  expect('long point refused', await refused(() => a.finish(new Uint8Array(33), V.ADb)), true);
}

// One case from Python: {prs, ci, sid, ya, yb, ada, adb} in hex -> what this side computes.
async function runCase(c) {
  const [prs, ci, sid, ya, yb, ada, adb] = ['prs', 'ci', 'sid', 'ya', 'yb', 'ada', 'adb'].map((k) => unhex(c[k]));
  const a = await CPace.create({ prs, ci, sid, initiator: true, ad: ada, scalar: ya });
  const b = await CPace.create({ prs, ci, sid, initiator: false, ad: adb, scalar: yb });
  return {
    g: hex(await generator(prs, ci, sid)),
    Ya: hex(a.message),
    Yb: hex(b.message),
    isk_a: hex(await a.finish(b.message, adb)),
    isk_b: hex(await b.finish(a.message, ada)),
  };
}

async function readStdin() {
  let text = '';
  for await (const chunk of process.stdin) text += chunk;
  return text.trim() ? JSON.parse(text) : [];
}

try {
  await draftVectors();
} catch (err) {
  failures.push(`draft vectors threw: ${err.stack || err}`);
}
const cases = [];
for (const c of await readStdin()) cases.push(await runCase(c));
process.stdout.write(JSON.stringify({ failures, cases }) + '\n');
process.exit(failures.length ? 1 : 0);

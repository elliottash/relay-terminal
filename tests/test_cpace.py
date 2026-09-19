# SPDX-License-Identifier: AGPL-3.0-or-later
"""CPACE-X25519-SHA512 (remote/cpace.py and app/cpace.js), card #97EG.

The vectors are draft-irtf-cfrg-cpace-21 appendix B.1, copied verbatim; vectors produced by our own
code would prove nothing. The cross-implementation half runs tests/cpace_vectors.mjs under Node,
which checks app/cpace.js against the same vectors and then against remote/cpace.py on inputs of
our own. It is skipped when Node is missing so the suite still runs everywhere.
"""
import json
import os
import shutil
import subprocess
import unittest
from pathlib import Path

from remote import cpace

HERE = Path(__file__).resolve().parent
NODE = shutil.which("node")
h = bytes.fromhex

# Draft-21 appendix B.1.1 - B.1.5.
PRS = b"Password"
CI = h("0b415f696e69746961746f720b425f726573706f6e646572")
SID = h("7e4b4791d6a8ef019b936c79fb7f2c57")
GEN_STRING = h(
    "0843506163653235350850617373776f72646d000000000000000000"
    "00000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000180b415f696e69746961746f"
    "720b425f726573706f6e646572107e4b4791d6a8ef019b936c79fb7f"
    "2c57")
G = h("d04bf6d41f6a289632a2e929fa29bebd51092512a7829fdde7d314b62f05a73f")
ADA = b"ADa"
YA_SCALAR = h("21b4f4bd9e64ed355c3eb676a28ebedaf6d8f17bdc365995b319097153044080")
YA = h("1d13c89278cdadd826f6d8d7f887701430f8380ddc17611cdd6dc989ce0c9f32")
ADB = b"ADb"
YB_SCALAR = h("848b0779ff415f0af4ea14df9dd1d3c29ac41d836c7808896c4eba19c51ac40a")
YB = h("248cccf6d5cdc3646f0ad593f9e6cef4e69d4945f8372e623512ecea32185623")
K = h("5b067effbdc0b2a0e1d907b21ebb25cfedb96a852179a847c37e43ee71322c6b")
TRANSCRIPT_IR = h(
    "201d13c89278cdadd826f6d8d7f887701430f8380ddc17611cdd6dc9"
    "89ce0c9f320341446120248cccf6d5cdc3646f0ad593f9e6cef4e69d"
    "4945f8372e623512ecea3218562303414462")
ISK_IR = h(
    "6e19b875f7a561d6b3ca3dbb9ef42ac55de3e717881018204b8922b4"
    "d5e53bb2aa82c300bea7b65d2b671da71922ddf6472301b79bc270ad"
    "fa8bf413285f2263")

# Draft-21 appendix B.1.10: scalar_mult_vfy(s, uN) = qN; an all-zero qN must abort.
LOW_ORDER_S = h("af46e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449aff")
ZERO = bytes(32)
LOW_ORDER = [
    (h("0000000000000000000000000000000000000000000000000000000000000000"), ZERO),
    (h("0100000000000000000000000000000000000000000000000000000000000000"), ZERO),
    (h("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"), ZERO),
    (h("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800"), ZERO),
    (h("5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157"), ZERO),
    (h("edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"), ZERO),
    (h("daffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
     h("d8e2c776bbacd510d09fd9278b7edcd25fc5ae9adfba3b6e040e8d3b71b21806")),
    (h("eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"), ZERO),
    (h("dbffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
     h("c85c655ebe8be44ba9c0ffde69f2fe10194458d137f09bbff725ce58803cdb38")),
    (h("d9ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
     h("db64dafa9b8fdd136914e61461935fe92aa372cb056314e1231bc4ec12417456")),
    (h("cdeb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b880"),
     h("e062dcd5376d58297be2618c7498f55baa07d7e03184e8aada20bca28888bf7a")),
    (h("4c9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f11d7"),
     h("993c6ad11c4c29da9a56f7691fd0ff8d732e49de6250b6c2e80003ff4629a175")),
]

# The card's own inputs (wire spec): PIN as PRS, fixed CI, the code room as sid.
MEET_CI = b"relay/meet/v1"
MEET_SID = b"room-7f3a9c"


def draft_pair():
    a = cpace.CPace(PRS, CI, SID, initiator=True, ad=ADA, scalar=YA_SCALAR)
    b = cpace.CPace(PRS, CI, SID, initiator=False, ad=ADB, scalar=YB_SCALAR)
    return a, b


class DraftVectorTests(unittest.TestCase):
    """draft-irtf-cfrg-cpace-21 appendix A and B.1, against remote/cpace.py."""

    def test_string_utilities(self):
        self.assertEqual(cpace.prepend_len(b""), h("00"))
        self.assertEqual(cpace.prepend_len(b"1234"), h("0431323334"))
        self.assertEqual(cpace.prepend_len(bytes(range(127))), h("7f") + bytes(range(127)))
        self.assertEqual(cpace.prepend_len(bytes(range(128))), h("8001") + bytes(range(128)))
        self.assertEqual(cpace.lv_cat(b"1234", b"5", b"", b"678"), h("043132333401350003363738"))
        self.assertEqual(cpace.transcript_ir(b"123", b"PartyA", b"234", b"PartyB"),
                         h("03313233065061727479410332333406506172747942"))
        self.assertEqual(cpace.transcript_ir(b"3456", b"PartyA", b"2345", b"PartyB"),
                         h("043334353606506172747941043233343506506172747942"))

    def test_generator(self):
        self.assertEqual(cpace.generator_string(PRS, CI, SID), GEN_STRING)
        self.assertEqual(cpace.generator(PRS, CI, SID), G)

    def test_messages(self):
        a, b = draft_pair()
        self.assertEqual(a.message, YA)
        self.assertEqual(b.message, YB)

    def test_secret_point(self):
        self.assertEqual(cpace.scalar_mult_vfy(YA_SCALAR, YB), K)
        self.assertEqual(cpace.scalar_mult_vfy(YB_SCALAR, YA), K)
        self.assertEqual(cpace.transcript_ir(YA, ADA, YB, ADB), TRANSCRIPT_IR)

    def test_isk_initiator_responder(self):
        a, b = draft_pair()
        self.assertEqual(a.finish(YB, ADB), ISK_IR)
        self.assertEqual(b.finish(YA, ADA), ISK_IR)

    def test_low_order_points(self):
        a, _ = draft_pair()
        for index, (u, q) in enumerate(LOW_ORDER):
            with self.subTest(u=index):
                if q == ZERO:
                    with self.assertRaises(cpace.CPaceError):
                        cpace.scalar_mult_vfy(LOW_ORDER_S, u)
                    with self.assertRaises(cpace.CPaceError):
                        a.finish(u, ADB)
                else:
                    self.assertEqual(cpace.scalar_mult_vfy(LOW_ORDER_S, u), q)


class ProtocolTests(unittest.TestCase):
    def meet(self, guest_pin: bytes, desktop_pin: bytes, sid: bytes = MEET_SID):
        guest = cpace.CPace(guest_pin, MEET_CI, sid, initiator=True, ad=b"guest")
        desktop = cpace.CPace(desktop_pin, MEET_CI, sid, initiator=False, ad=b"desktop")
        return guest.finish(desktop.message, b"desktop"), desktop.finish(guest.message, b"guest")

    def test_round_trip_agrees(self):
        guest_isk, desktop_isk = self.meet(b"4829", b"4829")
        self.assertEqual(len(guest_isk), 64)
        self.assertEqual(guest_isk, desktop_isk)

    def test_wrong_pin_disagrees(self):
        guest_isk, desktop_isk = self.meet(b"4828", b"4829")
        self.assertNotEqual(guest_isk, desktop_isk)

    def test_fresh_scalars_every_attempt(self):
        """Two attempts with the same PIN never share a message or a key."""
        first, second = self.meet(b"4829", b"4829")[0], self.meet(b"4829", b"4829")[0]
        self.assertNotEqual(first, second)
        one = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"guest")
        two = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"guest")
        self.assertNotEqual(one.message, two.message)

    def test_sid_and_ci_bind_the_generator(self):
        base = cpace.generator(b"4829", MEET_CI, MEET_SID)
        self.assertNotEqual(base, cpace.generator(b"4829", MEET_CI, b"room-other"))
        self.assertNotEqual(base, cpace.generator(b"4829", b"relay/meet/v2", MEET_SID))
        self.assertNotEqual(base, cpace.generator(b"4830", MEET_CI, MEET_SID))

    def test_roles_matter(self):
        """Both sides claiming to be the initiator puts the transcript in two orders."""
        a = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"guest")
        b = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"desktop")
        self.assertNotEqual(a.finish(b.message, b"desktop"), b.finish(a.message, b"guest"))

    def test_tampered_ad_disagrees(self):
        guest = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"guest")
        desktop = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=False, ad=b"desktop")
        self.assertNotEqual(guest.finish(desktop.message, b"desktop"),
                            desktop.finish(guest.message, b"guesT"))

    def test_bad_peer_points_refused(self):
        guest = cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"guest")
        for bad in (b"", bytes(31), bytes(33), guest.message + b"\x00", bytes(32), (1).to_bytes(32, "little")):
            with self.subTest(length=len(bad), point=bad.hex()):
                with self.assertRaises(cpace.CPaceError):
                    guest.finish(bad, b"desktop")

    def test_bad_scalar_refused(self):
        with self.assertRaises(cpace.CPaceError):
            cpace.CPace(b"4829", MEET_CI, MEET_SID, initiator=True, ad=b"guest", scalar=bytes(31))


@unittest.skipUnless(NODE, "node is not installed")
class CrossLanguageTests(unittest.TestCase):
    """app/cpace.js under Node: the draft's vectors, then byte-for-byte agreement with Python."""

    def cases(self):
        # Fixed scalars so any disagreement is reproducible; plus the draft's own inputs, the
        # card's real ones, an empty PRS, and a long PRS that pushes the generator string past
        # one SHA-512 block and needs a two-byte length prefix.
        fixed = [
            (PRS, CI, SID, YA_SCALAR, YB_SCALAR, ADA, ADB),
            (b"4829", MEET_CI, MEET_SID, h("11" * 32), h("22" * 32), b"guest", b"desktop"),
            (b"0000", MEET_CI, "räume-ü".encode(), bytes(range(32)), bytes(range(32, 64)), b"guest", b"desktop"),
            (b"", b"", b"", h("ff" * 32), h("01" * 32), b"", b""),
            (bytes(range(200)), MEET_CI, MEET_SID, h("5a" * 32), h("a5" * 32), b"x" * 130, b"y"),
        ]
        fixed += [(str(1000 + i * 1111).encode(), MEET_CI, b"room-%d" % i, os.urandom(32), os.urandom(32),
                   b"guest", b"desktop") for i in range(8)]
        return fixed

    def test_js_matches_draft_and_python(self):
        cases = self.cases()
        request = [dict(zip(("prs", "ci", "sid", "ya", "yb", "ada", "adb"), (v.hex() for v in c))) for c in cases]
        run = subprocess.run([NODE, str(HERE / "cpace_vectors.mjs")], input=json.dumps(request),
                             capture_output=True, text=True, timeout=60, cwd=str(HERE.parent))
        self.assertTrue(run.stdout.strip(), run.stderr)
        result = json.loads(run.stdout)
        self.assertEqual(result["failures"], [], run.stderr)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(len(result["cases"]), len(cases))

        for (prs, ci, sid, ya, yb, ada, adb), js in zip(cases, result["cases"]):
            with self.subTest(prs=prs[:16], sid=sid):
                a = cpace.CPace(prs, ci, sid, initiator=True, ad=ada, scalar=ya)
                b = cpace.CPace(prs, ci, sid, initiator=False, ad=adb, scalar=yb)
                isk = a.finish(b.message, adb)
                self.assertEqual(js["g"], cpace.generator(prs, ci, sid).hex())
                self.assertEqual(js["Ya"], a.message.hex())
                self.assertEqual(js["Yb"], b.message.hex())
                self.assertEqual(js["isk_a"], isk.hex())
                self.assertEqual(js["isk_b"], isk.hex())
                self.assertEqual(b.finish(a.message, ada), isk)


if __name__ == "__main__":
    unittest.main()

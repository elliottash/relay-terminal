# SPDX-License-Identifier: AGPL-3.0-or-later
"""One model, one name, on the phone too (card #MDL1, rule 1).

`relay::models::nameOf` (C++), `presets.derived_name` (the worker) and `app/modelname.js`'s
`nameOf` are three copies of the last step of the naming rule, because the three sides each need
it where the others are not. Nothing but this test holds them to the same answer: it runs the
JavaScript under Node and compares it with the worker's, id by id, and reads the C++ out of
src/ModelCatalog.cpp for the shape of the rule.

The worker sends the name it computed on every event that names a model, so the derivation is only
the fallback — for an older worker, and for an id typed by hand. The phone must prefer the sent
one: nothing on the phone can know that the Kimi Coding Plan's `k3` is Kimi K3.
"""
import json
import shutil
import subprocess
import unittest
from pathlib import Path

from relay_core import presets

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class WebModelNameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        done = subprocess.run([shutil.which("node"), str(HERE / "model_name_peer.mjs")],
                              capture_output=True, text=True, cwd=str(ROOT))
        assert done.returncode == 0, done.stderr
        cls.answers = json.loads(done.stdout)

    def test_the_phone_derives_the_same_name_as_the_worker(self):
        for model_id, name in self.answers["derived"].items():
            with self.subTest(model_id):
                self.assertEqual(name, presets.derived_name(model_id))

    def test_a_name_is_lower_case_with_no_spaces_and_no_vendor_prefix(self):
        derived = self.answers["derived"]
        self.assertEqual(derived["MiniMax-M3"], "minimax-m3")
        self.assertEqual(derived["openai/gpt-6-sol"], "gpt-6-sol")
        self.assertEqual(derived["~openai/gpt-sol-latest"], "gpt-sol-latest")
        self.assertEqual(derived["Some Model"], "some-model")
        for name in derived.values():
            self.assertEqual(name, name.lower())
            self.assertNotIn(" ", name)
            self.assertNotIn("/", name)

    def test_nothing_named_is_never_an_exception(self):
        self.assertEqual(self.answers["derived"][""], "")
        self.assertEqual(self.answers["not_a_string"], "")
        self.assertEqual(self.answers["empty_object"], "")
        self.assertEqual(self.answers["not_an_object"], "")

    def test_the_workers_own_name_wins_wherever_it_sent_one(self):
        # The catalog is the worker's: only its row says that "k3" is Kimi K3.
        self.assertEqual(self.answers["prefers_the_worker"], "kimi-k3")
        self.assertEqual(self.answers["prefers_the_worker"], presets.model_name("kimi-code", "k3"))
        self.assertEqual(self.answers["other_field_named"], "kimi-k3")
        # And with none sent, the derivation — which is right for every id no row overrides.
        self.assertEqual(self.answers["falls_back"], "gpt-6-sol")
        self.assertEqual(self.answers["other_field"], "minimax-m3")

    def test_the_phone_prints_names_where_it_used_to_print_ids(self):
        app = (ROOT / "app" / "app.js").read_text(encoding="utf-8")
        self.assertIn("import { modelName } from './modelname.js';", app)
        self.assertIn("return `model: ${model} · conversation kept`;", app)
        self.assertNotIn("Model: ${model}", app)
        self.assertNotIn("const model = event.model || '';", app)
        board = (ROOT / "app" / "board.js").read_text(encoding="utf-8")
        self.assertIn("nameOf(str(attrs.model))", board)

    def test_the_cpp_derivation_is_the_same_rule(self):
        # Not run here — it is C++ — but it must still be the same three steps, and
        # tests/modelcatalog_test.cpp checks the answers.
        cpp = (ROOT / "src" / "ModelCatalog.cpp").read_text(encoding="utf-8")
        body = cpp.split("QString nameOf(", 1)[1].split("\n}", 1)[0]
        for step in ("'/'", "'~'", "toLower()"):
            self.assertIn(step, body)


if __name__ == "__main__":
    unittest.main()

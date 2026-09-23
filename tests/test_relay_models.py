# SPDX-License-Identifier: AGPL-3.0-or-later
"""scripts/relay-models.py: show, check, discover, set and rename (card #MCP7).

Everything that edits runs on a copy of `backend/relay_core` under a temporary directory, through
the CLI's `--root`, in a subprocess — the way a person runs it. Offline: `discover` runs with
RELAY_MODELS_OFFLINE=1 and the keyring off.

    cd tests && RELAY_KEYRING=off PYTHONPATH=$PWD/../backend:$PWD/.. python3 -m unittest test_relay_models
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))
from relay_core import model_ranking as MR        # noqa: E402 — the parser; it reads text only

SCRIPT = REPO / "scripts" / "relay-models.py"
RANKING = "backend/relay_core/model-ranking.md"
PRESETS = "backend/relay_core/presets.py"

# What `check` reports on the shipped tree today, by the start of the line. Each is a real
# disagreement the tool found on 2026-09-22 and left for the owner to rule on:
#   openrouter main — model-ranking.md's Provider picks put glm-5.3-flash in openrouter's main,
#                     TIER_DEFAULTS still names deepseek/deepseek-v4.1-flash;
#   gemini main/flash — the Models table classes gemini-flash-latest for main and flash (and
#                     gemini-pro-latest for high only), TIER_DEFAULTS names the concrete
#                     gemini-3.1-pro-preview and gemini-3.8-flash.
# When one is fixed this test fails and says so: delete its line here.
KNOWN_DRIFT = ()


def run(*args, root=None, env=None, check_exit=None):
    environ = dict(os.environ, RELAY_KEYRING="off", RELAY_MODELS_OFFLINE="1")
    environ.update(env or {})
    argv = [sys.executable, str(SCRIPT)] + (["--root", str(root)] if root else []) + list(args)
    out = subprocess.run(argv, capture_output=True, text=True, env=environ, timeout=60)
    if check_exit is not None and out.returncode != check_exit:
        raise AssertionError(f"{args} exited {out.returncode}\nstdout:\n{out.stdout}\nstderr:\n{out.stderr}")
    return out


class Copy(unittest.TestCase):
    """A throwaway checkout: backend/relay_core copied from this one."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="relay-models-")
        self.root = Path(self.tmp.name)
        shutil.copytree(REPO / "backend" / "relay_core", self.root / "backend" / "relay_core",
                        ignore=shutil.ignore_patterns("__pycache__"))

    def tearDown(self):
        self.tmp.cleanup()

    def read(self, rel):
        return (self.root / rel).read_text(encoding="utf-8")

    def write(self, rel, text):
        (self.root / rel).write_text(text, encoding="utf-8")

    def replace(self, rel, old, new):
        text = self.read(rel)
        self.assertEqual(text.count(old), 1, f"{old!r} is not in {rel} exactly once")
        self.write(rel, text.replace(old, new))

    def tier_line(self, provider, tier):
        """TIER_DEFAULTS[provider][tier] as presets.py spells it today — the extras are whatever
        another session last set them to, so the tests read them rather than repeat them."""
        found = re.findall(rf'"{tier}": \("{re.escape(provider)}", "[^"]+", \{{[^}}]*\}}\)', self.read(PRESETS))
        self.assertEqual(len(found), 1, (provider, tier, found))
        return found[0]

    def assert_only_known_drift(self, out, fixed=()):
        lines = [line for line in out.stdout.splitlines() if line.strip()]
        known = tuple(p for p in KNOWN_DRIFT if p not in fixed)
        self.assertEqual([line for line in lines if not line.startswith(known) and line != "ok"], [],
                         "check found drift it did not find before")
        for prefix in known:
            self.assertTrue(any(line.startswith(prefix) for line in lines),
                            f"{prefix!r} is no longer reported: it was fixed, delete it from KNOWN_DRIFT")
        self.assertEqual(out.returncode, 1 if known else 0, out.stdout + out.stderr)

    def catalog_row(self, preset, model_id):
        """The MODEL_CATALOG row, read out of the copy by the copy's own relay_core."""
        code = ("import json, sys; sys.path.insert(0, sys.argv[1]); from relay_core import presets as P; "
                "print(json.dumps([r for r in P.MODEL_CATALOG[sys.argv[2]] if r['id'] == sys.argv[3]]))")
        out = subprocess.run([sys.executable, "-c", code, str(self.root / "backend"), preset, model_id],
                             capture_output=True, text=True, check=True,
                             env=dict(os.environ, RELAY_KEYRING="off", PYTHONPATH=""))
        rows = json.loads(out.stdout)
        return rows[0] if rows else None

    def tier_default(self, preset, tier):
        code = ("import json, sys; sys.path.insert(0, sys.argv[1]); from relay_core import presets as P; "
                "print(json.dumps(P.TIER_DEFAULTS[sys.argv[2]][sys.argv[3]]))")
        out = subprocess.run([sys.executable, "-c", code, str(self.root / "backend"), preset, tier],
                             capture_output=True, text=True, check=True,
                             env=dict(os.environ, RELAY_KEYRING="off", PYTHONPATH=""))
        return json.loads(out.stdout)


class CheckTests(Copy):
    def test_the_shipped_tree_has_only_the_known_drift(self):
        self.assert_only_known_drift(run("check"))

    def test_check_reads_the_root_it_is_given_not_pythonpath(self):
        # A wrong tier tag and a second main-classed openai model, in the copy only; PYTHONPATH
        # names this checkout's backend, and --root has to win.
        self.replace(PRESETS, '{"id": "gpt-6-luna", "tier": "flash"', '{"id": "gpt-6-luna", "tier": None')
        self.replace(RANKING, "| gpt-6-astra | high |", "| gpt-6-astra | high, main |")
        out = run("check", root=self.root, env={"PYTHONPATH": str(REPO / "backend")})
        self.assertEqual(out.returncode, 1)
        self.assertIn("MODEL_CATALOG['openai'] gpt-6-luna: tier is None, TIER_DEFAULTS implies 'flash'",
                      out.stdout)
        self.assertIn("openai main: TIER_DEFAULTS names gpt-6-sol", out.stdout)
        self.assertNotIn("gpt-6-luna", run("check").stdout)

    def test_a_tier_naming_a_model_with_no_catalog_row_is_reported(self):
        line = self.tier_line("openai", "flash")
        self.replace(PRESETS, line, re.sub(r'"openai", "[^"]+"', '"openai", "gpt-6-nova"', line))
        out = run("check", root=self.root)
        self.assertIn("TIER_DEFAULTS['openai']['flash'] names gpt-6-nova, which has no "
                      "MODEL_CATALOG['openai'] row", out.stdout)

    def test_show_prints_every_provider(self):
        out = run("show", root=self.root, check_exit=0)
        for provider in ("openai", "anthropic", "openrouter", "guest:codex", "guest:claude", "relay-free"):
            self.assertIn(provider, out.stdout)
        self.assertRegex(out.stdout, r"openai\s+ranked\s+high gpt-6-astra")


class SetTests(Copy):
    def ranking(self):
        return MR.parse(self.read(RANKING), "copy")

    def assert_sorted(self):
        order = [(-(row.score if row.score is not None else -1), row.name)
                 for row in self.ranking().models.values()]
        self.assertEqual(order, sorted(order), "set left model-ranking.md unsorted")

    def test_openai_main_moves_everywhere_at_once(self):
        before = self.tier_default("openai", "main")
        self.assertEqual(before[:2], ["openai", "gpt-6-sol"])
        run("set", "openai", "main=gpt-6-astra", root=self.root, check_exit=0)
        rank = self.ranking()
        self.assertEqual(rank.classes("gpt-6-astra"), ("high", "main"))
        self.assertEqual(rank.classes("gpt-6-sol"), ())           # "-": openai alone served it
        # The model changes and the request extras stay exactly as they were.
        self.assertEqual(self.tier_default("openai", "main"), ["openai", "gpt-6-astra", before[2]])
        self.assertEqual(self.catalog_row("openai", "gpt-6-astra")["tier"], "main")
        self.assertIsNone(self.catalog_row("openai", "gpt-6-sol")["tier"])
        self.assertEqual(self.catalog_row("openai", "gpt-6-luna")["tier"], "flash")
        self.assert_sorted()
        self.assert_only_known_drift(run("check", root=self.root))

    def test_a_brand_new_model_gets_a_catalog_row_and_a_ranking_row(self):
        out = run("set", "anthropic", "main=claude-opus-6-1", root=self.root, check_exit=0)
        self.assertIn("added MODEL_CATALOG['anthropic'] row claude-opus-6-1 (name claude-opus-6.1)", out.stdout)
        self.assertEqual(self.catalog_row("anthropic", "claude-opus-6-1"),
                         {"id": "claude-opus-6-1", "name": "claude-opus-6.1", "tier": "main", "efforts": None})
        rank = self.ranking()
        self.assertEqual(rank.classes("claude-opus-6.1"), ("main",))
        self.assertIsNone(rank.score("claude-opus-6.1"))
        self.assertIn("added by relay-models set", rank.models["claude-opus-6.1"].notes)
        old = next(name for name in rank.models if name.startswith("claude-opus-5"))
        self.assertEqual(rank.classes(old), ())
        self.assertEqual(self.tier_default("anthropic", "main")[:2], ["anthropic", "claude-opus-6-1"])
        self.assert_sorted()
        self.assert_only_known_drift(run("check", root=self.root))

    def test_several_classes_in_one_call(self):
        run("set", "openai", "main=gpt-6-astra", "flash=gpt-5.6-terra", root=self.root, check_exit=0)
        self.assertEqual(self.catalog_row("openai", "gpt-5.6-terra")["tier"], "flash")
        # gpt-6-luna is still openai's lite, so its tag moves from flash to lite rather than None.
        self.assertEqual(self.catalog_row("openai", "gpt-6-luna")["tier"], "lite")
        self.assertEqual(self.ranking().classes("gpt-6-luna"), ())
        self.assert_only_known_drift(run("check", root=self.root))

    def test_openrouter_is_set_through_its_provider_picks_row(self):
        run("set", "openrouter", "main=deepseek/deepseek-v4.1-flash", root=self.root, check_exit=0)
        self.assertEqual(self.ranking().pick("openrouter", "main"), "deepseek-v4.1-flash")
        self.assert_only_known_drift(run("check", root=self.root), fixed=("openrouter main:",))

    def test_a_guest_changes_the_ranking_only(self):
        before = self.read(PRESETS)
        out = run("set", "guest:codex", "high=gpt-6-sol", root=self.root, check_exit=0)
        self.assertEqual(self.read(PRESETS), before)
        self.assertEqual(self.ranking().classes("gpt-6-sol"), ("high", "main"))
        # gpt-6-astra is openai's too, so the shared row keeps its high and set says so.
        self.assertEqual(self.ranking().classes("gpt-6-astra"), ("high",))
        self.assertIn("warning: gpt-6-astra keeps high", out.stdout)

    def test_dry_run_prints_a_diff_and_writes_nothing(self):
        before = (self.read(RANKING), self.read(PRESETS))
        out = run("set", "openai", "main=gpt-6-astra", "--dry-run", root=self.root, check_exit=0)
        self.assertIn("+++ b/backend/relay_core/presets.py", out.stdout)
        self.assertIn('-    "openai": {"main": ("openai", "gpt-6-sol"', out.stdout)
        self.assertIn("land.py begin", out.stdout)
        self.assertEqual((self.read(RANKING), self.read(PRESETS)), before)

    def test_a_pattern_that_does_not_match_exactly_once_changes_nothing(self):
        # A repeated key: valid Python, and exactly the ambiguity `set` must not guess through.
        line = self.tier_line("openai", "main")
        self.replace(PRESETS, line, line + ",\n               " + line)
        before = (self.read(RANKING), self.read(PRESETS))
        out = run("set", "openai", "main=gpt-6-astra", root=self.root)
        self.assertEqual(out.returncode, 3, out.stdout + out.stderr)
        self.assertIn("refused, nothing written", out.stderr)
        self.assertIn("one 'main' key, found 2", out.stderr)
        self.assertEqual((self.read(RANKING), self.read(PRESETS)), before)

    def test_a_ranking_the_edit_cannot_read_changes_nothing(self):
        text = self.read(RANKING)
        row = next(line for line in text.splitlines() if line.startswith("| gpt-6-astra |"))
        self.write(RANKING, text.replace(row, row + "\n" + row))
        before = (self.read(RANKING), self.read(PRESETS))
        out = run("set", "openai", "main=gpt-6-astra", root=self.root)
        self.assertEqual(out.returncode, 3, out.stdout + out.stderr)
        self.assertIn("more than one row for 'gpt-6-astra'", out.stderr)
        self.assertEqual((self.read(RANKING), self.read(PRESETS)), before)

    def test_bad_arguments_are_refused(self):
        self.assertEqual(run("set", "nobody", "main=x", root=self.root).returncode, 2)
        self.assertEqual(run("set", "openai", "fast=x", root=self.root).returncode, 2)
        self.assertEqual(run("set", "guest:claude", "lite=haiku", root=self.root).returncode, 2)


class RenameTests(unittest.TestCase):
    # Made-up families, so a real repo-wide rename can never rewrite this test's own fixtures.
    TEXT = ("claude-quill-3 claude-quill-3-5 claude-quill-3.1 xclaude-quill-3 anthropic/claude-quill-3\n"
            "\"claude-quill-3\" claude-quill-3-20260514 Claude Quill 3 and Claude Quill 3.1; claude quill 3.\n"
            "gpt-9.1-nova gpt-9.1-nova-pro gpt-9.1-nova-2 chatgpt-9.1-nova openai/gpt-9.1-nova:batch.\n")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="relay-models-rename-")
        self.root = Path(self.tmp.name)
        for rel in ("src/a.txt", "issues/card.md", "docs/qa_evidence/run.txt",
                    "tests/fixtures/issues_legacy/x.md", "build/out.txt", "build-arm/out.txt"):
            (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.root / rel).write_text(self.TEXT, encoding="utf-8")
        (self.root / "src/blob.bin").write_bytes(b"claude-quill-3\0binary")

    def tearDown(self):
        self.tmp.cleanup()

    def text(self, rel):
        return (self.root / rel).read_text(encoding="utf-8")

    def test_whole_ids_only_suffixes_kept_names_derived(self):
        out = run("rename", "claude-quill-3", "claude-quill-3-5", root=self.root, check_exit=0)
        self.assertEqual(self.text("src/a.txt").splitlines()[:2], [
            "claude-quill-3-5 claude-quill-3-5 claude-quill-3.1 xclaude-quill-3 anthropic/claude-quill-3-5",
            "\"claude-quill-3-5\" claude-quill-3-5-20260514 Claude Quill 3.5 and Claude Quill 3.1; "
            "claude quill 3.5.",
        ])
        self.assertIn("6  src/a.txt", out.stdout)
        self.assertIn("land.py begin <me> --from-head src/a.txt", out.stdout)
        # Serving variants keep their suffix; a numbered continuation is another model.
        run("rename", "gpt-9.1-nova", "gpt-10-nova", root=self.root, check_exit=0)
        self.assertEqual(self.text("src/a.txt").splitlines()[2],
                         "gpt-10-nova gpt-10-nova-pro gpt-9.1-nova-2 chatgpt-9.1-nova openai/gpt-10-nova:batch.")

    def test_exclusions_binaries_and_dry_run(self):
        out = run("rename", "claude-quill-3", "claude-quill-4", "--dry-run", root=self.root, check_exit=0)
        self.assertIn("dry run: nothing written", out.stdout)
        self.assertEqual(self.text("src/a.txt"), self.TEXT)
        run("rename", "claude-quill-3", "claude-quill-4", root=self.root, check_exit=0)
        self.assertIn("claude-quill-4 ", self.text("src/a.txt"))
        for rel in ("issues/card.md", "docs/qa_evidence/run.txt", "tests/fixtures/issues_legacy/x.md",
                    "build/out.txt", "build-arm/out.txt"):
            self.assertEqual(self.text(rel), self.TEXT, rel)
        self.assertEqual((self.root / "src/blob.bin").read_bytes(), b"claude-quill-3\0binary")

    def test_an_explicit_name_and_the_dotted_spelling(self):
        (self.root / "src/n.txt").write_text("claude-sable-4-5 claude-sable-4.5 Claude Sable 4.5 sable-name\n")
        run("rename", "claude-sable-4-5", "claude-sable-4-6", root=self.root, check_exit=0)
        self.assertEqual(self.text("src/n.txt"), "claude-sable-4-6 claude-sable-4.6 Claude Sable 4.6 sable-name\n")
        run("rename", "claude-sable-4-6", "claude-sable-5", "--name", "sable-name=sable-5-name",
            root=self.root, check_exit=0)
        self.assertEqual(self.text("src/n.txt"), "claude-sable-5 claude-sable-5 Claude Sable 5 sable-5-name\n")

    def test_in_a_git_checkout_only_tracked_files_change(self):
        if shutil.which("git") is None:
            self.skipTest("no git")
        git = ["git", "-C", str(self.root), "-c", "user.email=t@t", "-c", "user.name=t"]
        subprocess.run(git + ["init", "-q"], check=True)
        subprocess.run(git + ["add", "src/a.txt", "issues/card.md"], check=True)
        (self.root / "src/untracked.txt").write_text("claude-quill-3\n")
        run("rename", "claude-quill-3", "claude-quill-4", root=self.root, check_exit=0)
        self.assertIn("claude-quill-4", self.text("src/a.txt"))
        self.assertEqual(self.text("src/untracked.txt"), "claude-quill-3\n")
        self.assertEqual(self.text("issues/card.md"), self.TEXT)


class DiscoverTests(unittest.TestCase):
    def test_offline_it_skips_every_remote_source_and_still_names_the_claude_aliases(self):
        out = run("discover", check_exit=0)
        self.assertIn("openrouter: skipped", out.stdout)
        self.assertIn("guest:codex: skipped", out.stdout)
        self.assertRegex(out.stdout, r"opus\s+-> claude-opus-")
        data = json.loads(run("discover", "--json", check_exit=0).stdout)
        status = {source["source"]: source["status"] for source in data["sources"]}
        self.assertEqual(status.pop("guest:claude"), "ok")
        self.assertEqual(set(status.values()), {"skipped"})
        self.assertIn("anthropic", status)

    def test_a_first_party_listing_is_summarised_as_new_and_gone(self):
        # The summary step alone, fed a canned listing: no key, no network.
        import importlib.util
        spec = importlib.util.spec_from_file_location("relay_models", SCRIPT)
        tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tool)
        tool.load_core(REPO)
        served = [{"id": "gpt-6-astra", "created": 5}, {"id": "gpt-6-nova", "created": 9},
                  {"id": "gpt-6-sol"}, {"id": "text-embedding-9"}, {"id": "gpt-5-mini", "created": 1}]
        out = tool.summarise("openai", served, {"gpt-6-astra", "gpt-6-sol", "gpt-6-luna"}, set())
        self.assertEqual([(row["id"], row["older"]) for row in out["new"]],
                         [("gpt-6-nova", False), ("gpt-5-mini", True)])
        self.assertEqual(out["gone"], ["gpt-6-luna"])
        self.assertEqual(out["not_chat"], 1)


if __name__ == "__main__":
    unittest.main()

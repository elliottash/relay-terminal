# SPDX-License-Identifier: AGPL-3.0-or-later
"""Claude Code / Codex memories offered to Relay as suggestions at startup (#MEMS step 5)."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import memory_import, memory_suggestions  # noqa: E402

CLAUDE_MD = """\
# Global instructions

- Reply in concise Markdown.
- Never push to a remote without asking first.
  Pull requests are fine once the tests pass.
- Short

Use they/them for anyone whose pronouns have not been stated.

## Tools

```bash
rm -rf build   # not a fact
```

| a | b |
| - | - |

@~/.claude/extra.md

Rules:

export OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz0123
"""

FEEDBACK = """\
---
name: small-commits
description: Commit small and often, straight to main.
metadata:
  type: feedback
---

Commit small and often, straight to main; never open a branch for your own work.

**Why:** side branches hide work from the other sessions. They also go stale.

**How to apply:** land with the shared script. See [[land-script]].
"""

USER = """\
---
name: role
description: The user is an economist who writes Python.
type: user
---

The user is an economist and writes most analysis code in Python.
"""

PROJECT = """\
---
name: build-dir
description: The build directory is shared.
metadata:
  type: project
---

Build through scripts/relay-build; the build directory is shared between sessions.
"""

REFERENCE = """\
---
name: dashboard
description: Grafana board for the gateway.
type: reference
---

The gateway dashboard lives at grafana.example.org/d/gateway.
"""

CODEX_SUMMARY = """\
v1
## User Profile
Economist and research engineer who runs several coding agents at once.

## User preferences
- Wants terse answers with file:line references. [Task 1][Task 2]
- Prefers fixing clear gaps over listing them as follow-ups. [Task 3]
- token: ghp_abcdefghijklmnopqrstuvwxyz0123456789

## General Tips
- Run the narrow test first.

## What's in Memory
### /home/u/repo
#### 2026-09-20
- desc: a routing entry that is not a user fact
"""


def write(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class MemoryImportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.home = base / "home"
        self.store = base / "switchboard"
        env = {"RELAY_GLOBAL_SWITCHBOARD": str(self.store), "RELAY_KEYRING": "off"}
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name in ("CLAUDE_CONFIG_DIR", "CODEX_HOME", "RELAY_MEMORY_IMPORT"):
            os.environ.pop(name, None)
        self.addCleanup(self.tmp.cleanup)
        claude = self.home / ".claude"
        write(claude / "CLAUDE.md", CLAUDE_MD)
        memory = claude / "projects" / "-home-u-repo" / "memory"
        write(memory / "small-commits.md", FEEDBACK)
        write(memory / "role.md", USER)
        write(memory / "build-dir.md", PROJECT)
        write(memory / "dashboard.md", REFERENCE)
        write(memory / "MEMORY.md", "- [Role](role.md) — the user is an economist\n")

    def facts(self):
        return {row["fact"]: row for row in memory_suggestions.pending()}

    def test_claude_user_and_feedback_imported_project_and_reference_skipped(self):
        summary = memory_import.import_all(home=self.home)
        facts = self.facts()
        self.assertIn("The user is an economist who writes Python.", facts)   # the description
        feedback = [f for f in facts if f.startswith("Commit small and often")]
        self.assertEqual(len(feedback), 1)
        # The description, with the Why folded in as one sentence and the How-to line dropped.
        self.assertEqual(feedback[0], "Commit small and often, straight to main. "
                                      "Why: side branches hide work from the other sessions.")
        self.assertFalse(any("build directory" in f or "grafana" in f for f in facts))
        self.assertFalse(any("MEMORY.md" in f or "role.md" in f for f in facts))
        row = facts[feedback[0]]
        self.assertEqual(row["name"], "small-commits")
        self.assertEqual(summary["codex"], 0)
        self.assertEqual(summary["claude"], len(facts))

    def test_claude_md_split_into_bullets_and_paragraphs(self):
        memory_import.import_all(home=self.home)
        facts = set(self.facts())
        self.assertIn("Reply in concise Markdown.", facts)
        self.assertIn("Never push to a remote without asking first. "
                      "Pull requests are fine once the tests pass.", facts)
        self.assertIn("Use they/them for anyone whose pronouns have not been stated.", facts)
        for text in facts:
            self.assertNotIn("rm -rf", text)          # code block
            self.assertNotIn("Global instructions", text)   # heading
            self.assertNotIn("| a |", text)            # table
            self.assertNotIn("@~/", text)              # import line
            self.assertNotEqual(text, "Rules:")        # lead-in
            self.assertNotEqual(text, "Short")         # too short to be a fact

    def test_secret_line_dropped(self):
        summary = memory_import.import_all(home=self.home)
        for text in self.facts():
            self.assertNotIn("sk-", text)
            self.assertNotIn("OPENAI_API_KEY", text)
        self.assertGreaterEqual(summary["secrets"], 1)
        self.assertTrue(memory_import.looks_secret("password: hunter2hunter2"))
        self.assertTrue(memory_import.looks_secret("GITHUB_TOKEN=ghp_abcdefghijklmnop0123456789"))
        self.assertFalse(memory_import.looks_secret("Never ask for my password in chat."))

    def test_second_run_imports_nothing(self):
        first = memory_import.import_all(home=self.home)
        self.assertGreater(first["claude"], 0)
        count = len(memory_suggestions.pending())
        second = memory_import.import_all(home=self.home)
        self.assertEqual((second["claude"], second["codex"], second["duplicate"]), (0, 0, 0))
        self.assertEqual(second["unchanged"], 5)     # CLAUDE.md and the four memory files
        self.assertEqual(len(memory_suggestions.pending()), count)
        ledger = json.loads((self.store / "memory" / "imported.json").read_text())
        entry = ledger["files"][str(self.home / ".claude" / "CLAUDE.md")]
        self.assertEqual(entry["source"], "claude")
        self.assertEqual(len(entry["facts"]), 3)

    def test_changed_file_offers_only_new_facts(self):
        memory_import.import_all(home=self.home)
        claude_md = self.home / ".claude" / "CLAUDE.md"
        # One fact accepted in the meantime must not come back as a duplicate either: it was
        # offered once, so the ledger skips it before suggest() is asked.
        kept = next(r for r in memory_suggestions.pending() if r["fact"] == "Reply in concise Markdown.")
        memory_suggestions.accept(kept["id"])
        before = set(self.facts())
        write(claude_md, CLAUDE_MD + "\n- Always run the targeted tests before committing.\n")
        summary = memory_import.import_all(home=self.home)
        after = set(self.facts())
        self.assertEqual(after - before, {"Always run the targeted tests before committing."})
        self.assertEqual(summary["claude"], 1)
        self.assertEqual(summary["skipped"], 3)
        self.assertEqual(summary["duplicate"], 0)

    def test_rejected_fact_not_reoffered(self):
        memory_import.import_all(home=self.home)
        target = next(r for r in memory_suggestions.pending() if r["fact"] == "Reply in concise Markdown.")
        memory_suggestions.reject(target["id"])
        # A fresh ledger (a new machine, a deleted file) still cannot bring it back.
        (self.store / "memory" / "imported.json").unlink()
        summary = memory_import.import_all(home=self.home)
        self.assertNotIn("Reply in concise Markdown.", self.facts())
        self.assertGreaterEqual(summary["declined"], 1)
        self.assertEqual(summary["claude"], 0)

    def test_codex_summary_and_agents_md(self):
        codex = self.home / ".codex"
        write(codex / "memories" / "memory_summary.md", CODEX_SUMMARY)
        write(codex / "memories" / "MEMORY.md", "# Task Group: repo\n## User preferences\n- block-local\n")
        write(codex / "memories" / "raw_memories.md", "- per-rollout raw note\n")
        write(codex / "AGENTS.md", "# Codex\n\n- Prefer rg over grep for code search.\n")
        summary = memory_import.import_all(home=self.home)
        codex_rows = {r["fact"]: r for r in memory_suggestions.pending() if r.get("source") == "codex"}
        self.assertEqual(set(codex_rows), {
            "Economist and research engineer who runs several coding agents at once.",
            "Wants terse answers with file:line references.",
            "Prefers fixing clear gaps over listing them as follow-ups.",
            "Prefer rg over grep for code search.",
        })
        self.assertEqual(summary["codex"], 4)
        self.assertGreaterEqual(summary["secrets"], 1)

    def test_claude_memory_without_description_uses_first_paragraph(self):
        facts = memory_import.claude_memory("---\nname: tabs\ntype: user\n---\n\nThe user indents\n"
                                            "with tabs.\n\n**How to apply:** keep them.\n")
        self.assertEqual(facts, [{"fact": "The user indents with tabs.", "name": "tabs", "title": None}])

    def test_nested_bullets_stay_with_their_parent(self):
        facts = memory_import.split_facts("- Commit messages:\n  - say why\n  - name the card\n"
                                          "- One more rule for good measure.\n")
        self.assertEqual(facts, ["Commit messages:\n- say why\n- name the card",
                                 "One more rule for good measure."])

    def test_codex_parser_ignores_other_sections_and_unversioned_header(self):
        facts = memory_import.codex_summary("## User preferences\n- Likes tables for comparisons.\n"
                                            "## General Tips\n- not imported\n")
        self.assertEqual(facts, ["Likes tables for comparisons."])

    def test_failed_save_is_retried_and_not_marked_offered(self):
        calls = []

        def broken(fact, **kw):
            calls.append(fact)
            raise OSError("disk full")

        summary = memory_import.import_all(home=self.home, suggest=broken)
        self.assertGreater(summary["errors"], 0)
        again = memory_import.import_all(home=self.home)
        self.assertGreater(again["claude"], 0)
        self.assertEqual(again["unchanged"], 2)       # project and reference: nothing to offer

    def test_switch_and_start_emit(self):
        self.assertFalse(memory_import.enabled({"RELAY_MEMORY_IMPORT": "off"}))
        self.assertTrue(memory_import.enabled({}))
        self.assertIsNone(memory_import.start(lambda e: None, home=self.home,
                                              env={"RELAY_MEMORY_IMPORT": "off"}))
        events = []
        memory_import.start(events.append, home=self.home, env={}).join(10)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "memory_import")
        self.assertGreater(events[0]["claude"], 0)
        self.assertEqual(events[0]["codex"], 0)
        events.clear()
        memory_import.start(events.append, home=self.home, env={}).join(10)
        self.assertEqual(events, [])                  # nothing new: nothing to say

    def test_startup_import_follows_the_configure_field(self):
        events = []
        absent = memory_import.StartupImport(events.append, home=self.home, env={})
        absent.configure(None)
        absent.thread.join(10)
        absent.configure(None)                        # a second configure starts nothing
        self.assertEqual(events, [])                  # imported, but nobody asked for the event
        self.assertGreater(len(memory_suggestions.pending()), 0)
        absent.configure(True)                        # a GUI that asks later still gets it once
        self.assertEqual([e["event"] for e in events], ["memory_import"])
        absent.configure(True)
        self.assertEqual(len(events), 1)

    def test_startup_import_true_emits_and_false_skips(self):
        events = []
        wanted = memory_import.StartupImport(events.append, home=self.home, env={})
        wanted.configure(True)
        wanted.thread.join(10)
        self.assertEqual(len(events), 1)
        self.assertGreater(events[0]["claude"], 0)
        skipped = memory_import.StartupImport(events.append, home=self.home, env={})
        skipped.configure(False)
        self.assertIsNone(skipped.thread)
        with self.assertRaises(ValueError):
            skipped.configure("yes")

    def test_worker_request_imports_without_a_configure(self):
        # #MEMS: a pane whose configure waits for its first prompt sends `memory_import` at ready,
        # and the real worker imports and says how many, with no configure at all.
        import subprocess, threading
        root = Path(__file__).resolve().parents[1]
        env = {**os.environ, "HOME": str(self.home), "RELAY_KEYRING": "off"}
        proc = subprocess.Popen([sys.executable, "-S", "-u", str(root / "backend" / "worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, cwd=self.tmp.name, env=env)
        timer = threading.Timer(30, proc.kill)
        timer.start()
        events = []
        try:
            proc.stdin.write(json.dumps({"type": "memory_import", "enabled": True}) + "\n")
            proc.stdin.flush()
            for line in proc.stdout:
                events.append(json.loads(line))
                if events[-1].get("event") in ("memory_import", "error"):
                    break
            proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n")
            proc.stdin.flush()
        finally:
            timer.cancel()
            proc.wait(timeout=10)
            proc.stdin.close()
            proc.stdout.close()
        told = [e for e in events if e.get("event") == "memory_import"]
        self.assertEqual(len(told), 1, events)
        self.assertGreater(told[0]["claude"], 0)
        self.assertGreater(len(memory_suggestions.pending()), 0)

    def test_start_never_raises(self):
        with mock.patch.object(memory_import, "import_all", side_effect=RuntimeError("boom")):
            events = []
            memory_import.start(events.append, home=self.home, env={}).join(10)
        self.assertEqual(events, [])

    def test_no_sources_touches_nothing(self):
        empty = Path(self.tmp.name) / "empty-home"
        empty.mkdir()
        summary = memory_import.import_all(home=empty)
        self.assertEqual(summary["claude"] + summary["codex"], 0)
        self.assertFalse(self.store.exists())


if __name__ == "__main__":
    unittest.main()

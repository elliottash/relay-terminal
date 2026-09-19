# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from relay_core import guest_slash


class GuestSlashTests(unittest.TestCase):
    def test_claude_catalog_unions_builtins_skills_and_legacy_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            home = root / "home"
            (home / ".claude" / "skills" / "global").mkdir(parents=True)
            (home / ".claude" / "skills" / "global" / "SKILL.md").write_text("# global")
            (root / ".claude" / "skills" / "project").mkdir(parents=True)
            (root / ".claude" / "skills" / "project" / "SKILL.md").write_text("# project")
            (root / ".claude" / "commands" / "nested").mkdir(parents=True)
            (root / ".claude" / "commands" / "legacy.md").write_text("# legacy")
            (root / ".claude" / "commands" / "nested" / "review.md").write_text("# review")
            (root / ".claude" / "skills" / "ignored").mkdir()
            # Claude Code reads a personal command directory as well as the project's, and offers
            # its commands in every project; the catalog has to say the same thing the guest does.
            (home / ".claude" / "commands").mkdir(parents=True)
            (home / ".claude" / "commands" / "personal.md").write_text("# personal")

            catalog = guest_slash.claude_commands(root, home)

        self.assertIn("/model", catalog)
        self.assertIn("/global", catalog)
        self.assertIn("/project", catalog)
        self.assertIn("/legacy", catalog)
        self.assertIn("/review", catalog)
        self.assertIn("/personal", catalog)
        self.assertNotIn("/ignored", catalog)
        self.assertEqual(catalog, sorted(set(catalog), key=str.casefold))

    def test_codex_catalog_is_static(self):
        self.assertEqual(guest_slash.commands("codex"), list(guest_slash.CODEX_BUILTINS))

    def test_catalog_rejects_unsafe_file_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            command_dir = root / ".claude" / "commands"
            command_dir.mkdir(parents=True)
            (command_dir / "okay.md").write_text("")
            (command_dir / "not a command.md").write_text("")
            catalog = guest_slash.claude_commands(root, root / "home")
        self.assertIn("/okay", catalog)
        self.assertNotIn("/not a command", catalog)

    def test_emit_uses_guest_event_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            capture = Path(directory) / "payload.json"
            helper = Path(directory) / "helper.py"
            helper.write_text(
                "#!/usr/bin/env python3\n"
                "import os, pathlib, sys\n"
                "pathlib.Path(os.environ['CAPTURE']).write_text(' '.join(sys.argv[1:]) + '\\n' + sys.stdin.read())\n"
            )
            helper.chmod(0o755)
            with patch.dict(os.environ, {"RELAY_GUEST_EVENT": str(helper), "CAPTURE": str(capture)}, clear=False):
                self.assertTrue(guest_slash.emit("codex", directory))
            invocation, payload = capture.read_text().split("\n", 1)
        self.assertEqual(invocation, "slash codex")
        self.assertEqual(json.loads(payload)["commands"], list(guest_slash.CODEX_BUILTINS))

    def test_emit_runs_the_helper_through_an_interpreter_not_its_mode_bit(self):
        """`relay_core.guest_codex` names the interpreter and so does this: a `guest-event.py`
        that arrived without its execute bit must still carry the catalog (one channel, one way
        of calling it, 26.3)."""
        with tempfile.TemporaryDirectory() as directory:
            capture = Path(directory) / "payload.json"
            helper = Path(directory) / "helper.py"
            helper.write_text(
                "import os, pathlib, sys\n"
                "pathlib.Path(os.environ['CAPTURE']).write_text(' '.join(sys.argv[1:]))\n")
            helper.chmod(0o644)          # no execute bit at all
            with patch.dict(os.environ, {"RELAY_GUEST_EVENT": str(helper),
                                         "CAPTURE": str(capture)}, clear=False):
                self.assertTrue(guest_slash.emit("codex", directory))
            self.assertEqual("slash codex", capture.read_text())

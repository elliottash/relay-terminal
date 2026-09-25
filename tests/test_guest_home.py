# SPDX-License-Identifier: AGPL-3.0-or-later
"""The guests' Relay-owned home (card #5A37): what `guest_home.ensure()` shares into it, the
generated settings and their overlay, and where a resumed conversation runs."""
from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from relay_core import guest, guest_home, guest_launch, guest_sessions  # noqa: E402


class GuestHomeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.home = root / "home"
        self.user_claude = self.home / ".claude"
        self.user_codex = self.home / ".codex"
        self.relay = root / "data" / "relay" / "guests"
        for path in (self.user_claude / "skills" / "mine", self.user_codex / "skills"):
            path.mkdir(parents=True)
        (self.user_claude / "CLAUDE.md").write_text("be terse\n")
        (self.user_claude / ".credentials.json").write_text('{"token": "x"}')
        (self.user_claude / "settings.json").write_text(json.dumps({"model": "opus", "theme": "dark"}))
        (self.home / ".claude.json").write_text(json.dumps({"mcpServers": {"a": {}}}))
        (self.user_codex / "config.toml").write_text('model = "gpt"\n')
        (self.user_codex / "auth.json").write_text("{}")
        env = {"HOME": str(self.home), guest.RELAY_HOME_ENV: str(self.relay),
               "RELAY_USER_CLAUDE_CONFIG_DIR": "", "RELAY_USER_CODEX_HOME": ""}
        self.env = mock.patch.dict(os.environ, env)
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def user_tree(self):
        return sorted((str(p), p.stat().st_mtime_ns) for p in self.home.rglob("*"))

    def test_paths_follow_the_relay_home_only_when_it_is_set(self):
        self.assertEqual(guest.config_dir("claude"), str(self.relay / "claude"))
        self.assertEqual(guest.user_config_dir("claude"), str(self.user_claude))
        self.assertEqual(guest.claude_projects_dir(), str(self.relay / "claude" / "projects"))
        self.assertEqual(guest.config_dir("claude", str(self.home)), str(self.user_claude))
        for off in ("off", "", "relative/dir"):
            with mock.patch.dict(os.environ, {guest.RELAY_HOME_ENV: off}):
                self.assertEqual(guest.config_dir("codex"), str(self.user_codex))
                self.assertEqual(guest_home.ensure("codex"), {})

    def test_ensure_links_the_users_setup_and_never_writes_theirs(self):
        before = self.user_tree()
        report = guest_home.ensure("claude")
        claude = self.relay / "claude"
        self.assertEqual(os.readlink(claude / "skills"), str(self.user_claude / "skills"))
        self.assertEqual(os.readlink(claude / ".credentials.json"), str(self.user_claude / ".credentials.json"))
        self.assertIn("CLAUDE.md", report["linked"])
        self.assertFalse((claude / "agents").exists())            # nothing to share
        self.assertFalse((claude / ".claude.json").is_symlink())  # copied: claude rewrites it
        self.assertEqual(json.loads((claude / ".claude.json").read_text()), {"mcpServers": {"a": {}}})
        settings = json.loads((claude / "settings.json").read_text())
        self.assertEqual(settings, {"model": "opus", "theme": "dark",
                                    "cleanupPeriodDays": guest_home.RETENTION_DAYS})
        self.assertEqual(oct((claude / "settings.json").stat().st_mode & 0o777), "0o600")
        codex = guest_home.ensure("codex")
        self.assertEqual(sorted(codex["linked"]), ["auth.json", "config.toml", "skills"])
        self.assertEqual(before, self.user_tree())
        # Idempotent: a second run changes nothing.
        again = guest_home.ensure("claude")
        self.assertEqual((again["linked"], again["removed"], again["settings"]), ([], [], False))

    def test_the_homes_own_file_is_never_replaced_and_dead_links_go(self):
        claude = self.relay / "claude"
        claude.mkdir(parents=True)
        (claude / "CLAUDE.md").write_text("relay's own\n")
        guest_home.ensure("claude")
        self.assertEqual((claude / "CLAUDE.md").read_text(), "relay's own\n")
        (self.user_claude / "CLAUDE.md").unlink()
        (claude / "CLAUDE.md").unlink()
        guest_home.ensure("claude")
        (self.user_claude / ".credentials.json").unlink()
        report = guest_home.ensure("claude")
        self.assertEqual(report["removed"], [".credentials.json"])
        self.assertFalse(os.path.lexists(claude / ".credentials.json"))

    def test_settings_keep_what_claude_changed_and_follow_the_users_file(self):
        guest_home.ensure("claude")
        path = self.relay / "claude" / "settings.json"
        generated = json.loads(path.read_text())
        generated["model"] = "sonnet"                      # `/model` inside the Relay home
        generated["cleanupPeriodDays"] = 30                # never lets claude prune again
        path.write_text(json.dumps(generated))
        (self.user_claude / "settings.json").write_text(json.dumps({"model": "opus", "theme": "light"}))
        guest_home.ensure("claude")
        self.assertEqual(json.loads(path.read_text()),
                         {"model": "sonnet", "theme": "light",
                          "cleanupPeriodDays": guest_home.RETENTION_DAYS})

    def test_a_conversation_from_before_resumes_in_the_users_directory(self):
        old = self.user_claude / "projects" / "-w" / "abc-123.jsonl"
        old.parent.mkdir(parents=True)
        old.write_text("{}\n")
        self.assertEqual(guest_home.home_for_resume("claude", "abc-123"), str(self.user_claude))
        self.assertEqual(guest_home.home_for_resume("claude", "new-id"), "")
        self.assertEqual(guest_home.home_for_resume("claude", "../x"), "")
        launch = guest_launch.home_environment("claude", ["-r", "abc-123"])
        self.assertEqual(launch, {"CLAUDE_CONFIG_DIR": str(self.user_claude)})
        self.assertEqual(guest_launch.home_environment("claude", []), {})
        mine = self.relay / "claude" / "projects" / "-w" / "abc-123.jsonl"
        mine.parent.mkdir(parents=True)
        mine.write_text("{}\n")
        self.assertEqual(guest_home.home_for_resume("claude", "abc-123"), "")
        rollout = (self.user_codex / "sessions" / "2026" / "09" / "01"
                   / "rollout-2026-09-01T10-00-00-0199aaaa-bbbb-cccc-dddd-eeeeeeeeeeee.jsonl")
        rollout.parent.mkdir(parents=True)
        rollout.write_text("{}\n")
        thread = "0199aaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
        self.assertEqual(guest_launch.resumed_id("codex", ["resume", thread]), thread)
        self.assertEqual(guest_launch.home_environment("codex", ["resume", thread]),
                         {"CODEX_HOME": str(self.user_codex)})

    def test_sessions_are_read_from_both_homes(self):
        for root, name in ((self.user_claude, "old-one"), (self.relay / "claude", "new-one")):
            path = root / "projects" / "-w" / f"{name}.jsonl"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"type": "user", "sessionId": name, "cwd": "/w",
                                        "message": {"role": "user", "content": "hello " + name}}) + "\n")
        with mock.patch.object(guest_sessions.guest_accounts, "accounts", return_value=[]):
            ids = {record["id"] for record in guest_sessions.scan_claude()}
            self.assertEqual(ids, {"old-one", "new-one"})
            self.assertEqual(guest_sessions.live_transcript("claude", session_id="old-one"),
                             self.user_claude / "projects" / "-w" / "old-one.jsonl")


if __name__ == "__main__":
    unittest.main()

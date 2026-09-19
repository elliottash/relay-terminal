"""The marked settings installer (GT7X, protocol 26.4).

The file Relay writes is the project's `.claude/settings.local.json` — the per-developer one
Claude Code keeps out of git — and never the shared `.claude/settings.json`: a commit of that
would hand every teammate a hook command that does nothing but fail on their machine.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from relay_core import guest_install as installer

BACKEND = str(Path(__file__).resolve().parents[1] / "backend")

REALISTIC = {
    "permissions": {"allow": ["Bash(ls:*)", "Read"]},
    "hooks": {
        "PreToolUse": [
            {"matcher": "Bash", "hooks": [{"type": "command", "command": "~/bin/guard.sh"}]}
        ],
        "Stop": [
            {"hooks": [{"type": "command", "command": "~/bin/notify.sh", "timeout": 5}]}
        ],
    },
    "statusLine": {"type": "command", "command": "~/bin/my-statusline.sh"},
    "model": "opus",
}


def write(path: Path, settings) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


class WhichFile(unittest.TestCase):
    def test_the_project_file_is_the_local_one_git_does_not_carry(self):
        self.assertEqual(Path(".claude") / "settings.local.json", installer.SETTINGS_RELATIVE)
        self.assertTrue(str(installer.project_settings_path("/w")).endswith(
            "/w/.claude/settings.local.json"))

    def test_the_shared_project_file_is_never_written(self):
        with tempfile.TemporaryDirectory() as root:
            installer.install(installer.project_settings_path(root))
            self.assertFalse((Path(root) / ".claude" / "settings.json").exists())
            self.assertTrue((Path(root) / ".claude" / "settings.local.json").exists())

    def test_the_global_file_is_the_users_own_settings_json(self):
        self.assertTrue(str(installer.global_settings_path("/h")).endswith("/h/.claude/settings.json"))


class TheCommand(unittest.TestCase):
    """A settings file is read by every claude started in that project, so the command has to be
    harmless when there is no Relay around it: no empty expansion, no PYTHONPATH, exit 0."""

    def test_it_is_a_no_op_outside_a_pane(self):
        for command in (installer.hook_command("Stop"), installer.STATUSLINE_COMMAND):
            with self.subTest(command=command):
                run = subprocess.run(["/bin/sh", "-c", command], capture_output=True, text=True,
                                     env={"PATH": os.environ.get("PATH", os.defpath)})
                self.assertEqual((0, "", ""), (run.returncode, run.stdout, run.stderr))

    def test_it_runs_the_shim_by_absolute_path_inside_a_pane(self):
        """The old form was `"$RELAY_PYTHON" -m relay_core.guest_hook`, which needed PYTHONPATH
        and expanded to an empty command — exit 127 — outside a pane."""
        with tempfile.TemporaryDirectory() as root:
            environment = {"PATH": os.environ.get("PATH", os.defpath),
                           "RELAY_GUEST_EVENT": str(Path(root) / "guest-events"),
                           "RELAY_RUNTIME_DIR": root, "RELAY_SESSION_TOKEN": "tok-1",
                           "RELAY_BACKEND_DIR": BACKEND, "RELAY_PYTHON": sys.executable}
            run = subprocess.run(["/bin/sh", "-c", installer.hook_command("Stop")],
                                 input='{"session_id": "s"}', text=True, capture_output=True,
                                 env=environment)
            self.assertEqual((0, "", ""), (run.returncode, run.stdout, run.stderr))
            spooled = list((Path(root) / "guest-events").glob("*.json"))
            self.assertEqual(1, len(spooled))
            envelope = json.loads(spooled[0].read_text(encoding="utf-8"))
        self.assertEqual(("hook", "Stop"), (envelope["event"], envelope["data"]["name"]))
        self.assertNotIn("PYTHONPATH", installer.hook_command("Stop"))


class Install(unittest.TestCase):
    def test_empty_project_gets_every_marked_entry(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            result = installer.install(path)
            settings = read(path)
        self.assertEqual(["hooks.PermissionRequest", "hooks.UserPromptSubmit", "hooks.Stop",
                          "hooks.Notification", "statusLine"], result["installed"])
        for event in installer.HOOK_EVENTS:
            command = settings["hooks"][event][0]["hooks"][0]
            self.assertEqual("command", command["type"])
            self.assertIn(f'guest_hook.py" {event}', command["command"])
            self.assertTrue(installer.marked(command))
        self.assertTrue(installer.marked(settings["statusLine"]))

    def test_pretooluse_is_not_installed(self):
        """It fires before *every* tool call, including the ones the user's own permission rules
        allow silently; holding each of those open stalled the guest (review of 51587e3).
        PermissionRequest fires only when claude is really about to ask."""
        self.assertNotIn("PreToolUse", installer.HOOK_EVENTS)
        self.assertIn("PermissionRequest", installer.HOOK_EVENTS)

    def test_every_hook_carries_a_timeout(self):
        """Claude Code's own default is 600 s, which is not a wait Relay should inherit."""
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            installer.install(path)
            settings = read(path)
        timeouts = {event: settings["hooks"][event][0]["hooks"][0]["timeout"]
                    for event in installer.HOOK_EVENTS}
        self.assertEqual(installer.HOOK_TIMEOUT_DEFAULT, timeouts["Stop"])
        # The question outlives the shim's own wait, so the shim decides when to give up.
        from relay_core import guest_hook
        self.assertGreater(timeouts["PermissionRequest"], guest_hook.PERMISSION_TIMEOUT_DEFAULT)

    def test_the_file_keeps_its_indentation_and_its_mode(self):
        """`save` re-serialises the whole object, so it must reproduce what it found: mkstemp
        makes a 0600 file, and re-indenting a four-space file is a diff nobody asked for."""
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(REALISTIC, indent=4) + "\n", encoding="utf-8")
            os.chmod(path, 0o640)
            installer.install(path)
            text = path.read_text(encoding="utf-8")
            self.assertEqual(0o640, os.stat(path).st_mode & 0o777)
        self.assertIn('\n    "permissions": {', text)
        self.assertNotIn('\n  "permissions": {', text)

    def test_a_tab_indented_file_stays_tab_indented(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(REALISTIC, indent="\t"), encoding="utf-8")
            installer.install(path)
            text = path.read_text(encoding="utf-8")
        self.assertIn('\n\t"permissions": {', text)
        self.assertFalse(text.endswith("\n"))   # the file had no trailing newline, and still has none

    def test_a_new_file_is_not_left_private_to_mkstemp(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            installer.install(path)
            mode = os.stat(path).st_mode & 0o777
        umask = os.umask(0o077)
        os.umask(umask)
        self.assertEqual(0o666 & ~umask, mode)

    def test_reinstall_is_idempotent(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            installer.install(path)
            installer.install(path)
            settings = read(path)
        for event in installer.HOOK_EVENTS:
            self.assertEqual(1, len(settings["hooks"][event]))

    def test_realistic_existing_settings_are_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            write(path, REALISTIC)
            result = installer.install(path)
            settings = read(path)
        self.assertEqual(REALISTIC["permissions"], settings["permissions"])
        self.assertEqual("opus", settings["model"])
        self.assertIn(REALISTIC["hooks"]["PreToolUse"][0], settings["hooks"]["PreToolUse"])
        self.assertIn(REALISTIC["hooks"]["Stop"][0], settings["hooks"]["Stop"])
        # A statusLine is one slot, not a list: the user's own line wins.
        self.assertEqual("kept", result["statusline"])
        self.assertEqual(REALISTIC["statusLine"], settings["statusLine"])

    def test_marker_is_a_token_not_a_substring(self):
        self.assertFalse(installer.marked({"command": "/opt/--relay-guest/bin/x"}))
        self.assertTrue(installer.marked({"command": "x --relay-guest"}))

    def test_status_and_is_installed_agree(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            self.assertFalse(installer.is_installed(path))
            installer.install(path)
            report = installer.status(path)
            self.assertTrue(installer.is_installed(path))
            self.assertTrue(report["installed"])
            self.assertEqual(sorted(installer.HOOK_EVENTS), report["events"])


class Remove(unittest.TestCase):
    def test_realistic_install_remove_round_trip(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            write(path, REALISTIC)
            installer.install(path)
            result = installer.remove(path)
            settings = read(path)
            self.assertTrue(result["changed"])
            self.assertEqual(REALISTIC, settings)
            self.assertFalse(installer.is_installed(path))

    def test_an_uninstalled_file_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            write(path, REALISTIC)
            before = path.read_text(encoding="utf-8")
            result = installer.remove(path)
            self.assertFalse(result["changed"])
            self.assertEqual(before, path.read_text(encoding="utf-8"))

    def test_removing_from_an_empty_project_creates_nothing(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            result = installer.remove(path)
        self.assertFalse(result["changed"])
        self.assertFalse(path.exists())

    def test_a_mixed_group_loses_only_the_marked_command(self):
        mixed = {"hooks": {"Stop": [{"hooks": [
            {"type": "command", "command": "~/bin/mine.sh"},
            {"type": "command", "command": installer.hook_command("Stop")},
        ]}]}}
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            write(path, mixed)
            installer.remove(path)
            settings = read(path)
        self.assertEqual([{"type": "command", "command": "~/bin/mine.sh"}],
                         settings["hooks"]["Stop"][0]["hooks"])


class InvalidSettings(unittest.TestCase):
    def test_invalid_json_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            path.parent.mkdir(parents=True)
            path.write_text("{not json", encoding="utf-8")
            for action in (installer.install, installer.remove):
                with self.subTest(action=action.__name__), self.assertRaises(installer.SettingsError):
                    action(path)
            self.assertEqual("{not json", path.read_text(encoding="utf-8"))
            self.assertFalse(installer.is_installed(path))

    def test_non_object_or_malformed_hooks_are_refused(self):
        bad = ([1, 2], {"hooks": "nope"}, {"hooks": {"Stop": {"hooks": []}}})
        for value in bad:
            with self.subTest(value=value), tempfile.TemporaryDirectory() as root:
                path = installer.project_settings_path(root)
                write(path, value)
                with self.assertRaises(installer.SettingsError):
                    installer.install(path)
                self.assertEqual(value, read(path))


if __name__ == "__main__":
    unittest.main()

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


class Install(unittest.TestCase):
    def test_empty_project_gets_every_marked_entry(self):
        with tempfile.TemporaryDirectory() as root:
            path = installer.project_settings_path(root)
            result = installer.install(path)
            settings = read(path)
        self.assertEqual(["hooks.PreToolUse", "hooks.UserPromptSubmit", "hooks.Stop",
                          "hooks.Notification", "statusLine"], result["installed"])
        for event in installer.HOOK_EVENTS:
            command = settings["hooks"][event][0]["hooks"][0]
            self.assertEqual("command", command["type"])
            self.assertIn(f"relay_core.guest_hook {event}", command["command"])
            self.assertTrue(installer.marked(command))
        self.assertTrue(installer.marked(settings["statusLine"]))

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
            {"type": "command", "command": installer.HOOK_COMMAND.format(event="Stop")},
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


class CommandLine(unittest.TestCase):
    def run_cli(self, *args):
        run = subprocess.run([sys.executable, "-m", "relay_core.guest_install", *args],
                             text=True, capture_output=True, env={**os.environ, "PYTHONPATH": BACKEND})
        return run.returncode, json.loads(run.stdout), run.stderr

    def test_project_on_status_off(self):
        with tempfile.TemporaryDirectory() as root:
            code, out, err = self.run_cli("--project", root, "--on")
            self.assertEqual((0, True, ""), (code, out["ok"], err))
            code, out, err = self.run_cli("--project", root, "--status")
            self.assertEqual((0, True, ""), (code, out["installed"], err))
            code, out, err = self.run_cli("--project", root, "--off")
            self.assertEqual((0, True, ""), (code, out["changed"], err))

    def test_global_needs_the_explicit_second_opt_in(self):
        with tempfile.TemporaryDirectory() as home:
            code, out, err = self.run_cli("--global", "--on", "--home", home)
            self.assertEqual((2, False, ""), (code, out["ok"], err))
            self.assertFalse((Path(home) / ".claude" / "settings.json").exists())

    def test_global_with_the_opt_in(self):
        with tempfile.TemporaryDirectory() as home:
            code, out, err = self.run_cli("--global", "--on", "--global-opt-in", "--home", home)
            self.assertEqual((0, True, ""), (code, out["ok"], err))
            self.assertIn("hooks", read(Path(home) / ".claude" / "settings.json"))


if __name__ == "__main__":
    unittest.main()

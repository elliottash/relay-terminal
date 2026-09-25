"""Launch-time configuration for a guest picked in the model picker (GT7X, protocol 26.9).

Nothing here touches the real home directory: every case builds its own home, runtime dir and
project, and the launch is asked for its command line rather than run (there is no claude or
codex to run in a unit test, and the real ones would cost a session).
"""
import contextlib
import io
import json
import os
from pathlib import Path
import shlex
import tempfile
import unittest

from relay_core import guest_codex, guest_home, guest_install, guest_launch


class ClaudeSettingsFile(unittest.TestCase):
    def test_the_launch_file_holds_exactly_the_installers_entries(self):
        """One source (26.4): what `claude --settings` reads is what the retired installer wrote,
        entry for entry, so the shim's guard, path, marker and timeouts are unchanged."""
        with tempfile.TemporaryDirectory() as root:
            path = guest_launch.write_claude_settings(root, cwd=None, home=root)
            settings = json.loads(Path(path).read_text(encoding="utf-8"))
        # Plus the retention that keeps claude from pruning Relay's transcripts (#5A37).
        self.assertEqual(settings.pop("cleanupPeriodDays"), guest_home.RETENTION_DAYS)
        self.assertEqual(guest_install.relay_entries(), settings)
        self.assertEqual(sorted(guest_install.HOOK_EVENTS), sorted(settings["hooks"]))
        for event, groups in settings["hooks"].items():
            command = groups[0]["hooks"][0]["command"]
            self.assertTrue(command.startswith(guest_install.GUARD), command)
            self.assertIn(guest_install.MARKER, command.split())
            self.assertEqual(guest_install.HOOK_TIMEOUTS.get(event, guest_install.HOOK_TIMEOUT_DEFAULT),
                             groups[0]["hooks"][0]["timeout"])
        self.assertTrue(guest_install.marked(settings["statusLine"]))

    def test_the_file_lives_under_the_runtime_dir_and_is_private(self):
        with tempfile.TemporaryDirectory() as root:
            path = guest_launch.write_claude_settings(root, home=root)
            self.assertEqual(os.path.join(root, guest_launch.SETTINGS_DIR, guest_launch.CLAUDE_SETTINGS_NAME), path)
            self.assertEqual(0o600, os.stat(path).st_mode & 0o777)
            self.assertEqual(0o700, os.stat(os.path.dirname(path)).st_mode & 0o777)
            # A second launch in the same pane replaces the file rather than piling up copies.
            guest_launch.write_claude_settings(root, home=root)
            self.assertEqual([guest_launch.CLAUDE_SETTINGS_NAME], os.listdir(os.path.dirname(path)))

    def test_a_users_own_statusline_is_kept(self):
        """A command-line settings file wins over the user's files, so Relay's statusline would
        replace one they wrote. The installer kept theirs; so does the launch (the chip then has
        nothing to show), whichever of the three files it is in."""
        for where in ("global", "project", "local"):
            with self.subTest(where=where), tempfile.TemporaryDirectory() as root:
                home, project = Path(root, "home"), Path(root, "project")
                target = {"global": home / ".claude" / "settings.json",
                          "project": project / ".claude" / "settings.json",
                          "local": project / ".claude" / "settings.local.json"}[where]
                target.parent.mkdir(parents=True)
                target.write_text(json.dumps({"statusLine": {"type": "command", "command": "my-line"}}))
                path = guest_launch.write_claude_settings(str(Path(root, "run")), cwd=str(project), home=str(home))
                settings = json.loads(Path(path).read_text(encoding="utf-8"))
                self.assertNotIn("statusLine", settings)
                self.assertEqual(sorted(guest_install.HOOK_EVENTS), sorted(settings["hooks"]))

    def test_relays_own_leftover_statusline_does_not_count_as_the_users(self):
        with tempfile.TemporaryDirectory() as root:
            home = Path(root, "home")
            (home / ".claude").mkdir(parents=True)
            (home / ".claude" / "settings.json").write_text(json.dumps(
                {"statusLine": {"type": "command", "command": guest_install.STATUSLINE_COMMAND}}))
            self.assertFalse(guest_launch.user_statusline_present(None, str(home)))

    def test_no_runtime_dir_is_an_error_not_a_file_somewhere(self):
        with self.assertRaises(guest_launch.LaunchError):
            guest_launch.write_claude_settings("")


class Argv(unittest.TestCase):
    def test_claude(self):
        self.assertEqual(["claude", "--settings", "/r/guest/claude-settings.json",
                          "--dangerously-skip-permissions"],
                         guest_launch.claude_argv("/r/guest/claude-settings.json"))
        self.assertEqual(["claude", "--settings", "/s", "--dangerously-skip-permissions", "-r", "abc", "--fork-session"],
                         guest_launch.claude_argv("/s", ["-r", "abc", "--fork-session"]))

    def test_codex_carries_notify_and_the_bypass_as_overrides(self):
        argv = guest_launch.codex_argv("/usr/bin/python3")
        self.assertEqual("codex", argv[0])
        self.assertEqual(["--dangerously-bypass-approvals-and-sandbox", "--dangerously-bypass-hook-trust"],
                         argv[-2:])
        overrides = [argv[i + 1] for i, word in enumerate(argv) if word == "-c"]
        self.assertEqual(2, len(overrides))
        notify = next(o for o in overrides if o.startswith("notify="))
        self.assertIn('"/usr/bin/python3"', notify)
        self.assertIn('"notify"', notify)
        self.assertIn(guest_codex.__file__.rsplit("/", 1)[-1], notify)
        self.assertIn('tui.notification_condition="always"', overrides)

    def test_codex_resume_and_fork_keep_the_subcommand_in_front(self):
        """`codex resume <id>`: the subcommand first, the flags after it, the id last — the shape
        `codex resume --help` documents."""
        for sub in guest_launch.CODEX_SUBCOMMANDS:
            with self.subTest(sub=sub):
                argv = guest_launch.codex_argv("/py", [sub, "0195-abc"])
                self.assertEqual(["codex", sub], argv[:2])
                self.assertEqual("0195-abc", argv[-1])
                self.assertIn("--dangerously-bypass-approvals-and-sandbox", argv)
                # No "Hooks need review" screen in front of a resumed session either.
                self.assertIn("--dangerously-bypass-hook-trust", argv)

    def test_claude_takes_the_panes_model_and_effort_after_the_bypass(self):
        argv = guest_launch.claude_argv("/s", model="fable", effort="xhigh")
        self.assertEqual(["claude", "--settings", "/s", "--dangerously-skip-permissions",
                          "--model", "fable", "--effort", "xhigh"], argv)
        # A resumed row keeps its own words, and they stay last.
        argv = guest_launch.claude_argv("/s", ["-r", "abc"], model="opus", effort="max")
        self.assertEqual(["-r", "abc"], argv[-2:])
        self.assertEqual(argv[argv.index("--model") + 1], "opus")

    def test_claude_does_not_repeat_a_model_or_effort_the_row_already_names(self):
        argv = guest_launch.claude_argv("/s", ["--model=haiku", "--effort", "low", "-r", "abc"],
                                        model="opus", effort="max")
        self.assertEqual(argv.count("--model"), 0)       # only the row's `--model=haiku`
        self.assertEqual(argv.count("--effort"), 1)
        self.assertEqual(argv[argv.index("--effort") + 1], "low")
        self.assertNotIn("opus", argv)
        self.assertNotIn("max", argv)

    def test_codex_takes_the_model_and_the_effort_as_a_flag_and_an_override(self):
        argv = guest_launch.codex_argv("/py", model="gpt-6-sol", effort="ultra")
        self.assertEqual(argv[argv.index("-m") + 1], "gpt-6-sol")
        overrides = [argv[i + 1] for i, word in enumerate(argv) if word == "-c"]
        self.assertIn('model_reasoning_effort="ultra"', overrides)
        # The flags stay in front of the bypass pair, which stays last.
        self.assertEqual(["--dangerously-bypass-approvals-and-sandbox",
                          "--dangerously-bypass-hook-trust"], argv[-2:])

    def test_codex_resume_keeps_the_id_last_with_the_model_and_effort_in_the_flags(self):
        argv = guest_launch.codex_argv("/py", ["resume", "0195-abc"], model="gpt-5.5",
                                       effort="high")
        self.assertEqual(["codex", "resume"], argv[:2])
        self.assertEqual("0195-abc", argv[-1])
        self.assertLess(argv.index("-m"), argv.index("0195-abc"))
        self.assertIn('model_reasoning_effort="high"', argv)

    def test_codex_does_not_repeat_what_the_row_already_names(self):
        argv = guest_launch.codex_argv(
            "/py", ["resume", "-m", "gpt-5.5", "-c", 'model_reasoning_effort="low"', "0195-abc"],
            model="gpt-6-astra", effort="max")
        self.assertEqual(argv.count("-m"), 1)
        self.assertNotIn("gpt-6-astra", argv)
        self.assertNotIn('model_reasoning_effort="max"', argv)
        self.assertIn('model_reasoning_effort="low"', argv)

    def test_the_effort_override_parses_as_toml(self):
        import tomllib
        argv = guest_launch.codex_argv("/py", effort="xhigh")
        override = next(w for w in argv if w.startswith(guest_launch.CODEX_EFFORT_KEY + "="))
        key, _, value = override.partition("=")
        self.assertEqual(tomllib.loads(f"{key} = {value}"), {key: "xhigh"})

    def test_the_overrides_parse_as_toml(self):
        """`-c` values are parsed as TOML by codex; a value that does not parse is taken as a raw
        string, which for `notify` would be a program named after the whole array."""
        import tomllib
        for override in guest_launch.codex_overrides("/usr/bin/python3"):
            if override == "-c":
                continue
            key, _, value = override.partition("=")
            document = tomllib.loads(f"{key.rsplit('.', 1)[-1]} = {value}")
            self.assertTrue(document)


class SessionId(unittest.TestCase):
    """A claude Relay starts is told its session id, so its transcript is known and two claudes in
    one directory cannot be mistaken for each other (review B7)."""

    def test_a_fresh_claude_is_given_an_id(self):
        extra, session = guest_launch.claude_session([])
        self.assertEqual(["--session-id", session], extra)
        self.assertRegex(session, r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
        self.assertNotEqual(session, guest_launch.claude_session([])[1])

    def test_a_resumed_claude_keeps_its_own(self):
        for extra in (["-r", "abc-123"], ["--resume", "abc-123"], ["--resume=abc-123"]):
            with self.subTest(extra=extra):
                self.assertEqual((extra, "abc-123"), guest_launch.claude_session(extra))

    def test_a_fork_or_a_continue_has_an_id_relay_cannot_know(self):
        for extra in (["-r", "abc", "--fork-session"], ["-c"], ["--continue"]):
            with self.subTest(extra=extra):
                self.assertEqual((extra, ""), guest_launch.claude_session(extra))

    def test_the_command_line_carries_it_and_reports_it(self):
        with tempfile.TemporaryDirectory() as root:
            fresh = guest_launch.command_line("claude", root, home=root)
            resumed = guest_launch.command_line("claude", root, home=root, extra=["-r", "abc"])
            codex = guest_launch.command_line("codex", root, home=root, python="/py", extra=["resume", "t-1"])
            plain = guest_launch.command_line("codex", root, home=root, python="/py")
        self.assertEqual(fresh["session_id"], fresh["argv"][fresh["argv"].index("--session-id") + 1])
        self.assertEqual("abc", resumed["session_id"])
        self.assertNotIn("--session-id", resumed["argv"])
        self.assertEqual("t-1", codex["session_id"])
        self.assertEqual("", plain["session_id"])


class CommandLine(unittest.TestCase):
    def test_claude_with_a_live_bridge(self):
        with tempfile.TemporaryDirectory() as root:
            home, run = Path(root, "home"), Path(root, "run")
            home.mkdir()
            result = guest_launch.command_line("claude", str(run), cwd=str(root), port=43123, home=str(home))
        self.assertEqual({"CLAUDE_CODE_SSE_PORT": "43123", "ENABLE_IDE_INTEGRATION": "true"}, result["env"])
        self.assertEqual(result["settings"], result["argv"][2])
        self.assertTrue(result["command"].startswith("CLAUDE_CODE_SSE_PORT=43123 ENABLE_IDE_INTEGRATION=true claude "))
        # What the shell will run is the argv: the words survive quoting.
        words = shlex.split(result["command"])
        self.assertEqual(result["argv"], words[len(result["env"]):])
        self.assertEqual([], result["legacy"])

    def test_claude_without_a_bridge_gets_no_bridge_variables(self):
        with tempfile.TemporaryDirectory() as root:
            result = guest_launch.command_line("claude", root, port=0, home=root)
        self.assertEqual({}, result["env"])
        self.assertTrue(result["command"].startswith("claude --settings "))

    def test_codex_never_gets_the_bridge_variables(self):
        with tempfile.TemporaryDirectory() as root:
            result = guest_launch.command_line("codex", root, port=43123, home=root, python="/py")
        self.assertEqual({}, result["env"])
        self.assertEqual("", result["settings"])
        self.assertFalse(os.path.exists(os.path.join(root, guest_launch.SETTINGS_DIR)))
        self.assertTrue(result["command"].startswith("codex -c "))

    def test_a_path_with_a_space_is_quoted(self):
        with tempfile.TemporaryDirectory() as root:
            run = os.path.join(root, "my run")
            result = guest_launch.command_line("claude", run, home=root)
        self.assertEqual(result["argv"], shlex.split(result["command"]))

    def test_an_unknown_guest_is_an_error(self):
        with tempfile.TemporaryDirectory() as root, self.assertRaises(ValueError):
            guest_launch.command_line("gemini", root, home=root)


class LegacyCleanup(unittest.TestCase):
    """The stopgap's marked entries would run every hook twice beside the launch file (26.9), so
    a launch removes exactly them from the three files it once wrote — and nothing else."""

    def test_marked_entries_are_removed_from_all_three_files(self):
        with tempfile.TemporaryDirectory() as root:
            home, project = Path(root, "home"), Path(root, "project")
            project.mkdir()
            local = guest_install.project_settings_path(project)
            globl = guest_install.global_settings_path(home)
            for path in (local, globl):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps({"permissions": {"allow": ["Bash(ls:*)"]}}))
                guest_install.install(path)
            codex = Path(guest_codex.settings_path(str(home)))
            codex.parent.mkdir(parents=True, exist_ok=True)
            codex.write_text('model = "gpt-5"\n')
            guest_codex.enable(home=str(home), python="/py", script="/s.py")
            changed = guest_launch.clean_legacy(str(project), str(home))
            self.assertEqual(sorted([str(local), str(globl), str(codex)]), sorted(changed))
            for path in (local, globl):
                self.assertFalse(guest_install.is_installed(path))
                self.assertEqual(["Bash(ls:*)"], guest_install.load(path)["permissions"]["allow"])
            self.assertEqual('model = "gpt-5"\n', codex.read_text())

    def test_files_without_marked_entries_are_not_rewritten(self):
        with tempfile.TemporaryDirectory() as root:
            home, project = Path(root, "home"), Path(root, "project")
            local = guest_install.project_settings_path(project)
            local.parent.mkdir(parents=True)
            text = '{"hooks": {"Stop": [{"hooks": [{"type": "command", "command": "mine"}]}]}}'
            local.write_text(text)
            before = os.stat(local).st_mtime_ns
            self.assertEqual([], guest_launch.clean_legacy(str(project), str(home)))
            self.assertEqual(text, local.read_text())
            self.assertEqual(before, os.stat(local).st_mtime_ns)
            self.assertFalse((home / ".claude").exists())
            self.assertFalse((home / ".codex").exists())

    def test_a_file_that_is_not_json_is_left_alone(self):
        with tempfile.TemporaryDirectory() as root:
            home, project = Path(root, "home"), Path(root, "project")
            local = guest_install.project_settings_path(project)
            local.parent.mkdir(parents=True)
            local.write_text("not json")
            self.assertEqual([], guest_launch.clean_legacy(str(project), str(home)))
            self.assertEqual("not json", local.read_text())


class Cli(unittest.TestCase):
    def run_cli(self, *args) -> tuple[int, dict]:
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            code = guest_launch.main(list(args))
        return code, json.loads(captured.getvalue())

    def test_one_json_object_with_the_command(self):
        with tempfile.TemporaryDirectory() as root:
            code, out = self.run_cli("claude", "--runtime-dir", root, "--cwd", root, "--port", "5000",
                                     "--home", root, "--", "-r", "abc")
        self.assertEqual(0, code)
        self.assertTrue(out["ok"])
        self.assertEqual(["-r", "abc"], out["argv"][-2:])
        self.assertIn("CLAUDE_CODE_SSE_PORT=5000", out["command"])

    def test_codex_resume_through_the_cli(self):
        with tempfile.TemporaryDirectory() as root:
            code, out = self.run_cli("codex", "--runtime-dir", root, "--home", root, "--python", "/py",
                                     "--", "resume", "abc")
        self.assertEqual(0, code)
        self.assertEqual(["codex", "resume"], out["argv"][:2])
        self.assertEqual("abc", out["argv"][-1])

    def test_the_model_and_effort_flags_reach_the_command(self):
        with tempfile.TemporaryDirectory() as root:
            code, out = self.run_cli("claude", "--runtime-dir", root, "--home", root,
                                     "--model", "sonnet", "--effort", "high")
            self.assertEqual(0, code)
            self.assertEqual(out["argv"][out["argv"].index("--effort") + 1], "high")
            self.assertIn("--model sonnet", out["command"])
            code, out = self.run_cli("codex", "--runtime-dir", root, "--home", root,
                                     "--python", "/py", "--model", "gpt-5.5", "--effort", "max")
        self.assertEqual(0, code)
        self.assertEqual(out["argv"][out["argv"].index("-m") + 1], "gpt-5.5")
        self.assertIn('model_reasoning_effort="max"', out["argv"])

    def test_a_bad_guest_is_reported_not_raised(self):
        with tempfile.TemporaryDirectory() as root:
            code, out = self.run_cli("gemini", "--runtime-dir", root, "--home", root)
        self.assertEqual(1, code)
        self.assertFalse(out["ok"])
        self.assertIn("gemini", out["error"])


if __name__ == "__main__":
    unittest.main()

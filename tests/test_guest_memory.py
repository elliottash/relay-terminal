# SPDX-License-Identifier: AGPL-3.0-or-later
"""Guests use Relay's memory (#MEMS part C): the "guests use memory from" setting, relay | own |
both, on both launch routes — the TUI command line (`guest_launch`) and the harness workers
(`guest_harness_claude` / `guest_harness_codex` through `guest_harness_provider`) — and the
relay_board bridge exposing `app_user_memory` so a guest's suggestions reach Relay's confirm flow.

Every test points RELAY_GLOBAL_SWITCHBOARD at a temporary global Board holding one user memory, so
nothing reads or writes the owner's own.
"""
import io
import json
import os
import shlex
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

from relay_core import guest_harness_provider as ghp
from relay_core import guest_instructions, guest_launch
from relay_core.guest_board_bridge import APP_ALLOW, Bridge
from relay_core.guest_harness import HarnessNotAvailable
from relay_core.guest_harness_claude import ClaudeHarness
from relay_core.guest_harness_codex import CodexHarness

try:
    from tests.guest_harness_fake import FakeHarness
except ImportError:                                            # PYTHONPATH=backend:tests
    from guest_harness_fake import FakeHarness

FACT = "The user writes Rust on weekends and prefers tabs."
CARD = f"""---
id: U7MM
type: memory
status: active
name: weekend-rust
scope: user
pinned: true
---
# Weekend Rust

{FACT}
"""
CODEX_OFF = ["-c", "features.memories=false", "-c", "memories.generate_memories=false",
             "-c", "memories.use_memories=false"]


class _GlobalMemory(unittest.TestCase):
    """A temporary global Board with one pinned user memory, and a workspace and home beside it."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.global_root = root / "global"
        (self.global_root / "memory").mkdir(parents=True)
        (self.global_root / "memory" / "U7MM.md").write_text(CARD, encoding="utf-8")
        self.home = root / "home"
        self.home.mkdir()
        self.workspace = root / "work"
        (self.workspace / ".git").mkdir(parents=True)     # stops the project-root walk here
        self.run_dir = str(root / "run")
        patcher = mock.patch.dict(os.environ, {"RELAY_GLOBAL_SWITCHBOARD": str(self.global_root),
                                               "RELAY_KEYRING": "off"})
        patcher.start()
        self.addCleanup(patcher.stop)

    def launch(self, guest, memory=None, extra=()):
        return guest_launch.command_line(guest, self.run_dir, cwd=str(self.workspace),
                                         home=str(self.home), python="/py", extra=list(extra),
                                         memory=memory)


class SettingValues(unittest.TestCase):
    def test_modes(self):
        self.assertEqual(guest_instructions.DEFAULT_MEMORY, "relay")
        for given, want in ((None, "relay"), ("", "relay"), (" both ", "both"), ("own", "own")):
            self.assertEqual(guest_instructions.memory_mode(given), want)
        for bad in ("theirs", 1, ["relay"]):
            with self.assertRaises(ValueError):
                guest_instructions.memory_mode(bad)
        self.assertFalse(guest_instructions.own_memory("relay"))
        self.assertTrue(guest_instructions.own_memory("own"))
        self.assertTrue(guest_instructions.own_memory("both"))


class Instructions(_GlobalMemory):
    def test_relay_and_both_carry_the_global_user_memory_and_how_to_suggest(self):
        for mode in ("relay", "both"):
            with self.subTest(mode=mode):
                block = guest_instructions.memory_instructions(str(self.workspace), mode)
                self.assertTrue(block.startswith("[Relay memory]"))
                self.assertIn(FACT, block)
                self.assertIn("app_user_memory", block)
                self.assertIn('"suggest"', block)
                self.assertEqual("auto-memory is off" in block, mode == "relay")
        self.assertEqual("", guest_instructions.memory_instructions(str(self.workspace), "own"))

    def test_build_instructions_adds_the_block_only_when_asked(self):
        plain = guest_instructions.build_instructions({"enabled": False}, str(self.workspace))
        self.assertEqual(plain, guest_instructions.GUEST_INSTRUCTIONS)
        own = guest_instructions.build_instructions({"enabled": False}, str(self.workspace),
                                                    memory="own")
        self.assertEqual(own, guest_instructions.GUEST_INSTRUCTIONS)
        relay = guest_instructions.build_instructions({"enabled": False}, str(self.workspace),
                                                      memory="relay")
        self.assertTrue(relay.startswith(guest_instructions.GUEST_INSTRUCTIONS + "\n\n[Relay memory]"))
        self.assertEqual(relay.count("[Relay memory]"), 1)


class TuiClaude(_GlobalMemory):
    def settings(self, result):
        return json.loads(Path(result["settings"]).read_text(encoding="utf-8"))

    def test_relay_is_the_default_and_turns_auto_memory_off(self):
        for memory in (None, "relay"):
            with self.subTest(memory=memory):
                result = self.launch("claude", memory)
                self.assertEqual(result["memory"], "relay")
                settings = self.settings(result)
                self.assertIs(settings["autoMemoryEnabled"], False)
                self.assertIs(settings["autoDreamEnabled"], False)
                self.assertIn("hooks", settings)            # the installer's entries are still there
                argv = result["argv"]
                path = argv[argv.index("--append-system-prompt-file") + 1]
                self.assertEqual(path, result["memory_file"])
                self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
                block = Path(path).read_text(encoding="utf-8")
                self.assertIn(FACT, block)
                self.assertIn("suggest-memory", block)
                self.assertNotIn("app_user_memory", block)   # no bridge in the terminal
                self.assertEqual(argv, shlex.split(result["command"]))
                self.assertNotIn(FACT, result["command"])  # the block is never typed

    def test_both_injects_and_leaves_its_memory_on(self):
        result = self.launch("claude", "both")
        settings = self.settings(result)
        self.assertNotIn("autoMemoryEnabled", settings)
        self.assertNotIn("autoDreamEnabled", settings)
        self.assertIn("--append-system-prompt-file", result["argv"])
        self.assertIn(FACT, Path(result["memory_file"]).read_text(encoding="utf-8"))

    def test_own_adds_nothing(self):
        result = self.launch("claude", "own")
        self.assertNotIn("autoMemoryEnabled", self.settings(result))
        self.assertNotIn("--append-system-prompt-file", result["argv"])
        self.assertEqual("", result["memory_file"])

    def test_a_row_that_appends_its_own_prompt_keeps_it(self):
        for flag in guest_launch.CLAUDE_APPEND_FLAGS:
            with self.subTest(flag=flag):
                result = self.launch("claude", "relay", extra=[flag, "x", "-r", "abc"])
                self.assertEqual(result["argv"].count(flag), 1)
                other = [f for f in guest_launch.CLAUDE_APPEND_FLAGS if f != flag][0]
                self.assertNotIn(other, result["argv"])


class TuiCodex(_GlobalMemory):
    def developer(self, argv):
        words = [argv[i + 1] for i, word in enumerate(argv[:-1])
                 if word == "-c" and argv[i + 1].startswith("developer_instructions=")]
        self.assertLessEqual(len(words), 1)
        return json.loads(words[0].split("=", 1)[1]) if words else None

    def test_relay_turns_memories_off_and_points_at_the_block(self):
        result = self.launch("codex", None)
        argv = result["argv"]
        for index in range(0, len(CODEX_OFF), 2):
            at = argv.index(CODEX_OFF[index + 1])
            self.assertEqual(argv[at - 1], "-c")
        text = self.developer(argv)
        self.assertIn(result["memory_file"], text)
        self.assertIn(FACT, Path(result["memory_file"]).read_text(encoding="utf-8"))
        self.assertNotIn(FACT, result["command"])
        self.assertEqual(argv, shlex.split(result["command"]))

    def test_both_points_at_the_block_and_keeps_codex_memories(self):
        result = self.launch("codex", "both")
        self.assertNotIn("features.memories=false", result["argv"])
        self.assertIsNotNone(self.developer(result["argv"]))

    def test_own_adds_nothing(self):
        result = self.launch("codex", "own")
        self.assertNotIn("features.memories=false", result["argv"])
        self.assertIsNone(self.developer(result["argv"]))
        self.assertEqual("", result["memory_file"])

    def test_the_users_own_developer_instructions_are_kept(self):
        config = self.home / ".codex" / "config.toml"
        config.parent.mkdir()
        config.write_text('developer_instructions = "Answer in French."\n', encoding="utf-8")
        text = self.developer(self.launch("codex", "relay")["argv"])
        self.assertTrue(text.startswith("Answer in French.\n\n[Relay memory]"), text)
        # Codex's own CODEX_HOME wins over ~/.codex when no home is given, as it does for codex.
        with mock.patch.dict(os.environ, {"CODEX_HOME": str(config.parent)}):
            self.assertEqual(guest_launch.codex_user_developer_instructions(), "Answer in French.")

    def test_resume_keeps_the_subcommand_first(self):
        result = self.launch("codex", "relay", extra=["resume", "t-1"])
        self.assertEqual(result["argv"][:2], ["codex", "resume"])
        self.assertEqual(result["argv"][-1], "t-1")
        self.assertIn("memories.use_memories=false", result["argv"])


class TuiMain(_GlobalMemory):
    def run_main(self, argv):
        out = io.StringIO()
        with redirect_stdout(out):
            code = guest_launch.main(argv)
        return code, json.loads(out.getvalue())

    def test_memory_flag(self):
        code, result = self.run_main(["claude", "--runtime-dir", self.run_dir, "--cwd",
                                      str(self.workspace), "--home", str(self.home),
                                      "--memory", "own"])
        self.assertEqual((code, result["memory"], result["memory_file"]), (0, "own", ""))
        code, result = self.run_main(["codex", "--runtime-dir", self.run_dir, "--memory", "nope"])
        self.assertEqual((code, result["ok"]), (1, False))

    def test_suggest_memory_writes_a_pending_suggestion(self):
        code, result = self.run_main(["suggest-memory", "--source", "claude",
                                      "The user deploys with Nix flakes."])
        self.assertEqual((code, result["status"]), (0, "pending"))
        files = list((self.global_root / "memory" / "suggestions").glob("*.md"))
        self.assertEqual(len(files), 1)
        self.assertIn("Nix flakes", files[0].read_text(encoding="utf-8"))
        # The same fact again is not a second suggestion.
        code, again = self.run_main(["suggest-memory", "--source", "codex",
                                     "The user deploys with Nix flakes."])
        self.assertEqual(again["status"], "duplicate")

    def test_the_suggest_command_runs_as_written(self):
        """The line the terminal guest is told to run works from an unrelated cwd and env."""
        import subprocess
        line = guest_launch.suggest_command("codex", sys.executable)
        env = {k: v for k, v in os.environ.items() if k not in ("PYTHONPATH", "RELAY_GLOBAL_SWITCHBOARD")}
        done = subprocess.run(line + " " + shlex.quote("The user reads diffs in a split view."),
                              shell=True, cwd=self._tmp.name, env=env, capture_output=True,
                              text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(json.loads(done.stdout)["status"], "pending")
        self.assertEqual(len(list((self.global_root / "memory" / "suggestions").glob("*.md"))), 1)


class _Refused(Exception):
    pass


class HarnessClaude(unittest.TestCase):
    def spawned(self, own_memory):
        seen = {}

        def spawn(argv, cwd, env):
            seen.update(argv=argv, env=env)
            raise FileNotFoundError("stop here")
        harness = ClaudeHarness(spawn=spawn, own_memory=own_memory)
        harness._cwd = "/"
        argv = harness._argv(model=None, session_id="s", resume=None, fork=False,
                             permissions="bypass")
        with mock.patch.dict(os.environ, {"CLAUDE_CODE_DISABLE_AUTO_MEMORY": "0"}):
            with self.assertRaises(HarnessNotAvailable):
                harness._launch(argv)
        return seen

    def test_relay_turns_auto_memory_off_by_settings_and_environment(self):
        seen = self.spawned(False)
        argv = seen["argv"]
        self.assertEqual(json.loads(argv[argv.index("--settings") + 1]),
                         {"autoMemoryEnabled": False, "autoDreamEnabled": False,
                          "cleanupPeriodDays": 36500})
        self.assertEqual(seen["env"]["CLAUDE_CODE_DISABLE_AUTO_MEMORY"], "1")

    def test_own_memory_passes_neither(self):
        seen = self.spawned(True)
        # Only the retention (#5A37) rides `--settings`; nothing about memory does.
        argv = seen["argv"]
        self.assertEqual(json.loads(argv[argv.index("--settings") + 1]), {"cleanupPeriodDays": 36500})
        self.assertNotIn("CLAUDE_CODE_DISABLE_AUTO_MEMORY", seen["env"])

    def test_a_settings_file_of_the_callers_is_not_doubled(self):
        harness = ClaudeHarness(settings="/s.json", own_memory=False)
        argv = harness._argv(model=None, session_id="s", resume=None, fork=False,
                             permissions="bypass")
        self.assertEqual(argv.count("--settings"), 1)
        self.assertEqual(argv[argv.index("--settings") + 1], "/s.json")


class HarnessCodex(unittest.TestCase):
    def argv(self, own_memory):
        seen = {}

        def spawn(argv, cwd):
            seen["argv"] = argv
            raise OSError("stop here")
        harness = CodexHarness(codex_path=sys.executable, spawn=spawn, own_memory=own_memory)
        with self.assertRaises(HarnessNotAvailable):
            harness.start(cwd="/", board_bridge={"command": "x"})
        return seen["argv"]

    def test_relay_switches_the_app_server_memories_off(self):
        argv = self.argv(False)
        self.assertEqual(argv[2:2 + len(CODEX_OFF)], CODEX_OFF)   # after `app-server`
        self.assertIn("mcp_servers.relay_board.command=\"x\"", argv)

    def test_own_memory_leaves_them(self):
        self.assertFalse(set(CODEX_OFF[1::2]) & set(self.argv(True)))


class Provider(_GlobalMemory):
    def start(self, guest, memory):
        harness = FakeHarness([], guest=guest)
        request = {"skills": {"enabled": False}}
        if memory is not None:
            request["guest"] = {"memory": memory}
        with mock.patch.object(ghp, "make_harness", return_value=harness) as make:
            provider = ghp.start_provider("guest:" + guest, request, str(self.workspace))
        self.addCleanup(provider.close)
        return provider, harness, make

    def test_the_mode_reaches_the_harness_and_the_instructions(self):
        for guest in ("claude", "codex"):
            for memory, want in ((None, "relay"), ("relay", "relay"), ("both", "both"),
                                 ("own", "own")):
                with self.subTest(guest=guest, memory=memory):
                    provider, harness, make = self.start(guest, memory)
                    make.assert_called_once_with(guest, memory=want)
                    self.assertEqual(provider.memory, want)
                    self.assertEqual(FACT in harness.instructions, want != "own")
                    self.assertEqual("app_user_memory" in harness.instructions, want != "own")
                    self.assertEqual(harness.instructions.count("[Relay memory]"),
                                     0 if want == "own" else 1)

    def test_make_harness_hands_the_adapter_its_memory_switch(self):
        made = []

        class Adapter:
            def __init__(self, **kwargs):
                made.append(kwargs)
        with mock.patch.object(ghp, "_load_adapter", return_value=Adapter):
            ghp.make_harness("claude", memory="relay")
            ghp.make_harness("codex", memory="both")
            ghp.make_harness("codex")
        self.assertEqual(made, [{"own_memory": False}, {"own_memory": True}, {}])


class BridgeTools(unittest.TestCase):
    def test_guests_discover_app_user_memory(self):
        bridge = Bridge(available=False)
        self.addCleanup(bridge.close)
        names = {spec["name"] for spec in bridge.specs()}
        self.assertEqual(APP_ALLOW & names, APP_ALLOW)
        self.assertFalse({name for name in names if name.startswith("app_")} - APP_ALLOW)
        spec = next(s for s in bridge.specs() if s["name"] == "app_user_memory")
        self.assertIn("suggest", spec["inputSchema"]["properties"]["action"]["enum"])

    def test_a_bound_agent_offers_it_from_its_own_app_tools(self):
        from relay_core.app_tools import TOOL_SPECS
        bridge = Bridge(available=False, delegation=False)
        self.addCleanup(bridge.close)
        app = mock.Mock()
        app.tool_specs.return_value = [dict(s) for s in TOOL_SPECS]
        agent = mock.Mock(board=None, subagents=None, app=app, activity=None,
                          executor=mock.Mock(keybindings=None))
        bridge.bind(agent)
        names = {spec["name"] for spec in bridge.specs()}
        self.assertEqual(names & APP_ALLOW, APP_ALLOW)
        agent.app = None
        self.assertNotIn("app_user_memory", {spec["name"] for spec in bridge.specs()})


if __name__ == "__main__":
    unittest.main()

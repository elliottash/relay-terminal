import importlib.util
import os
import re
import stat
import tempfile
import unittest
from pathlib import Path

from relay_core import guest


# ----- What is installed, as far as these tests are concerned --------------------------------
# classify_command is a pure rule over a tokenized command line and needs nothing from the
# machine. detect_installations asks the machine, so the cases state the machine instead: a
# temporary bin directory stands in for PATH (with `claude` and `codex` stubs that answer
# --version, or with nothing at all) and a temporary home stands in for the user's, so the
# suite passes the same way on a box with the real CLIs and on one without them.

def _make_cli(directory: Path, name: str, version: str) -> None:
    script = directory / name
    script.write_text(f"#!/bin/sh\necho '{version}'\n")
    script.chmod(script.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class ClassifyCommand(unittest.TestCase):
    CASES = (
        # argv                                                            guest
        (["claude"],                                                      "claude"),
        (["/home/u/.npm-global/bin/claude", "--resume", "abc123"],        "claude"),
        (["claude-code"],                                                 "claude"),
        (["codex"],                                                       "codex"),
        (["/usr/local/bin/codex", "-m", "gpt-5"],                         "codex"),
        # The shebang install: the kernel rewrites `codex` into node + the script path.
        (["node", "/home/u/.npm-global/bin/codex"],                       "codex"),
        (["/usr/bin/nodejs", "/opt/claude/bin/claude.js"],                "claude"),
        # The npm shim: the script leaf is cli.js; the *package* directory names the guest.
        (["node", "/home/u/.npm-global/lib/node_modules/@anthropic-ai/claude-code/cli.js"], "claude"),
        (["node", "/home/u/.nvm/versions/node/v22.3.0/lib/node_modules/@anthropic-ai/claude-code/cli.js"], "claude"),
        (["node", "/home/u/.npm-global/lib/node_modules/@openai/codex/bin/codex.js"],       "codex"),
        # An unscoped package directory, immediately under a node_modules.
        (["node", "/srv/app/node_modules/codex/bin/index.js"],            "codex"),
        (["npx", "-y", "claude"],                                         "claude"),
        (["bunx", "--bun", "codex"],                                      "codex"),
        (["deno", "run", "-A", "/opt/tools/claude.mjs"],                  None),  # "run" is the target
        # Not guests.
        (["vim"],                                                         None),
        (["cloud"],                                                       None),
        (["claude-not"],                                                  None),
        (["node", "server.js"],                                           None),
        (["node", "/home/u/src/claude-utils/build.js"],                   None),  # not a package
        (["python", "-m", "claude"],                                      None),  # python is not a launcher
        # A plain directory that happens to be named after a guest is not a package: this is the
        # case the "any path component" rule got wrong (review of 51587e3).
        (["node", "/home/codex/server.js"],                               None),
        (["node", "/var/www/claude/index.js"],                            None),
        (["node", "/srv/claude-code/server.js"],                          None),
        (["node", "/opt/@anthropic-ai/other-thing/cli.js"],               None),
        ([],                                                              None),
    )

    def test_table(self):
        for argv, wanted in self.CASES:
            with self.subTest(argv=argv):
                self.assertEqual(wanted, guest.classify_command(argv))


class MirroredInCxx(unittest.TestCase):
    """One rule in two languages (26.1). `guestProgram` in src/Pane.h classifies the same argv
    the pane already has, and `guest.classify_command` classifies what the backend is handed.
    Nothing links them but this test: it reads the C++ table out of the header and fails when a
    guest, a binary name or a package name exists on one side only."""

    PANE = Path(__file__).resolve().parents[1] / "src" / "Pane.h"
    ROW = re.compile(r'\{"(?P<id>[a-z-]+)",\s*"(?P<name>[^"]+)",\s*\{(?P<binaries>[^}]*)\},\s*'
                     r'\n?\s*\{(?P<packages>[^}]*)\}\},')
    LITERAL = re.compile(r'QStringLiteral\("([^"]+)"\)')

    def cxx_table(self):
        text = self.PANE.read_text(encoding="utf-8")
        start = text.index("static const QList<GuestSpec> &guestSpecs()")
        body = text[start:text.index("return specs;", start)]
        rows = [(row["id"], row["name"], tuple(self.LITERAL.findall(row["binaries"])),
                 tuple(self.LITERAL.findall(row["packages"])))
                for row in self.ROW.finditer(body)]
        self.assertTrue(rows, "the C++ guest table could not be read out of src/Pane.h")
        return rows

    def test_the_two_tables_are_the_same(self):
        self.assertEqual([(spec.id, spec.name, spec.binaries, spec.packages) for spec in guest.GUESTS],
                         self.cxx_table())

    def test_the_launchers_are_the_same(self):
        text = self.PANE.read_text(encoding="utf-8")
        block = text[text.index("static const QSet<QString> launchers"):]
        found = tuple(self.LITERAL.findall(block[:block.index("};")]))
        self.assertEqual(guest.LAUNCHERS, found)

    def test_the_pane_classifies_argv_not_a_joined_command_line(self):
        """foregroundCommandLine() flattens the NUL-separated cmdline to spaces, so a path with a
        space in it came apart before it reached the classifier (review of 51587e3)."""
        text = self.PANE.read_text(encoding="utf-8")
        self.assertIn("setGuest(guestProgram(foregroundArgv()))", text)
        self.assertNotIn("guestProgram(foregroundCommandLine())", text)

    def test_every_hook_name_relay_installs_is_one_the_pane_handles(self):
        """The hook names are a contract between the installers and `handleGuestHook` (26.4/26.6).
        `guest_codex` writes a `notify` entry into ~/.codex/config.toml and forwards its payload as
        a `hook` named `notify`; the pane had no branch for it, so the finished Codex turn the
        Options row promises reached nothing at all."""
        from relay_core import guest_codex, guest_install
        text = self.PANE.read_text(encoding="utf-8")
        body = text[text.index("void handleGuestHook("):]
        body = body[:body.index("\n    }\n")]
        for name in guest_install.HOOK_EVENTS + (guest_codex.NOTIFY_EVENT,):
            with self.subTest(hook=name):
                self.assertIn('QStringLiteral("%s")' % name, body)

    def test_the_codex_rollout_tail_is_started_for_a_codex_pane(self):
        """Codex has no statusline and no turn-start hook: without the tail (26.6) `guest_busy` is
        never true for codex, so the composer types into a working Codex instead of queueing."""
        text = self.PANE.read_text(encoding="utf-8")
        self.assertIn("relay_core.guest_codex", text)
        self.assertIn("startGuestTail(m_guest)", text)
        self.assertIn("stopGuestTail()", text)

    def test_a_helper_relay_starts_is_told_which_pane_it_writes_to(self):
        """qputenv writes the GUI's own environment, so an inherited RELAY_GUEST_EVENT names
        whichever pane started its shell last (26.3). Helpers get this pane's spool explicitly."""
        text = self.PANE.read_text(encoding="utf-8")
        body = text[text.index("QProcessEnvironment guestHelperEnvironment()"):]
        body = body[:body.index("\n    }\n")]
        for variable in ("RELAY_RUNTIME_DIR", "RELAY_SESSION_TOKEN", "RELAY_GUEST_EVENT"):
            self.assertIn('QStringLiteral("%s")' % variable, body)
        self.assertIn("setProcessEnvironment(guestHelperEnvironment())", text)

    def test_the_backend_directory_is_appended_to_pythonpath_only_once(self):
        """startTerminal runs per pane and per shell restart and qputenv mutates Relay's own
        environment, so an unguarded append grew PYTHONPATH without bound (review of 51587e3)."""
        text = self.PANE.read_text(encoding="utf-8")
        guard = text.index('if (!pythonPath.split(QDir::listSeparator(), Qt::SkipEmptyParts).contains(backendDir))')
        append = text.index('qputenv("PYTHONPATH"', guard)
        self.assertLess(guard, append)
        self.assertEqual(1, text.count('qputenv("PYTHONPATH"'))


class Spec(unittest.TestCase):
    def test_known_guests_in_order(self):
        self.assertEqual(("claude", "codex"), guest.guest_ids())
        self.assertEqual("Claude Code", guest.spec("claude").name)
        self.assertEqual("Codex", guest.spec("codex").name)

    def test_unknown_guest_is_an_error(self):
        for bad in ("gemini", "", "Claude"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                guest.spec(bad)


class DetectInstallations(unittest.TestCase):
    def test_both_installed(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            binary, home = root / "bin", root / "home"
            binary.mkdir()
            (home / ".claude").mkdir(parents=True)
            _make_cli(binary, "claude", "2.1.156 (Claude Code)")
            _make_cli(binary, "codex", "codex-cli 0.154.0")
            found = {i.guest: i for i in guest.detect_installations(path=str(binary), home=str(home))}
        self.assertEqual((str(binary / "claude"), "2.1.156 (Claude Code)", True),
                         (found["claude"].binary, found["claude"].version, found["claude"].config_present))
        self.assertEqual((str(binary / "codex"), "codex-cli 0.154.0", False),
                         (found["codex"].binary, found["codex"].version, found["codex"].config_present))

    def test_nothing_installed_still_lists_every_guest(self):
        with tempfile.TemporaryDirectory() as root:
            found = guest.detect_installations(path=str(Path(root) / "bin"), home=root)
        self.assertEqual(["claude", "codex"], [i.guest for i in found])
        for installation in found:
            self.assertIsNone(installation.binary)
            self.assertIsNone(installation.version)
            self.assertFalse(installation.config_present)

    def test_version_on_stderr_is_found(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            (root / "bin").mkdir()
            script = root / "bin" / "claude"
            script.write_text("#!/bin/sh\necho '2.0.0 (Claude Code)' >&2\n")
            script.chmod(0o755)
            found = {i.guest: i for i in guest.detect_installations(path=str(root / "bin"), home=str(root))}
        self.assertEqual("2.0.0 (Claude Code)", found["claude"].version)
        self.assertIsNone(found["codex"].version)


class WellKnownPaths(unittest.TestCase):
    def test_paths(self):
        self.assertEqual("/h/.claude/ide", guest.claude_ide_lock_dir("/h"))
        self.assertEqual("/h/.claude/projects", guest.claude_projects_dir("/h"))
        self.assertEqual("/h/.codex/sessions", guest.codex_sessions_dir("/h"))
        self.assertEqual("/h/.claude", guest.config_dir("claude", "/h"))

    def test_codex_state_db_picks_the_highest_schema(self):
        with tempfile.TemporaryDirectory() as root:
            codex = Path(root) / ".codex"
            codex.mkdir()
            self.assertIsNone(guest.codex_state_db(root))
            for name in ("state_3.sqlite", "state_5.sqlite", "state_notes.sqlite"):
                (codex / name).touch()
            self.assertEqual(str(codex / "state_5.sqlite"), guest.codex_state_db(root))


class BridgeEnv(unittest.TestCase):
    def test_claude_gets_the_bridge_variables(self):
        self.assertEqual({"CLAUDE_CODE_SSE_PORT": "45678", "ENABLE_IDE_INTEGRATION": "true"},
                         guest.bridge_env("claude", 45678))

    def test_codex_has_no_bridge(self):
        self.assertEqual({}, guest.bridge_env("codex", 45678))

    def test_bad_port_and_guest_are_errors(self):
        for port in (0, 65536, -1, "45678", None):
            with self.subTest(port=port), self.assertRaises(ValueError):
                guest.bridge_env("claude", port)
        with self.assertRaises(ValueError):
            guest.bridge_env("gemini", 45678)


class OneChannelInThreeLanguages(unittest.TestCase):
    """The guest event channel (26.3) is spelled out in five places and linked by nothing but
    this test.

    Three Python shims name `shell/guest-event.py` themselves — `relay_core.guest_hook`,
    `relay_core.guest_codex` and `relay_core.guest_slash` — and each has its own copy of the
    path. That is not an oversight to be tidied away: the hook shim and the codex notify entry
    are both run **by absolute path with no PYTHONPATH** (see `guest_install.RUN` and
    `guest_codex.notify_command`), so neither may import anything from `relay_core`, and a
    shared constant would turn every hook into an ImportError. What the copies must do is
    agree, and until 2026-09-19 nothing said so: the three landed from two branches on the same
    day, two of them spelling the path with `os.path` and one with `pathlib`.

    The same goes across the language line. `src/Pane.h` reads the spool, names the two
    directories and dispatches the five event names; `shell/guest-event.py` and
    `guest_hook.ANSWERS_DIR` name the directories on the Python side; `remote.wire`
    classifies the five names so no worker event can be pre-classified by accident. A name
    added on one side only is a dead branch or a lost event, and it is silent.
    """

    PANE = Path(__file__).resolve().parents[1] / "src" / "Pane.h"
    WRITER = Path(__file__).resolve().parents[1] / "shell" / "guest-event.py"

    def writer_module(self):
        spec = importlib.util.spec_from_file_location("relay_guest_event_under_test", self.WRITER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def pane_text(self):
        return self.PANE.read_text(encoding="utf-8")

    def test_the_three_shims_name_the_same_writer(self):
        from relay_core import guest_codex, guest_hook, guest_slash
        wanted = os.path.realpath(str(self.WRITER))
        for module in (guest_hook, guest_codex, guest_slash):
            with self.subTest(module=module.__name__):
                self.assertEqual(wanted, os.path.realpath(str(module.WRITER)))

    def test_the_writer_is_there_and_runnable(self):
        """Two of the three shims exec it as `[writer, event, guest]` with no interpreter
        (`guest_slash.emit`), so the executable bit is part of the contract, not packaging."""
        self.assertTrue(self.WRITER.is_file(), self.WRITER)
        self.assertTrue(os.access(self.WRITER, os.X_OK), f"{self.WRITER} is not executable")

    def test_the_two_directory_names_are_the_same_in_cxx_and_python(self):
        text = self.pane_text()
        names = re.findall(r'guest(?:Events|Answers)DirName\(\) \{ return QStringLiteral\("([a-z-]+)"\)', text)
        self.assertEqual(["guest-events", "guest-answers"], names,
                         "src/Pane.h no longer names the spool and answer directories where this "
                         "test reads them")
        self.assertEqual("guest-events", self.writer_module().EVENTS_DIR_NAME)
        from relay_core import guest_hook
        self.assertEqual("guest-answers", guest_hook.ANSWERS_DIR)

    def test_the_pane_handles_exactly_the_classified_channel_events(self):
        from remote import wire
        body = self.pane_text()
        body = body[body.index("void handleGuestEvent("):]
        body = body[:body.index("\n    void setGuestBusy")]
        branches = re.findall(r'name == QStringLiteral\("([a-z_]+)"\)', body)
        self.assertTrue(branches, "handleGuestEvent's branches could not be read out of src/Pane.h")
        self.assertEqual(sorted(wire.GUEST_CHANNEL_EVENTS), sorted(branches),
                         "src/Pane.h and remote.wire.GUEST_CHANNEL_EVENTS disagree about the "
                         "guest channel's events")
        self.assertEqual(len(branches), len(set(branches)))


if __name__ == "__main__":
    unittest.main()

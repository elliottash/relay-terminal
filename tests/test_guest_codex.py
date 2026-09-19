# SPDX-License-Identifier: GPL-3.0-or-later
"""Codex as a guest: the marked settings writes and the rollout tail (protocol 26.6).

Two promises are held here. The settings half: enabling and then disabling Guests on a
`config.toml` returns that file byte for byte, whatever it holds — comments, nested tables,
multi-line arrays, a user's own `notify` that Relay must refuse to take, a user's own
`notification_condition` that Relay must replace and put back. The rollout half: a tail that
reads only what was appended, reports only what changed, and says nothing at all before it has a
session to speak about.

The rollouts are copies of a real Codex session, trimmed to the records the tail reads, kept
under a realistic `sessions/YYYY/MM/DD/` layout and with mtimes the tests choose (see
`tests/fixtures/codex/README.md`). Nothing here reads the live `~/.codex`: the tests state the
machine instead, exactly as `tests/test_guest.py` does.

The channel is exercised against the real `shell/guest-event.py`, the one writer of protocol
26.3: a real child process, a real spool file, a real token check. (This phase carried a fixture
copy of the helper while the hooks branch was unmerged; the helper is in the tree now, so the
copy is gone.)
"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from relay_core import guest_codex

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "codex"
SESSIONS = FIXTURES / "sessions"
REAL_CONFIG = FIXTURES / "config.toml"                  # copied from a real ~/.codex/config.toml
COMMENTED_CONFIG = FIXTURES / "config-with-comments.toml"
HELPER = ROOT / "shell" / "guest-event.py"              # the channel's one writer (26.3)
# What the pane exports as RELAY_GUEST_EVENT: the spool *directory*, which is only what says there
# is a pane at all. The writer's own path comes from RELAY_GUEST_WRITER, which is how a test points
# at this checkout's helper (`guest_codex.helper_path`).
SPOOL = "/run/relay/pane-1/guest-events"

# The workspace the fixture session was started in, and the three sessions under `SESSIONS`.
WORKSPACE = "/home/elliott/repos/relay-terminal"
OTHER_WORKSPACE = "/home/elliott/repos/sweet-street"
TURN = "2026/09/18/rollout-2026-09-18T22-36-30-01a0b785-bca3-7b22-aa25-4c4c444a276d.jsonl"
BUSY = "2026/09/19/rollout-2026-09-19T00-10-00-01a0b7c1-aaaa-7bbb-8ccc-ddddeeeeffff.jsonl"
OTHER = "2026/09/19/rollout-2026-09-19T01-20-00-01a0b7c2-bbbb-7ccc-8ddd-eeeeffff0000.jsonl"

TURN_ID = "01a0b785-c2a6-7701-85b2-da50e915c0ae"
THREAD_ID = "01a0b785-bca3-7b22-aa25-4c4c444a276d"
BUSY_THREAD_ID = "01a0b7c1-aaaa-7bbb-8ccc-ddddeeeeffff"
OTHER_THREAD_ID = "01a0b7c2-bbbb-7ccc-8ddd-eeeeffff0000"
# The last request's usage block, exactly as the fixture's rollout reports it, and the context
# window for the turn: together they are the occupancy the pane's chip shows.
USAGE = {"input_tokens": 14685, "cached_input_tokens": 12032, "cache_write_input_tokens": 0,
         "output_tokens": 41, "reasoning_output_tokens": 0, "total_tokens": 14726}
WINDOW = 258400
LAST_REQUEST = USAGE["input_tokens"]                   # cached part included: the live context
LAST_REQUEST_PCT = round(LAST_REQUEST / WINDOW * 100)  # 6

PYTHON = sys.executable                                # deterministic argv for the settings tests
SCRIPT = os.path.abspath(guest_codex.__file__)         # a path that really exists


def fixture(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def enable(text: str, python: str = PYTHON, script: str = SCRIPT) -> str:
    return guest_codex.render_enable(text, python, script)


def notify_lines(python: str = PYTHON, script: str = SCRIPT) -> list[str]:
    return [python, "-S", script, "notify"]


def events_of(pairs) -> dict:
    return {name: data for name, data in pairs}


def pane_env(**extra) -> dict:
    """A pane's environment as far as the channel is concerned: a spool directory to say there is a
    pane, and this checkout's writer. Nothing is run with it unless a test passes a real runner."""
    return {"RELAY_GUEST_EVENT": SPOOL, "RELAY_GUEST_WRITER": str(HELPER), **extra}


@contextlib.contextmanager
def pane_environment(runtime: str, token: str = "tok-1"):
    """A pane's environment, in this process: the helper is a real child and inherits it."""
    keys = ("RELAY_GUEST_EVENT", "RELAY_GUEST_WRITER", "RELAY_RUNTIME_DIR", "RELAY_SESSION_TOKEN",
            "RELAY_PYTHON")
    saved = {key: os.environ.get(key) for key in keys}
    os.environ["RELAY_GUEST_EVENT"] = os.path.join(runtime, "guest-events")
    os.environ["RELAY_GUEST_WRITER"] = str(HELPER)
    os.environ["RELAY_RUNTIME_DIR"] = runtime
    os.environ["RELAY_SESSION_TOKEN"] = token
    os.environ.pop("RELAY_PYTHON", None)
    try:
        yield
    finally:
        for key, value in saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def spool(runtime: str) -> list[dict]:
    """Every envelope on the pane's event spool, oldest first (26.3). The channel is a directory
    of one file per event since the review of 51587e3; nothing deletes them here, so a test that
    emits twice sees both."""
    events = os.path.join(runtime, "guest-events")
    names = sorted(name for name in os.listdir(events) if name.endswith(".json"))
    envelopes = []
    for name in names:
        with open(os.path.join(events, name), encoding="utf-8") as handle:
            envelopes.append(json.load(handle))
    return envelopes


def envelope(runtime: str) -> dict:
    """The last event written to the spool — what the pane would handle last."""
    return spool(runtime)[-1]


class Recorder:
    """A stand-in for `subprocess.run` that remembers its call instead of running it."""

    def __init__(self, error: Exception | None = None):
        self.error = error
        self.argv: list[str] | None = None
        self.kwargs: dict = {}
        self.calls = 0

    def __call__(self, argv, **kwargs):
        self.calls += 1
        self.argv, self.kwargs = list(argv), kwargs
        if self.error:
            raise self.error
        return subprocess.CompletedProcess(argv, 0, "", "")


class SessionsDir:
    """A temporary sessions directory of fixture copies, with mtimes the test decides."""

    def __init__(self):
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)

    def __enter__(self) -> "SessionsDir":
        return self

    def __exit__(self, *_):
        self._temp.cleanup()

    def add(self, relative: str, when: float = 0.0, source: Path | None = None) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((source or SESSIONS / relative).read_bytes())
        os.utime(path, (when, when))
        return path

    def write(self, relative: str, text: str, when: float = 0.0) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        os.utime(path, (when, when))
        return path

    def append(self, path: Path, text: str) -> None:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(text)

    def tail(self, cwd: str | None = WORKSPACE, **kwargs) -> guest_codex.RolloutTail:
        return guest_codex.RolloutTail(sessions_dir=str(self.root), cwd=cwd, **kwargs)


# ----- the TOML document model ---------------------------------------------------------------


class TomlDocument(unittest.TestCase):
    def test_a_real_config_round_trips_byte_for_byte(self):
        for path in (REAL_CONFIG, COMMENTED_CONFIG):
            with self.subTest(path=path.name):
                text = fixture(path)
                self.assertEqual(text, guest_codex.Document.parse(text).text())

    def test_every_line_knows_the_table_it_is_in(self):
        document = guest_codex.Document.parse(fixture(COMMENTED_CONFIG))
        entries = {(line.table, line.key) for line in document.lines if line.kind == "entry"}
        # A root key, a key in a quoted-path table, a key in a plain table, a dotted root key.
        self.assertIn(((), "model"), entries)
        self.assertIn((("projects", WORKSPACE), "trust_level"), entries)
        self.assertIn((("tui",), "notification_condition"), entries)
        self.assertIn(((), "shell_environment_policy.include_only"), entries)
        # `[tui.model_availability_nux]` is a table of its own, not a key in `[tui]`.
        self.assertIn((("tui", "model_availability_nux"), "gpt-6-astra"), entries)

    def test_a_multi_line_array_is_one_entry_whose_value_is_its_list(self):
        document = guest_codex.Document.parse(fixture(COMMENTED_CONFIG))
        (index,) = document.entries((), "shell_environment_policy.include_only")
        self.assertEqual(5, len(document.lines[index].text.splitlines()))
        self.assertEqual(["PATH", "HOME", "LANG"], document.value_of(index))

    def test_a_hash_inside_a_string_is_not_a_comment(self):
        document = guest_codex.Document.parse(fixture(COMMENTED_CONFIG))
        (index,) = document.entries(("marketplaces", "codex-warp"), "source")
        self.assertEqual("https://github.com/warpdotdev/codex-warp.git",
                         document.value_of(index))

    def test_a_comment_after_a_header_does_not_hide_it(self):
        document = guest_codex.Document.parse("[tui]  # the table\n")
        self.assertEqual([("header", ("tui",))],
                         [(line.kind, line.table) for line in document.lines])

    def test_an_array_of_tables_is_a_header(self):
        document = guest_codex.Document.parse('[[mcp_servers]]\nname = "x"\n')
        self.assertEqual(("header", ("mcp_servers",)),
                         (document.lines[0].kind, document.lines[0].table))

    def test_a_multi_line_string_is_one_entry(self):
        document = guest_codex.Document.parse(
            'instructions = """\nline one\nline two"""\nmodel = 1\n')
        self.assertEqual(2, len(document.lines))
        self.assertEqual(("entry", "instructions"),
                         (document.lines[0].kind, document.lines[0].key))
        self.assertEqual("line one\nline two", document.value_of(0))

    def test_text_that_is_not_a_key_or_a_header_is_kept_as_text(self):
        text = "# just a comment\n\n   \n"
        document = guest_codex.Document.parse(text)
        self.assertEqual(["text"] * 3, [line.kind for line in document.lines])
        self.assertEqual(text, document.text())

    def test_an_empty_file_and_a_missing_newline_both_round_trip(self):
        self.assertEqual("", guest_codex.Document.parse("").text())
        self.assertEqual('model = "x"', guest_codex.Document.parse('model = "x"').text())

    def test_index_and_end_of_table(self):
        document = guest_codex.Document.parse(fixture(COMMENTED_CONFIG))
        position = document.index_of_table(("tui",))
        self.assertIsNotNone(position)
        self.assertEqual("header", document.lines[position].kind)
        # `[tui]` ends where `[tui.model_availability_nux]` begins.
        self.assertEqual(("tui", "model_availability_nux"),
                         document.lines[document.end_of_table(position)].table)
        self.assertIsNone(document.index_of_table(("nope",)))

    def test_split_key_unquotes_and_splits(self):
        self.assertEqual(("projects", "/home/u/x"), guest_codex.split_key('projects."/home/u/x"'))
        self.assertEqual(("a.b", "c"), guest_codex.split_key('"a.b" . c'))
        self.assertEqual(("model",), guest_codex.split_key("model"))
        self.assertEqual((), guest_codex.split_key(""))

    def test_a_value_toml_cannot_read_is_unreadable_not_a_guess(self):
        document = guest_codex.Document.parse("model = ?\n")
        self.assertIs(guest_codex._UNREADABLE, document.value_of(0))


# ----- enabling and disabling -----------------------------------------------------------------


class EnableDisable(unittest.TestCase):
    def test_enable_then_disable_is_the_same_bytes(self):
        for path in (REAL_CONFIG, COMMENTED_CONFIG):
            with self.subTest(path=path.name):
                text = fixture(path)
                self.assertEqual(text, guest_codex.render_disable(enable(text)))

    def test_an_empty_file_goes_back_to_empty(self):
        self.assertEqual("", guest_codex.render_disable(enable("")))
        self.assertEqual("", guest_codex.render_disable(""))

    def test_enabling_twice_is_enabling_once(self):
        once = enable(fixture(COMMENTED_CONFIG))
        self.assertEqual(once, enable(once))

    def test_a_moved_interpreter_is_refreshed_in_place(self):
        text = fixture(REAL_CONFIG)
        first = enable(text, python="/usr/bin/python3.11")
        second = enable(first, python="/usr/bin/python3.12")
        self.assertNotIn("3.11", second)
        self.assertEqual(1, second.count(f"# {guest_codex.MARKER}: notify"))
        self.assertEqual(1, second.count("notify = ["))
        self.assertEqual(text, guest_codex.render_disable(second))

    def test_the_notify_entry_is_a_root_key_before_the_first_table(self):
        rendered = enable(fixture(COMMENTED_CONFIG))
        lines = rendered.splitlines()
        notify = next(index for index, line in enumerate(lines) if line.startswith("notify = ["))
        first_header = next(index for index, line in enumerate(lines) if line.startswith("["))
        self.assertLess(notify, first_header)
        self.assertEqual(notify_lines(), guest_codex._toml_load(rendered)["notify"])

    def test_the_result_is_valid_toml_and_means_what_relay_needs(self):
        for path in (REAL_CONFIG, COMMENTED_CONFIG):
            with self.subTest(path=path.name):
                data = guest_codex._toml_load(enable(fixture(path)))
                self.assertEqual(notify_lines(), data["notify"])
                self.assertEqual("always", data["tui"]["notification_condition"])

    def test_a_tui_table_is_created_when_there_is_none_and_removed_again(self):
        # The real config has `[tui.model_availability_nux]` but no `[tui]` of its own.
        for text in ('model = "gpt-5.6-sol"\n', fixture(REAL_CONFIG)):
            with self.subTest(text=text[:20]):
                rendered = enable(text)
                self.assertIn(f"[tui]  # {guest_codex.MARKER}", rendered)
                self.assertIn(f'notification_condition = "always"  # {guest_codex.MARKER}',
                              rendered)
                self.assertEqual(text, guest_codex.render_disable(rendered))

    def test_a_users_other_tui_keys_survive(self):
        data = guest_codex._toml_load(enable(fixture(COMMENTED_CONFIG)))
        self.assertIs(False, data["tui"]["notifications"])
        self.assertEqual(4, data["tui"]["model_availability_nux"]["gpt-6-astra"])
        self.assertEqual("save-all", data["history"]["persistence"])

    def test_a_users_own_notify_is_never_taken(self):
        text = 'notify = ["/usr/bin/say", "done"]\nmodel = "x"\n'
        with self.assertRaises(guest_codex.SettingsConflict) as caught:
            enable(text)
        self.assertEqual(("notify",), caught.exception.keys)
        self.assertIsInstance(caught.exception, guest_codex.SettingsError)
        self.assertIn("notify", str(caught.exception))

    def test_a_users_own_notify_that_already_matches_is_left_alone(self):
        text = f"notify = {json.dumps(notify_lines())}\n"
        rendered = enable(text)
        self.assertEqual(1, rendered.count("notify = ["))
        self.assertNotIn(f"# {guest_codex.MARKER}: notify", rendered)
        self.assertEqual(text, guest_codex.render_disable(rendered))

    def test_a_users_notification_condition_is_replaced_and_put_back(self):
        text = fixture(COMMENTED_CONFIG)
        rendered = enable(text)
        self.assertIn(f'# {guest_codex.MARKER}: restore notification_condition = "unfocused"',
                      rendered)
        # The user's own comment on their own line stays where they put it.
        self.assertIn("# the default: notify only when unfocused", rendered)
        self.assertEqual("always",
                         guest_codex._toml_load(rendered)["tui"]["notification_condition"])
        self.assertEqual(text, guest_codex.render_disable(rendered))

    def test_a_users_notification_condition_that_matches_is_left_alone(self):
        text = 'model = "x"\n\n[tui]\nnotification_condition = "always"\n'
        rendered = enable(text)
        self.assertNotIn(f"# {guest_codex.MARKER}: restore", rendered)
        self.assertEqual(text, guest_codex.render_disable(rendered))

    def test_refreshing_keeps_the_users_place_and_their_restore_comment(self):
        text = fixture(COMMENTED_CONFIG)
        twice = enable(enable(text), python="/usr/bin/python3.12")
        self.assertEqual(1, twice.count(f"# {guest_codex.MARKER}: restore"))
        self.assertEqual(text, guest_codex.render_disable(twice))

    def test_a_value_relay_cannot_replace_on_one_line_is_refused_not_corrupted(self):
        # `notification_condition` with its value on the next line: replacing the entry would
        # leave that value behind as a stray line, so Relay refuses instead of writing it.
        with self.assertRaises(guest_codex.SettingsConflict) as caught:
            enable('[tui]\nnotification_condition =\n  "unfocused"\n')
        self.assertEqual(("tui.notification_condition",), caught.exception.keys)
        # A value spread over several lines is the same case: the whole entry would have to go.
        with self.assertRaises(guest_codex.SettingsConflict):
            enable('[tui]\nnotification_condition = [\n  "unfocused",\n]\n')

    def test_disabling_a_file_with_nothing_of_ours_changes_nothing(self):
        text = fixture(COMMENTED_CONFIG)
        self.assertEqual(text, guest_codex.render_disable(text))

    def test_a_file_without_a_final_newline_gets_one(self):
        # The one normalisation Relay makes: Codex always writes a newline, and an entry cannot
        # be appended to a line that has none.
        text = 'model = "x"'
        rendered = enable(text)
        self.assertTrue(rendered.endswith("\n"))
        self.assertEqual(text + "\n", guest_codex.render_disable(rendered))

    def test_our_own_entry_is_recognized_by_its_script_and_subcommand(self):
        self.assertTrue(guest_codex._is_our_notify(notify_lines()))
        # Deliberately not an equality check against `notify_command()`: the interpreter named
        # at enable time is whatever the GUI found then, and an entry whose path moved between
        # releases is still Relay's — it must not read back as "off".
        self.assertTrue(guest_codex._is_our_notify(["/other/python3", "-S",
                                                    "/moved/guest_codex.py", "notify"]))
        self.assertFalse(guest_codex._is_our_notify(["/usr/bin/say", "done"]))
        self.assertFalse(guest_codex._is_our_notify(["/other/python3", "-S",
                                                     "/other/notifier.py", "notify"]))
        self.assertFalse(guest_codex._is_our_notify(["python3", "-S", SCRIPT]))
        self.assertFalse(guest_codex._is_our_notify("notify"))
        self.assertFalse(guest_codex._is_our_notify(None))

    def test_the_restore_comment_carries_the_users_line_verbatim(self):
        line = guest_codex._restore_comment('notification_condition = "unfocused"  # mine\n')
        self.assertEqual('notification_condition = "unfocused"  # mine\n',
                         guest_codex._restore_text(line))
        self.assertIsNone(guest_codex._restore_text("# just a comment\n"))
        self.assertIsNone(guest_codex._restore_text('notification_condition = "x"\n'))


# ----- the settings on disk -------------------------------------------------------------------


class SettingsOnDisk(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.home = Path(self._temp.name)

    def tearDown(self):
        self._temp.cleanup()

    @property
    def config(self) -> Path:
        return Path(guest_codex.settings_path(str(self.home)))

    def write_config(self, text: str) -> None:
        self.config.parent.mkdir(parents=True, exist_ok=True)
        self.config.write_text(text, encoding="utf-8")

    def enable(self) -> dict:
        return guest_codex.enable(home=str(self.home), python=PYTHON, script=SCRIPT)

    def test_enable_creates_a_config_and_disable_takes_it_away_again(self):
        self.assertFalse(self.config.exists())
        state = self.enable()
        self.assertTrue(state["changed"])
        self.assertTrue(state["enabled"])
        self.assertEqual(["config.toml"], [path.name for path in self.config.parent.iterdir()])
        state = guest_codex.disable(home=str(self.home))
        self.assertTrue(state["changed"])
        self.assertTrue(state["removed"])
        self.assertFalse(self.config.exists())
        self.assertFalse(guest_codex.disable(home=str(self.home))["changed"])

    def test_enable_leaves_a_users_file_exactly_as_it_was_after_disable(self):
        text = fixture(COMMENTED_CONFIG)
        self.write_config(text)
        self.assertTrue(self.enable()["changed"])
        self.assertNotEqual(text, self.config.read_text(encoding="utf-8"))
        state = guest_codex.disable(home=str(self.home))
        self.assertTrue(state["changed"])
        self.assertFalse(state["removed"])
        self.assertEqual(text, self.config.read_text(encoding="utf-8"))

    def test_the_file_mode_is_kept(self):
        self.write_config('model = "x"\n')
        os.chmod(self.config, 0o600)
        self.enable()
        self.assertEqual(0o600, os.stat(self.config).st_mode & 0o777)

    def test_a_config_relay_created_gets_the_mode_codex_uses(self):
        self.enable()
        self.assertEqual(0o600, os.stat(self.config).st_mode & 0o777)

    def test_a_second_enable_writes_nothing(self):
        self.write_config('model = "x"\n')
        self.enable()
        before = os.stat(self.config).st_ino
        self.assertFalse(self.enable()["changed"])
        self.assertEqual(before, os.stat(self.config).st_ino)

    def test_settings_state_on_a_missing_file_says_so(self):
        state = guest_codex.settings_state(home=str(self.home))
        self.assertEqual({"path": str(self.config), "exists": False},
                         {key: state[key] for key in ("path", "exists")})
        self.assertFalse(state["enabled"])
        self.assertEqual(["Codex has no config.toml yet."], state["notes"])

    def test_settings_state_reports_our_entries_and_their_marks(self):
        self.write_config(fixture(REAL_CONFIG))
        self.assertFalse(guest_codex.settings_state(home=str(self.home))["enabled"])
        self.enable()
        state = guest_codex.settings_state(home=str(self.home))
        self.assertTrue(state["enabled"])
        self.assertEqual({guest_codex.NOTIFY_KEY: True,
                          guest_codex.NOTIFICATION_CONDITION: True}, state["marked"])
        self.assertEqual(notify_lines(), state[guest_codex.NOTIFY_KEY])
        self.assertEqual("always", state[guest_codex.NOTIFICATION_CONDITION])
        self.assertEqual([], state["notes"])

    def test_settings_state_notes_a_users_own_notify(self):
        self.write_config('notify = ["/usr/bin/say", "done"]\n')
        state = guest_codex.settings_state(home=str(self.home))
        self.assertFalse(state["enabled"])
        self.assertEqual(["Your own notify command is set; Relay will not replace it."],
                         state["notes"])

    def test_settings_state_notes_a_notify_program_that_is_gone(self):
        command = ["/usr/bin/python3", "-S", "/gone/guest_codex.py", "notify"]
        self.write_config(f"notify = {json.dumps(command)}\n"
                          'model = "x"\n\n[tui]\nnotification_condition = "always"\n')
        state = guest_codex.settings_state(home=str(self.home))
        self.assertTrue(state["enabled"])
        self.assertIn("Relay's notify program is missing (/gone/guest_codex.py)",
                      state["notes"][0])

    def test_settings_state_notes_codex_own_notifications_being_off(self):
        self.write_config(fixture(COMMENTED_CONFIG))
        self.enable()
        self.assertIn("Codex's own desktop notifications are off",
                      guest_codex.settings_state(home=str(self.home))["notes"][0])

    def test_settings_state_on_a_file_that_is_not_toml_says_so(self):
        self.write_config("this is not = = toml\n")
        state = guest_codex.settings_state(home=str(self.home))
        self.assertFalse(state["enabled"])
        self.assertTrue(state["notes"][0].startswith("config.toml is not valid TOML"))

    def test_a_config_too_large_to_edit_is_refused(self):
        self.write_config("x" * (guest_codex.MAX_CONFIG_BYTES + 1))
        with self.assertRaises(guest_codex.SettingsError):
            self.enable()
        with self.assertRaises(guest_codex.SettingsError):
            guest_codex.disable(home=str(self.home))
        self.assertIn("too large", guest_codex.settings_state(home=str(self.home))["notes"][0])

    def test_no_temporary_or_backup_files_are_left_behind(self):
        self.write_config(fixture(REAL_CONFIG))
        self.enable()
        guest_codex.disable(home=str(self.home))
        self.assertEqual(["config.toml"],
                         sorted(path.name for path in self.config.parent.iterdir()))

    def test_the_notify_command_names_an_absolute_interpreter_and_this_file(self):
        self.assertEqual([PYTHON, "-S", SCRIPT, "notify"],
                         guest_codex.notify_command(PYTHON, SCRIPT))
        self.assertTrue(os.path.isabs(guest_codex.notify_command()[0]))
        self.assertTrue(os.path.isabs(guest_codex.notify_command()[2]))


# ----- which rollout belongs to the pane -------------------------------------------------------


class RolloutSelection(unittest.TestCase):
    def test_the_thread_id_comes_from_the_file_name(self):
        self.assertEqual(THREAD_ID, guest_codex.rollout_thread_id(
            f"rollout-2026-09-18T22-36-30-{THREAD_ID}.jsonl"))
        for name in ("rollout-2026-09-18.jsonl", "session.jsonl", "rollout-abc.jsonl",
                     f"rollout-{THREAD_ID}.jsonl.bak", ""):
            with self.subTest(name=name):
                self.assertIsNone(guest_codex.rollout_thread_id(name))

    def test_the_panes_workspace_decides_which_rollout(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            sessions.add(BUSY, when=2000)
            sessions.add(OTHER, when=3000)          # newest, but another workspace
            mine = guest_codex.newest_rollout(str(sessions.root), WORKSPACE)
            theirs = guest_codex.newest_rollout(str(sessions.root), OTHER_WORKSPACE)
        self.assertEqual(BUSY_THREAD_ID, mine.thread_id)     # the newest session in this pane
        self.assertEqual(OTHER_THREAD_ID, theirs.thread_id)

    def test_without_a_workspace_the_newest_rollout_wins(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            sessions.add(OTHER, when=3000)
            found = guest_codex.newest_rollout(str(sessions.root))
        self.assertEqual(OTHER_THREAD_ID, found.thread_id)
        self.assertEqual(OTHER_WORKSPACE, found.cwd)

    def test_the_workspace_comes_from_the_session_meta_record(self):
        with SessionsDir() as sessions:
            path = sessions.add(TURN, when=1000)
            self.assertEqual(WORKSPACE, guest_codex._rollout_cwd(str(path)))
            self.assertEqual(WORKSPACE, guest_codex.newest_rollout(str(sessions.root)).cwd)

    def test_a_rollout_with_no_session_meta_has_no_workspace(self):
        with SessionsDir() as sessions:
            line = json.dumps({"type": "event_msg", "payload": {"type": "task_started"}})
            sessions.write(f"2026/09/19/rollout-2026-09-19T02-00-00-{OTHER_THREAD_ID}.jsonl",
                           "\n".join([line] * 25) + "\n", when=3000)
            found = guest_codex.newest_rollout(str(sessions.root))
        self.assertIsNone(found.cwd)

    def test_no_sessions_directory_is_no_rollout(self):
        with SessionsDir() as sessions:
            self.assertIsNone(guest_codex.newest_rollout(str(sessions.root)))
            self.assertIsNone(guest_codex.newest_rollout(str(sessions.root / "nope")))
        self.assertIsNone(guest_codex.newest_rollout(None, None, home="/nonexistent-home"))

    def test_a_flat_sessions_layout_is_found(self):
        with SessionsDir() as sessions:
            sessions.add(TURN.split("/", 3)[-1], when=1000, source=SESSIONS / TURN)
            found = guest_codex.newest_rollout(str(sessions.root), WORKSPACE)
        self.assertEqual(THREAD_ID, found.thread_id)

    def test_the_default_sessions_directory_comes_from_the_guest_registry(self):
        with tempfile.TemporaryDirectory() as home:
            directory = Path(home) / ".codex" / "sessions" / "2026" / "09" / "18"
            directory.mkdir(parents=True)
            (directory / Path(TURN).name).write_bytes((SESSIONS / TURN).read_bytes())
            found = guest_codex.newest_rollout(None, WORKSPACE, home=home)
        self.assertEqual(THREAD_ID, found.thread_id)


# ----- what the tail knows ---------------------------------------------------------------------


class TailStateFacts(unittest.TestCase):
    def state(self, **fields) -> guest_codex.TailState:
        return guest_codex.TailState(**fields)

    def test_context_occupancy_is_the_last_request_over_the_reported_window(self):
        state = self.state(context_window=WINDOW, last_request={"input_tokens": LAST_REQUEST})
        self.assertEqual(LAST_REQUEST_PCT, state.context_pct())
        state.last_request = {"input_tokens": WINDOW}
        self.assertEqual(100, state.context_pct())
        state.last_request = {"input_tokens": WINDOW * 2}
        self.assertEqual(100, state.context_pct())          # never above 100
        state.last_request = {"input_tokens": -5}
        self.assertIsNone(state.context_pct())

    def test_context_occupancy_is_unknown_until_both_numbers_are_known(self):
        self.assertIsNone(self.state().context_pct())
        self.assertIsNone(self.state(context_window=WINDOW).context_pct())
        self.assertIsNone(self.state(last_request={"input_tokens": 10}).context_pct())
        self.assertIsNone(self.state(context_window=WINDOW,
                                     last_request={"output_tokens": 10}).context_pct())

    def test_idle_time_counts_from_when_the_rollout_was_last_read(self):
        self.assertEqual(120, self.state(updated=1000.0).idle_seconds(now=1120.5))
        self.assertEqual(0, self.state(updated=1000.0).idle_seconds(now=900.0))
        self.assertEqual(0, self.state().idle_seconds(now=1120.5))

    def test_the_state_event_carries_busy_and_the_open_turn_only(self):
        self.assertEqual({"busy": False}, guest_codex.state_data(self.state()))
        self.assertEqual({"busy": True, "turn": TURN_ID},
                         guest_codex.state_data(self.state(busy=True, turn=TURN_ID)))

    def test_the_statusline_data_omits_what_codex_did_not_report(self):
        data = guest_codex.statusline_data(self.state(thread_id=THREAD_ID), now=10.0)
        self.assertEqual({"thread_id": THREAD_ID, "idle_seconds": 0, "stale": False}, data)

    def test_the_statusline_data_carries_the_numbers_the_chips_read(self):
        state = self.state(thread_id=THREAD_ID, cwd=WORKSPACE, model="gpt-5.6-sol",
                           context_window=WINDOW, last_request={"input_tokens": LAST_REQUEST},
                           turn_usage={"total_tokens": 14726},
                           thread_usage={"total_tokens": 14726}, updated=1000.0)
        self.assertEqual({"thread_id": THREAD_ID, "cwd": WORKSPACE, "model": "gpt-5.6-sol",
                          "context_pct": LAST_REQUEST_PCT, "context_window": WINDOW,
                          "last_request_tokens": {"input_tokens": LAST_REQUEST},
                          "turn_tokens": {"total_tokens": 14726},
                          "thread_tokens": {"total_tokens": 14726},
                          "idle_seconds": 5, "stale": False},
                         guest_codex.statusline_data(state, now=1005.0, stale_after=60))
        self.assertIs(True, guest_codex.statusline_data(state, now=1100.0, stale_after=60)["stale"])

    def test_only_the_counts_the_rollout_reported_are_kept(self):
        counts = guest_codex._tokens({"input_tokens": 5, "output_tokens": None,
                                      "total_tokens": True, "extra": 1})
        self.assertEqual({"input_tokens": 5}, counts)
        self.assertIsNone(guest_codex._tokens(None))
        self.assertIsNone(guest_codex._tokens({}))


# ----- following the rollout -------------------------------------------------------------------


class RolloutTailTests(unittest.TestCase):
    def test_a_finished_turn_is_read_whole(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            events = sessions.tail().poll(now=1000.0)
        self.assertEqual(["state", "statusline"], [name for name, _ in events])
        self.assertEqual({"busy": False, "turn": TURN_ID}, events_of(events)["state"])
        statusline = events_of(events)["statusline"]
        self.assertEqual("gpt-5.6-sol", statusline["model"])       # the model last in force
        self.assertEqual(THREAD_ID, statusline["thread_id"])
        self.assertEqual(WORKSPACE, statusline["cwd"])
        self.assertEqual(WINDOW, statusline["context_window"])
        self.assertEqual(LAST_REQUEST_PCT, statusline["context_pct"])
        # Every count the rollout reported, not only the one the percentage uses.
        self.assertEqual(USAGE, statusline["last_request_tokens"])
        self.assertIn("turn_tokens", statusline)
        self.assertIn("thread_tokens", statusline)

    def test_a_running_turn_is_busy(self):
        with SessionsDir() as sessions:
            sessions.add(BUSY, when=1000)
            events = sessions.tail().poll(now=1000.0)
        self.assertEqual({"busy": True, "turn": TURN_ID}, events_of(events)["state"])

    def test_the_pane_follows_its_own_workspace(self):
        with SessionsDir() as sessions:
            sessions.add(BUSY, when=1000)
            sessions.add(OTHER, when=3000)
            tail = sessions.tail(rescan_seconds=0)
            tail.poll(now=1000.0)
            self.assertEqual(BUSY_THREAD_ID, tail.state.thread_id)
            self.assertEqual(WORKSPACE, tail.state.cwd)

    def test_a_second_poll_of_an_unchanged_rollout_says_nothing(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            tail = sessions.tail()
            self.assertTrue(tail.poll(now=1000.0))
            self.assertEqual([], tail.poll(now=1001.0))

    def test_appended_records_are_read_from_where_the_last_poll_stopped(self):
        with SessionsDir() as sessions:
            path = sessions.add(TURN, when=1000)
            tail = sessions.tail()
            tail.poll(now=1000.0)
            sessions.append(path, json.dumps({"type": "event_msg",
                                              "payload": {"type": "task_started",
                                                          "turn_id": "turn-2",
                                                          "model_context_window": WINDOW}}) + "\n")
            events = events_of(tail.poll(now=1010.0))
        self.assertEqual({"busy": True, "turn": "turn-2"}, events["state"])

    def test_a_half_written_line_waits_for_its_newline(self):
        with SessionsDir() as sessions:
            path = sessions.add(TURN, when=1000)
            tail = sessions.tail()
            tail.poll(now=1000.0)
            record = json.dumps({"type": "event_msg",
                                 "payload": {"type": "task_started", "turn_id": "turn-3"}})
            sessions.append(path, record[:20])
            self.assertEqual([], tail.poll(now=1010.0))          # nothing readable yet
            sessions.append(path, record[20:] + "\n")
            events = events_of(tail.poll(now=1020.0))
        self.assertEqual({"busy": True, "turn": "turn-3"}, events["state"])

    def test_a_completion_for_an_older_turn_does_not_end_the_open_one(self):
        with SessionsDir() as sessions:
            path = sessions.add(TURN, when=1000)
            tail = sessions.tail()
            tail.poll(now=1000.0)
            lines = [json.dumps({"type": "event_msg",
                                 "payload": {"type": "task_started", "turn_id": "turn-9"}}),
                     json.dumps({"type": "event_msg",
                                 "payload": {"type": "task_complete", "turn_id": TURN_ID}})]
            sessions.append(path, "\n".join(lines) + "\n")
            events = events_of(tail.poll(now=1010.0))
        self.assertEqual({"busy": True, "turn": "turn-9"}, events["state"])

    def test_a_turn_that_was_interrupted_is_not_busy(self):
        with SessionsDir() as sessions:
            path = sessions.add(BUSY, when=1000)
            tail = sessions.tail()
            tail.poll(now=1000.0)
            sessions.append(path, json.dumps({"type": "event_msg",
                                              "payload": {"type": "turn_aborted",
                                                          "turn_id": TURN_ID,
                                                          "reason": "interrupted"}}) + "\n")
            events = events_of(tail.poll(now=1010.0))
        self.assertEqual({"busy": False, "turn": TURN_ID}, events["state"])

    def test_a_rollout_rewritten_in_place_is_read_afresh(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            tail = sessions.tail()
            tail.poll(now=1000.0)
            sessions.write(TURN, fixture(SESSIONS / BUSY), when=1010)
            events = events_of(tail.poll(now=1010.0))
        self.assertEqual(BUSY_THREAD_ID, events["statusline"]["thread_id"])
        self.assertEqual({"busy": True, "turn": TURN_ID}, events["state"])

    def test_a_newer_rollout_for_the_pane_replaces_the_old_one(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            tail = sessions.tail(rescan_seconds=0)
            tail.poll(now=1000.0)
            sessions.add(BUSY, when=2000)
            events = events_of(tail.poll(now=2000.0))
        self.assertEqual(BUSY_THREAD_ID, events["statusline"]["thread_id"])
        self.assertEqual({"busy": True, "turn": TURN_ID}, events["state"])

    def test_nothing_is_said_before_there_is_a_rollout(self):
        with SessionsDir() as sessions:
            tail = sessions.tail()
            self.assertEqual([], tail.poll(now=1000.0))
            self.assertIsNone(tail.state.rollout)
            self.assertEqual({}, events_of(tail.poll(now=1001.0)))

    def test_a_rollout_that_disappears_stops_the_events_without_claiming_idle(self):
        with SessionsDir() as sessions:
            path = sessions.add(TURN, when=1000)
            tail = sessions.tail(rescan_seconds=0)
            tail.poll(now=1000.0)
            path.unlink()
            self.assertEqual([], tail.poll(now=1010.0))

    def test_a_clock_tick_alone_is_not_an_event(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            tail = sessions.tail()
            tail.poll(now=1000.0)
            self.assertEqual([], tail.poll(now=9000.0))      # idle_seconds moved, nothing else

    def test_the_stale_flag_is_the_callers_threshold(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            tail = sessions.tail(stale_after=60)
            self.assertIs(False, events_of(tail.poll(now=1000.0))["statusline"]["stale"])
            self.assertIs(True, events_of(tail.poll(now=1061.0))["statusline"]["stale"])

    def test_watch_yields_what_reached_the_channel_and_stops_when_asked(self):
        with SessionsDir() as sessions, tempfile.TemporaryDirectory() as runtime:
            sessions.add(TURN, when=1000)
            slept = []
            with pane_environment(runtime):
                events = list(guest_codex.watch(sessions.tail(), interval=0,
                                                stop=lambda: bool(slept),
                                                sleep=lambda _: slept.append(1)))
        self.assertEqual(["state", "statusline"], [name for name, _ in events])
        self.assertEqual({"busy": False, "turn": TURN_ID}, events_of(events)["state"])

    def test_poll_and_emit_reports_how_many_events_reached_the_channel(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            self.assertEqual(0, guest_codex.poll_and_emit(sessions.tail(), env={}))
            self.assertEqual(2, guest_codex.poll_and_emit(
                sessions.tail(), env=pane_env(), runner=Recorder()))


# ----- the event channel -----------------------------------------------------------------------


class EventChannel(unittest.TestCase):
    def test_no_helper_in_the_environment_is_a_no_op(self):
        recorder = Recorder()
        for environment in ({}, {"RELAY_GUEST_EVENT": ""}, {"RELAY_SESSION_TOKEN": "t"}):
            with self.subTest(environment=environment):
                self.assertFalse(guest_codex.emit("state", {"busy": True}, env=environment,
                                                  runner=recorder))
        self.assertEqual(0, recorder.calls)

    def test_emit_hands_the_helper_the_event_and_the_data_on_stdin(self):
        recorder = Recorder()
        environment = pane_env(RELAY_PYTHON="/opt/relay/python3")
        self.assertTrue(guest_codex.emit("state", {"busy": True}, env=environment,
                                        runner=recorder))
        self.assertEqual(["/opt/relay/python3", "-S", str(HELPER), "state", "codex"],
                         recorder.argv)
        self.assertEqual('{"busy": true}', recorder.kwargs["input"])
        self.assertIs(True, recorder.kwargs["text"])
        self.assertIs(True, recorder.kwargs["capture_output"])
        self.assertEqual(guest_codex.HELPER_TIMEOUT, recorder.kwargs["timeout"])

    def test_the_interpreter_defaults_to_python3_on_path(self):
        recorder = Recorder()
        guest_codex.emit("state", {}, env=pane_env(), runner=recorder)
        self.assertTrue(recorder.argv[0])
        self.assertEqual(["-S", str(HELPER), "state", "codex"], recorder.argv[1:])

    def test_a_helper_that_cannot_run_is_not_a_failed_turn(self):
        for error in (OSError("gone"), subprocess.TimeoutExpired("helper", 5),
                      subprocess.SubprocessError("boom")):
            with self.subTest(error=error):
                self.assertFalse(guest_codex.emit("state", {}, env=pane_env(),
                                                  runner=Recorder(error)))

    def test_a_hook_event_wraps_the_payload(self):
        self.assertEqual({"name": "notify", "payload": {"type": "agent-turn-complete"}},
                         guest_codex.hook_data("notify", {"type": "agent-turn-complete"}))
        self.assertEqual(("hook", {"name": "notify",
                                   "payload": {"type": "agent-turn-complete"}}),
                         guest_codex.notify_event({"type": "agent-turn-complete"}))

    def test_the_helper_writes_the_envelope_the_pane_polls(self):
        with tempfile.TemporaryDirectory() as runtime, pane_environment(runtime):
            self.assertTrue(guest_codex.emit(guest_codex.STATE_EVENT, {"busy": True}))
            event = envelope(runtime)
        self.assertEqual({"token": "tok-1", "event": "state", "guest": "codex",
                          "data": {"busy": True}},
                         {key: event[key] for key in ("token", "event", "guest", "data")})
        self.assertTrue(event["sequence"])

    def test_two_events_never_share_a_sequence(self):
        with tempfile.TemporaryDirectory() as runtime, pane_environment(runtime):
            guest_codex.emit("state", {"busy": True})
            first = envelope(runtime)
            guest_codex.emit("state", {"busy": False})
            second = envelope(runtime)
        self.assertNotEqual(first["sequence"], second["sequence"])
        self.assertEqual({"busy": False}, second["data"])

    def test_the_helper_in_another_terminal_writes_nowhere(self):
        with tempfile.TemporaryDirectory() as runtime:
            self.assertFalse(guest_codex.emit("state", {"busy": True}, env={}))
            self.assertEqual([], os.listdir(runtime))


# ----- the notify hook -------------------------------------------------------------------------


class NotifyHook(unittest.TestCase):
    def test_only_a_finished_turn_is_a_hook(self):
        self.assertIsNotNone(guest_codex.notify_event({"type": "agent-turn-complete"}))
        for payload in ({"type": "something-else"}, {}, None, [], "text", 7):
            with self.subTest(payload=payload):
                self.assertIsNone(guest_codex.notify_event(payload))

    def test_the_payload_is_the_last_argument_codex_appends(self):
        recorder = Recorder()
        payload = {"type": "agent-turn-complete", "thread-id": THREAD_ID, "turn-id": TURN_ID,
                   "cwd": WORKSPACE, "client": "codex-tui", "input-messages": ["ls"],
                   "last-assistant-message": "done"}
        code = guest_codex.notify_main(["--some", "flag", json.dumps(payload)],
                                       env=pane_env(), runner=recorder)
        self.assertEqual(0, code)
        self.assertEqual(["hook", "codex"], recorder.argv[-2:])
        self.assertEqual({"name": "notify", "payload": payload},
                         json.loads(recorder.kwargs["input"]))

    def test_a_payload_that_is_not_json_is_dropped(self):
        recorder = Recorder()
        for argv in ([], ["not json"], ['{"type": "other"}'], ['["a", "list"]']):
            with self.subTest(argv=argv):
                self.assertEqual(0, guest_codex.notify_main(
                    argv, env=pane_env(), runner=recorder))
        self.assertEqual(0, recorder.calls)

    def test_it_exits_zero_even_when_the_helper_cannot_run(self):
        with tempfile.TemporaryDirectory() as runtime, pane_environment(runtime):
            self.assertEqual(0, guest_codex.notify_main(
                [json.dumps({"type": "agent-turn-complete"})], runner=Recorder(OSError("gone"))))
            self.assertEqual(0, guest_codex.main(["notify", "{oops"]))
            self.assertEqual(0, guest_codex.main([]))
            self.assertEqual(0, guest_codex.main(["nonsense", "--flag"]))

    def test_the_hook_reaches_the_channel_end_to_end(self):
        with tempfile.TemporaryDirectory() as runtime, pane_environment(runtime):
            payload = {"type": "agent-turn-complete", "turn-id": TURN_ID}
            self.assertEqual(0, guest_codex.main(["notify", json.dumps(payload)]))
            event = envelope(runtime)
        self.assertEqual("hook", event["event"])
        self.assertEqual({"name": "notify", "payload": payload}, event["data"])


# ----- the tail as a command -------------------------------------------------------------------


class TailCommand(unittest.TestCase):
    def test_one_poll_emits_the_state_and_statusline_events(self):
        with SessionsDir() as sessions, tempfile.TemporaryDirectory() as runtime:
            sessions.add(TURN, when=1000)
            with pane_environment(runtime):
                code = guest_codex.tail_main(["--once", "--sessions-dir", str(sessions.root),
                                              "--cwd", WORKSPACE])
            self.assertEqual(0, code)
            event = envelope(runtime)
        self.assertEqual("statusline", event["event"])       # the last event of the pair
        self.assertEqual(THREAD_ID, event["data"]["thread_id"])

    def test_a_tail_with_no_helper_is_still_a_clean_exit(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            self.assertEqual(0, guest_codex.tail_main(["--once", "--sessions-dir",
                                                       str(sessions.root)], env={}))

    def test_bad_options_are_not_errors(self):
        # Every case carries `--once`: without it the command is a watcher and loops until the
        # pane closes, which is exactly what it is for.
        with SessionsDir() as sessions:
            directory = str(sessions.root)
            for argv in (["--interval", "soon", "--once"], ["--stale-after", "later", "--once"],
                         ["--once", "--cwd"], ["--once", "--sessions-dir"]):
                with self.subTest(argv=argv):
                    self.assertEqual(0, guest_codex.tail_main(["--sessions-dir", directory]
                                                              + argv, env={}))

    def test_the_loop_ends_cleanly_when_a_pane_closes(self):
        with SessionsDir() as sessions:
            sessions.add(TURN, when=1000)
            tail = sessions.tail()
            self.assertEqual([], list(guest_codex.watch(tail, interval=0, stop=lambda: True,
                                                        env={}, sleep=lambda _: None)))


# ----- the settings as a command (the Options › Guests row calls this) --------------------------


# A config with a comment, a quoted-path table and a root key: enough shape that an enable which
# misplaced its lines could not round-trip it. Deliberately not REAL_CONFIG, which a user's own
# `notify` would make a conflict.
OWN_CONFIG = f'''# my own config\nmodel = "gpt-5.2"\n\n[projects."{WORKSPACE}"]\ntrust_level = "trusted"\n'''


class SettingsCommand(unittest.TestCase):
    def settings(self, argv: list[str]) -> tuple[int, dict]:
        """Run the settings CLI in this process and take its one JSON line back."""
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            code = guest_codex.settings_main(argv)
        return code, json.loads(captured.getvalue())

    def test_the_state_of_an_absent_home_is_reported_not_guessed_at(self):
        with tempfile.TemporaryDirectory() as home:
            code, state = self.settings(["--settings-state", "--home", home])
        self.assertEqual(0, code)
        self.assertTrue(state["ok"])
        self.assertFalse(state["enabled"])
        self.assertFalse(state["exists"])
        self.assertEqual(os.path.join(home, ".codex", "config.toml"), state["path"])
        self.assertEqual(["Codex has no config.toml yet."], state["notes"])

    def test_the_flags_reach_the_entry_point_like_any_subcommand(self):
        captured = io.StringIO()
        with tempfile.TemporaryDirectory() as home, contextlib.redirect_stdout(captured):
            code = guest_codex.main(["--settings-state", "--home", home])
        self.assertEqual(0, code)
        self.assertFalse(json.loads(captured.getvalue())["enabled"])

    def test_enable_and_disable_round_trip_a_config_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as home:
            target = os.path.join(home, ".codex", "config.toml")
            os.makedirs(os.path.dirname(target))
            with open(target, "w", encoding="utf-8") as handle:
                handle.write(OWN_CONFIG)
            code, enabled = self.settings(["--enable", "--home", home,
                                           "--python", PYTHON, "--script", SCRIPT])
            self.assertEqual(0, code)
            self.assertTrue(enabled["ok"])
            self.assertTrue(enabled["changed"])
            self.assertTrue(enabled["enabled"])
            self.assertTrue(enabled["marked"]["notify"])
            with open(target, encoding="utf-8") as handle:
                self.assertIn(guest_codex.MARKER, handle.read())
            code, disabled = self.settings(["--disable", "--home", home])
            self.assertEqual(0, code)
            self.assertTrue(disabled["ok"])
            self.assertTrue(disabled["changed"])
            self.assertFalse(disabled["enabled"])
            with open(target, encoding="utf-8") as handle:
                self.assertEqual(OWN_CONFIG, handle.read())
            code, again = self.settings(["--disable", "--home", home])   # off is idempotent
            self.assertEqual(0, code)
            self.assertFalse(again["changed"])

    def test_a_users_own_notify_is_a_conflict_the_file_survives(self):
        with tempfile.TemporaryDirectory() as home:
            target = os.path.join(home, ".codex", "config.toml")
            os.makedirs(os.path.dirname(target))
            mine = 'notify = ["my-own-notifier"]\n'
            with open(target, "w", encoding="utf-8") as handle:
                handle.write(mine)
            code, state = self.settings(["--enable", "--home", home])
            self.assertEqual(3, code)
            self.assertFalse(state["ok"])
            self.assertEqual(["notify"], state["conflict"])
            self.assertEqual("Your own codex config already sets notify; Relay leaves it alone.",
                             state["error"])
            with open(target, encoding="utf-8") as handle:
                self.assertEqual(mine, handle.read())

    def test_bad_option_lists_are_usage_errors(self):
        for argv in ([], ["--home"], ["--enable", "--nonsense"], ["nonsense", "--enable"]):
            with self.subTest(argv=argv):
                code, state = self.settings(argv)
                self.assertEqual(2, code)
                self.assertFalse(state["ok"])


if __name__ == "__main__":
    unittest.main()

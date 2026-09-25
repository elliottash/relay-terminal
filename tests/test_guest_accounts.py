# SPDX-License-Identifier: AGPL-3.0-or-later
"""Several Claude Code / Codex logins side by side (card #M8S2, `relay_core.guest_accounts`).

An account is a name and a config directory. These tests hold the one property that matters: a
preset naming an account starts, tests and resumes **only** under that account's directory, and
a figure it reports (login, limits) is that account's and nobody else's. Nothing here starts a
real claude or codex: the key tests drive the fakes of `test_keytest_guest`, extended to record
the environment they were started with, and the rest runs on `FakeHarness`.
"""
from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

from relay_core import guest_accounts, guest_harness_provider as ghp, keytest, roles  # noqa: E402
from relay_core.provider import ProviderConfig                                       # noqa: E402
from guest_harness_fake import FakeHarness                                           # noqa: E402
import test_keytest_guest as fakes_mod                                               # noqa: E402

# The fakes also write the account variables and whether an overriding key reached them, and a
# directory named by CLAUDE_CONFIG_DIR / CODEX_HOME is signed in when it holds `signed-in`.
_ENV_LOG = r'''
with open(os.path.join(here, "env.log"), "a") as log:
    log.write(json.dumps({"argv": sys.argv[1:], "CLAUDE_CONFIG_DIR": os.environ.get("CLAUDE_CONFIG_DIR"),
                          "CODEX_HOME": os.environ.get("CODEX_HOME"),
                          "ANTHROPIC_API_KEY": os.environ.get("ANTHROPIC_API_KEY"),
                          "OPENAI_API_KEY": os.environ.get("OPENAI_API_KEY")}) + "\n")
'''
FAKE_CLAUDE = fakes_mod.FAKE_CLAUDE.replace(
    'status = json.load(open(os.path.join(here, "claude-status.json")))',
    _ENV_LOG.strip() + '\nstatus = json.load(open(os.path.join(here, "claude-status.json")))\n'
    'if os.environ.get("CLAUDE_CONFIG_DIR"):\n'
    '    status["loggedIn"] = os.path.exists(os.path.join(os.environ["CLAUDE_CONFIG_DIR"], "signed-in"))',
    1)
FAKE_CODEX = fakes_mod.FAKE_CODEX.replace(
    'logged_in = os.path.exists(os.path.join(here, "codex-logged-in"))',
    _ENV_LOG.strip() + '\nlogged_in = os.path.exists(os.path.join(os.environ.get("CODEX_HOME") or here, '
    '"signed-in" if os.environ.get("CODEX_HOME") else "codex-logged-in"))', 1)
assert FAKE_CLAUDE != fakes_mod.FAKE_CLAUDE and FAKE_CODEX != fakes_mod.FAKE_CODEX


class Registry:
    """A private registry file and XDG config root for one test."""

    def __init__(self, test):
        self.root = tempfile.mkdtemp(prefix="relay-guest-accounts-")
        test.addCleanup(shutil.rmtree, self.root, True)
        patcher = mock.patch.dict(os.environ, {
            "XDG_CONFIG_HOME": os.path.join(self.root, "config"),
            guest_accounts.ENV_PATH: os.path.join(self.root, "accounts.json")})
        patcher.start()
        test.addCleanup(patcher.stop)
        guest_accounts._cache = None
        test.addCleanup(setattr, guest_accounts, "_cache", None)

    def add(self, guest_id, account_id, *, signed_in=False, label=None):
        entry = guest_accounts.save({"guest": guest_id, "id": account_id,
                                     "label": label or account_id})
        if signed_in:
            open(os.path.join(entry.config_dir, "signed-in"), "w").close()
        return entry


def reset_guest_state(test):
    ghp.reset_catalog()
    ghp._detected = None
    ghp._adapter_cache.clear()
    ghp._LAST_LIMITS.clear()
    ghp._asked_accounts.clear()
    test.addCleanup(ghp.reset_catalog)
    test.addCleanup(setattr, ghp, "_detected", None)
    test.addCleanup(ghp._adapter_cache.clear)
    test.addCleanup(ghp._LAST_LIMITS.clear)
    test.addCleanup(ghp._asked_accounts.clear)


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.reg = Registry(self)

    def test_an_account_is_saved_found_listed_and_deleted_without_touching_its_directory(self):
        entry = self.reg.add("claude", "work", label="Work Max")
        self.assertEqual(entry.preset_id, "guest:claude:work")
        self.assertTrue(os.path.isdir(entry.config_dir))
        self.assertTrue(entry.config_dir.startswith(os.environ["XDG_CONFIG_HOME"]))
        self.assertEqual(guest_accounts.find("claude", "work").label, "Work Max")
        self.assertIsNone(guest_accounts.find("codex", "work"))
        on_disk = json.loads(Path(guest_accounts.config_path()).read_text())
        self.assertEqual(on_disk["accounts"][0]["config_dir"], entry.config_dir)
        self.assertTrue(guest_accounts.delete("claude", "work"))
        self.assertEqual(guest_accounts.accounts(), [])
        self.assertTrue(os.path.isdir(entry.config_dir))      # the login is not signed out
        self.assertFalse(guest_accounts.delete("claude", "work"))

    def test_an_existing_directory_is_registered_as_it_is(self):
        # The usage tracker's homes, already signed in, are reused rather than logged in again.
        existing = os.path.join(self.reg.root, "codex-eth")
        os.makedirs(existing)
        entry = guest_accounts.save({"guest": "codex", "label": "ETH", "config_dir": existing})
        self.assertEqual((entry.id, entry.config_dir), ("eth", existing))
        self.assertEqual(guest_accounts.login_command("codex", "eth"),
                         f"CODEX_HOME={existing} codex login")

    def test_bad_ids_the_default_directory_and_a_shared_directory_are_refused(self):
        for spec in ({"guest": "gemini", "id": "x"}, {"guest": "claude", "id": "Bad Id!"},
                     {"guest": "claude", "id": "default"}):
            with self.assertRaises(ValueError):
                guest_accounts.save(spec)
        with mock.patch.dict(os.environ, {"HOME": self.reg.root}):
            with self.assertRaises(ValueError):
                guest_accounts.save({"guest": "claude", "id": "home",
                                     "config_dir": os.path.join(self.reg.root, ".claude")})
        first = self.reg.add("claude", "a")
        with self.assertRaises(ValueError):
            guest_accounts.save({"guest": "claude", "id": "b", "config_dir": first.config_dir})

    def test_the_environment_sets_the_directory_and_drops_keys_that_outrank_the_login(self):
        entry = self.reg.add("claude", "work")
        base = {"PATH": "/bin", "ANTHROPIC_API_KEY": "sk-x", "CLAUDE_CODE_OAUTH_TOKEN": "t"}
        env = guest_accounts.environment("claude", "work", base)
        self.assertEqual(env["CLAUDE_CONFIG_DIR"], entry.config_dir)
        self.assertNotIn("ANTHROPIC_API_KEY", env)
        self.assertNotIn("CLAUDE_CODE_OAUTH_TOKEN", env)
        self.assertEqual(guest_accounts.environment("claude", "", base), base)   # default login
        with self.assertRaises(ValueError):
            guest_accounts.environment("claude", "gone", base)

    def test_the_protocol_lists_saves_and_deletes(self):
        events = []
        guest_accounts.handle({"type": "guest_account_save", "id": 1,
                               "account": {"guest": "codex", "label": "Gmail"}}, events.append)
        self.assertEqual(events[0]["event"], "guest_account_saved")
        self.assertEqual(events[0]["account"]["preset"], "guest:codex:gmail")
        self.assertIn("CODEX_HOME=", events[0]["account"]["login_command"])
        self.assertEqual([a["id"] for a in events[1]["accounts"]], ["gmail"])
        events.clear()
        guest_accounts.handle({"type": "guest_account_delete", "id": 2, "key": "codex:gmail"},
                              events.append)
        self.assertEqual((events[0]["event"], events[0]["removed"]), ("guest_account_deleted", True))
        self.assertEqual(events[1]["accounts"], [])
        events.clear()
        guest_accounts.handle({"type": "guest_account_save", "id": 3, "account": {"guest": "x"}},
                              events.append)
        self.assertEqual(events[0]["event"], "error")


class PresetIdTests(unittest.TestCase):
    def setUp(self):
        self.reg = Registry(self)

    def test_an_account_preset_names_its_guest_its_account_and_its_base_url(self):
        self.assertEqual(ghp.preset_guest_id("guest:claude:work"), "claude")
        self.assertEqual(ghp.preset_account("guest:claude:work"), "work")
        self.assertEqual(ghp.preset_key("guest:codex:eth"), "codex:eth")
        self.assertEqual((ghp.preset_guest_id("guest:claude"), ghp.preset_account("guest:claude")),
                         ("claude", ""))
        self.assertIsNone(ghp.preset_guest_id("guest:claude:Bad!"))
        self.assertIsNone(ghp.preset_guest_id("guest:gemini:work"))
        self.reg.add("claude", "work")
        config = ghp.config_for_preset("guest:claude:work", {})
        self.assertEqual(config.base_url, "harness://claude/work")
        self.assertEqual((ghp.config_guest_id(config), ghp.config_account(config),
                          ghp.config_preset(config)), ("claude", "work", "guest:claude:work"))
        self.assertEqual(ghp.config_preset(ghp.config_for_preset("guest:codex", {})), "guest:codex")
        # The roles module spells the same base URL without importing the guest stack.
        self.assertEqual(roles.guest_base_url("guest:claude:work"), "harness://claude/work")
        self.assertEqual(roles.guest_base_url("guest:codex"), "harness://codex")

    def test_a_removed_account_is_refused_rather_than_run_on_another_login(self):
        with self.assertRaisesRegex(ValueError, "no account 'gone'"):
            ghp.config_for_preset("guest:claude:gone", {})
        with self.assertRaisesRegex(ValueError, "no account 'gone'"):
            ghp.start_provider("guest:claude:gone", {}, tempfile.gettempdir())
        with mock.patch.object(ghp, "adapter_available", return_value=True), \
             mock.patch.object(ghp, "installations",
                               return_value={"claude": {"installed": True, "binary": "claude"}}):
            self.assertFalse(roles.guest_runnable("claude:gone"))
            self.assertTrue(roles.guest_runnable("claude"))
            self.reg.add("claude", "back")
            self.assertTrue(roles.guest_runnable("claude:back"))

    def test_an_account_ranks_with_its_cli(self):
        from relay_core import model_ranking
        rank = model_ranking.load()
        self.assertEqual(rank.provider_order("guest:claude:work"), rank.provider_order("guest:claude"))
        self.assertEqual(rank.provider_kind("guest:codex:eth"), rank.provider_kind("guest:codex"))


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.reg = Registry(self)
        reset_guest_state(self)

    def test_both_adapters_start_their_process_under_the_accounts_directory(self):
        from relay_core.guest_harness_claude import ClaudeHarness
        from relay_core.guest_harness_codex import CodexHarness
        work = self.reg.add("claude", "work")
        eth = self.reg.add("codex", "eth")
        with mock.patch.object(ghp, "_load_adapter",
                               side_effect=lambda g: ClaudeHarness if g == "claude" else CodexHarness):
            claude = ghp.make_harness("claude", memory="relay", account="work")
            codex = ghp.make_harness("codex", account="eth")
            default = ghp.make_harness("claude")
        spawned = {}

        def claude_spawn(argv, cwd, env):
            spawned["claude"] = env
            raise OSError("stop here")

        claude._spawn = claude_spawn
        with mock.patch.dict(os.environ, {"ANTHROPIC_API_KEY": "sk-x", "OPENAI_API_KEY": "sk-o"}):
            with self.assertRaises(Exception):
                claude._launch(["claude"])
            env = codex._child_env()
            self.assertEqual(default._env_overrides, {})
        self.assertEqual(spawned["claude"]["CLAUDE_CONFIG_DIR"], work.config_dir)
        self.assertNotIn("ANTHROPIC_API_KEY", spawned["claude"])
        self.assertEqual(env["CODEX_HOME"], eth.config_dir)
        self.assertNotIn("OPENAI_API_KEY", env)

    def test_start_provider_carries_the_account_onto_the_provider_and_its_fields(self):
        self.reg.add("claude", "work")
        made = {}

        def make(guest_id, probe=False, *, memory=None, account=""):
            made["account"] = account
            return FakeHarness([])

        with mock.patch.object(ghp, "make_harness", side_effect=make):
            provider = ghp.start_provider("guest:claude:work", {}, tempfile.gettempdir())
        self.addCleanup(provider.close)
        self.assertEqual(made["account"], "work")
        self.assertEqual(provider.account, "work")
        self.assertEqual(provider.config.base_url, "harness://claude/work")

        class Agent:
            pass

        agent = Agent()
        agent.provider = provider
        self.assertEqual(ghp.configured_fields(agent)["guest_account"], "work")
        # Another account of the same guest is another process: the harness is not reused.
        self.assertIsNone(ghp.switch_model(agent, "claude", {"guest": {}}, ""))
        self.assertIs(ghp.switch_model(agent, "claude", {"guest": {}}, "work"), provider)

    def test_a_saved_session_resumes_only_on_its_own_account(self):
        self.reg.add("claude", "work")
        provider = ghp.HarnessProvider(ProviderConfig("harness://claude/work", "m", "", {}, 1),
                                       FakeHarness([]), "claude")
        self.addCleanup(provider.close)
        self.assertEqual(ghp.session_account({"guest_account": "work"}), "work")
        self.assertEqual(ghp.session_account({"guest_account": "../x"}), "")

        class Agent:
            pass

        agent = Agent()
        agent.provider = provider
        with mock.patch.object(ghp, "make_harness") as make:
            ghp.resume_session(agent, {"guest": "claude", "guest_session": "s-1"}, lambda e: None)
        make.assert_not_called()                  # default-login session, account harness: left


class OwnHomePathsTests(unittest.TestCase):
    """Card #WZ3K: the scratch sweep skips the running harness's own home-level files.
    `own_home_paths` names them for a config: the config dir the account actually runs on,
    the default home, and Claude Code's ~/.claude.json sidecar."""

    def setUp(self):
        self.reg = Registry(self)
        # Run from a Relay pane, the process names the Relay-owned home (#5A37); these cases are
        # about a machine without it, and the next one about a machine with it.
        env = mock.patch.dict(os.environ, {"RELAY_GUEST_HOME": "", "RELAY_USER_CLAUDE_CONFIG_DIR": "",
                                           "RELAY_USER_CODEX_HOME": ""})
        env.start()
        self.addCleanup(env.stop)

    def test_the_relay_home_and_the_users_own_are_both_owned(self):
        relay = os.path.join(self.reg.root, "data", "guests")
        with mock.patch.dict(os.environ, {"HOME": self.reg.root, "RELAY_GUEST_HOME": relay,
                                          "CLAUDE_CONFIG_DIR": os.path.join(relay, "claude")}):
            paths = ghp.own_home_paths(self.config("harness://claude"))
        self.assertIn(os.path.join(relay, "claude"), paths)
        self.assertIn(os.path.join(self.reg.root, ".claude"), paths)

    def config(self, url):
        return ProviderConfig(url, "m", "", {}, 1)

    def test_claude_names_its_config_dir_and_the_sidecar_file(self):
        with mock.patch.dict(os.environ, {"HOME": self.reg.root}):
            paths = ghp.own_home_paths(self.config("harness://claude"))
        self.assertIn(os.path.join(self.reg.root, ".claude"), paths)
        self.assertIn(os.path.join(self.reg.root, ".claude.json"), paths)
        self.assertNotIn(os.path.join(self.reg.root, ".codex"), paths)

    def test_an_env_override_is_owned_and_the_default_stays_listed(self):
        override = os.path.join(self.reg.root, ".claude-work")
        with mock.patch.dict(os.environ, {"HOME": self.reg.root, "CLAUDE_CONFIG_DIR": override}):
            paths = ghp.own_home_paths(self.config("harness://claude"))
        self.assertIn(override, paths)                              # the env-var home
        self.assertIn(os.path.join(self.reg.root, ".claude"), paths)  # the default, still listed
        self.assertIn(os.path.join(self.reg.root, ".claude.json"), paths)

    def test_an_account_runs_on_its_registered_directory(self):
        entry = self.reg.add("claude", "work")
        with mock.patch.dict(os.environ, {"HOME": self.reg.root}):
            paths = ghp.own_home_paths(self.config("harness://claude/work"))
        self.assertIn(entry.config_dir, paths)                      # the account's dir
        self.assertIn(os.path.join(self.reg.root, ".claude"), paths)

    def test_codex_names_its_home_and_no_claude_sidecar(self):
        with mock.patch.dict(os.environ, {"HOME": self.reg.root}):
            paths = ghp.own_home_paths(self.config("harness://codex"))
            self.assertIn(os.path.join(self.reg.root, ".codex"), paths)
            self.assertNotIn(os.path.join(self.reg.root, ".claude.json"), paths)
            with mock.patch.dict(os.environ,
                                 {"CODEX_HOME": os.path.join(self.reg.root, ".codex-work")}):
                paths = ghp.own_home_paths(self.config("harness://codex"))
        self.assertIn(os.path.join(self.reg.root, ".codex-work"), paths)
        self.assertIn(os.path.join(self.reg.root, ".codex"), paths)

    def test_a_config_no_guest_serves_owns_nothing(self):
        self.assertEqual(ghp.own_home_paths(self.config("https://api.anthropic.com")), [])


class RowsAndLimitsTests(unittest.TestCase):
    def setUp(self):
        self.reg = Registry(self)
        reset_guest_state(self)

    def test_each_account_is_a_row_with_its_own_login_and_limits(self):
        self.reg.add("claude", "work", label="Work")
        self.reg.add("claude", "home")
        with mock.patch.object(ghp, "installations", return_value={
                "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                "codex": {"installed": False, "binary": "", "version": ""}}), \
             mock.patch.object(ghp, "_read_login_status", return_value=None), \
             mock.patch.object(ghp, "_read_codex_catalog", return_value=[]), \
             mock.patch.object(ghp, "adapter_available", return_value=True):
            ghp.note_login("claude", True)
            ghp.note_login("claude:work", True)
            ghp.note_login("claude:home", False)
            ghp.usage_limits_event("claude", {"windows": [
                {"kind": "5h", "used_percent": 10, "resets_at": 1}]}, "work")
            rows = {row["id"]: row for row in ghp.preset_rows()}
            # The rows start the background scan; it must finish inside these mocks, or it runs on
            # into the next test and marks *its* catalogue ready.
            self.assertTrue(ghp.catalog_ready.wait(5.0))
        self.assertIn("guest:claude:work", rows)
        work, home = rows["guest:claude:work"], rows["guest:claude:home"]
        self.assertEqual((work["label"], work["guest"], work["account"]),
                         ("Claude Code (Work)", "claude", "work"))
        self.assertEqual(work["base_url"], "harness://claude/work")
        self.assertTrue(work["harness"])
        self.assertEqual((rows["guest:claude"]["logged_in"], work["logged_in"], home["logged_in"]),
                         (True, True, False))
        self.assertEqual(work["limits"]["windows"][0]["used_percent"], 10)
        self.assertNotIn("limits", home)
        self.assertNotIn("limits", rows["guest:claude"])
        self.assertIn("CLAUDE_CONFIG_DIR=", work["login_command"])

    def test_a_finished_sign_in_asks_every_login_again(self):
        self.reg.add("claude", "work")
        answers = {None: True, "work": True}
        seen = []

        def status(guest_id, binary, env=None):
            account = "work" if env and env.get("CLAUDE_CONFIG_DIR", "").endswith("claude-work") else None
            seen.append(account)
            return answers[account]

        pushed = []
        ghp.note_login("claude", False)
        ghp.note_login("claude:work", False)
        ghp.set_catalog_listener(lambda: pushed.append(True))
        self.addCleanup(ghp.set_catalog_listener, None)
        with mock.patch.object(ghp, "installations", return_value={
                "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                "codex": {"installed": False, "binary": "", "version": ""}}), \
             mock.patch.object(ghp, "_read_login_status", side_effect=status):
            ghp.refresh_logins()
            deadline = __import__("time").monotonic() + 5
            while not pushed and __import__("time").monotonic() < deadline:
                __import__("time").sleep(0.02)
        self.assertEqual(sorted(seen, key=str), sorted([None, "work"], key=str))
        self.assertEqual((ghp.login_status("claude"), ghp.login_status("claude:work")), (True, True))
        self.assertTrue(pushed)

    def test_a_limits_event_is_filed_under_the_accounts_preset(self):
        event = ghp.usage_limits_event("codex", {"windows": [
            {"kind": "weekly", "used_percent": 50, "resets_at": 2}]}, "eth")
        self.assertEqual((event["preset"], event["account"]), ("guest:codex:eth", "eth"))
        self.assertEqual(ghp.last_limits("codex:eth")["windows"][0]["used_percent"], 50)
        self.assertEqual(ghp.last_limits("codex"), {})


class AccountKeyTestTests(unittest.TestCase):
    """`test_key {preset: "guest:claude:work"}` end to end on the fake CLIs."""

    def setUp(self):
        self.reg = Registry(self)
        reset_guest_state(self)
        self.scratch = tempfile.mkdtemp(prefix="relay-keytest-cwd-")
        self.addCleanup(shutil.rmtree, self.scratch, True)
        patcher = mock.patch.multiple(fakes_mod, FAKE_CLAUDE=FAKE_CLAUDE, FAKE_CODEX=FAKE_CODEX)
        patcher.start()
        self.addCleanup(patcher.stop)

    def env_log(self, fakes):
        try:
            with open(os.path.join(fakes.dir, "env.log")) as f:
                return [json.loads(line) for line in f if line.strip()]
        except FileNotFoundError:
            return []

    def run_test(self, preset):
        events = []
        thread = keytest.run(preset, events.append, "req", cwd=self.scratch)
        thread.join(20)
        return fakes_mod.wait_for(events)[0]

    def test_a_signed_in_claude_account_runs_its_turn_in_its_own_directory(self):
        work = self.reg.add("claude", "work", signed_in=True)
        fakes = fakes_mod.FakePath(self)
        with mock.patch.dict(os.environ, {"ANTHROPIC_API_KEY": "sk-should-not-pass"}):
            event = self.run_test("guest:claude:work")
        self.assertTrue(event["ok"], event)
        self.assertEqual((event["preset"], event["guest"], event["account"]),
                         ("guest:claude:work", "claude", "work"))
        seen = self.env_log(fakes)
        self.assertEqual({e["CLAUDE_CONFIG_DIR"] for e in seen}, {work.config_dir})
        self.assertEqual({e["ANTHROPIC_API_KEY"] for e in seen}, {None})
        self.assertEqual([e["argv"][:2] for e in seen][0], ["auth", "status"])
        self.assertIs(ghp.login_status("claude:work"), True)
        self.assertIsNone(ghp.login_status("claude"))        # the default login was not asked

    def test_a_signed_out_codex_account_is_refused_with_its_login_line(self):
        eth = self.reg.add("codex", "eth", signed_in=False)
        fakes = fakes_mod.FakePath(self)
        event = self.run_test("guest:codex:eth")
        self.assertFalse(event["ok"])
        self.assertIn(f"CODEX_HOME={eth.config_dir} codex login", event["error"])
        self.assertEqual([e["argv"] for e in self.env_log(fakes)], [["login", "status"]])
        self.assertEqual(self.env_log(fakes)[0]["CODEX_HOME"], eth.config_dir)
        self.assertIs(ghp.login_status("codex:eth"), False)

    def test_two_codex_accounts_each_answer_for_themselves(self):
        self.reg.add("codex", "a", signed_in=True)
        b = self.reg.add("codex", "b", signed_in=True)
        fakes = fakes_mod.FakePath(self)
        self.assertTrue(self.run_test("guest:codex:a")["ok"])
        before = len(self.env_log(fakes))
        self.assertTrue(self.run_test("guest:codex:b")["ok"])
        after = self.env_log(fakes)[before:]
        self.assertEqual({e["CODEX_HOME"] for e in after}, {b.config_dir})
        self.assertEqual({e["argv"][0] for e in after}, {"login", "app-server"})

    def test_the_background_scan_asks_each_account_separately(self):
        self.reg.add("claude", "in", signed_in=True)
        self.reg.add("claude", "out", signed_in=False)
        fakes_mod.FakePath(self, install=("claude",))
        ghp.reset_catalog()
        ghp.start_catalog_scan()
        self.assertTrue(ghp.catalog_ready.wait(20))
        self.assertEqual((ghp.login_status("claude:in"), ghp.login_status("claude:out")),
                         (True, False))


class AccountSessionTests(unittest.TestCase):
    """An account's transcripts are in its own directory: indexed from there, listed with the
    account's preset (what the pane resumes them on) and tailed from there while it runs."""

    def setUp(self):
        import test_guest_sessions as gs
        self.gs = gs
        self.reg = Registry(self)
        self.home = Path(self.reg.root) / "home"
        self.home.mkdir()
        # HOME is the scan's default login; nothing under the real one is read.
        patcher = mock.patch.dict(os.environ, {"HOME": str(self.home)})
        patcher.start()
        self.addCleanup(patcher.stop)
        from relay_core import guest_sessions
        self.guest_sessions = guest_sessions
        self.index = gs.index_in(Path(self.reg.root))
        self.addCleanup(self.index.close)

    def account_claude(self, entry, session_id):
        folder = Path(entry.config_dir) / "projects" / self.guest_sessions.claude_slug(self.gs.CWD)
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f"{session_id}.jsonl"
        path.write_text("".join(json.dumps(line) + "\n"
                                for line in self.gs.claude_lines(session_id=session_id)))
        return path

    def test_reconcile_lists_each_session_under_its_own_login(self):
        work = self.reg.add("claude", "work")
        eth = self.reg.add("codex", "eth")
        other = "0b1c2d3e-0000-4000-8000-000000000001"
        self.gs.write_claude(self.home, self.gs.claude_lines())                  # default login
        self.account_claude(work, other)
        folder = Path(eth.config_dir) / "sessions" / "2026" / "09" / "18"
        folder.mkdir(parents=True)
        (folder / self.gs.CODEX_NAME).write_text(
            "".join(json.dumps(line) + "\n" for line in self.gs.codex_lines()))
        result = self.guest_sessions.reconcile(self.index, enabled=True)
        self.assertEqual(result["added"], 3)
        records = {r["id"]: r for r in self.guest_sessions.list_sessions(self.index)}
        self.assertNotIn("account", records[self.gs.CLAUDE_ID])
        self.assertEqual((records[other]["account"], records[other]["preset"]),
                         ("work", "guest:claude:work"))
        self.assertEqual(records[self.gs.CODEX_ID]["preset"], "guest:codex:eth")
        annotated = self.guest_sessions.annotate_items(
            self.index.search("", sources=["claude"], scope="all", limit=10, sort="recent")["items"])
        self.assertEqual({i.get("preset", "") for i in annotated}, {"", "guest:claude:work"})
        # An account whose directory is gone prunes nothing (its sessions may come back).
        shutil.rmtree(work.config_dir)
        self.assertEqual(self.guest_sessions.reconcile(self.index, enabled=True)["removed"], 0)

    def test_the_live_tail_of_an_account_session_reads_its_directory(self):
        work = self.reg.add("claude", "work")
        session = "0b1c2d3e-0000-4000-8000-000000000002"
        path = self.account_claude(work, session)
        self.assertIsNone(self.guest_sessions.LiveTail.for_session("claude", self.gs.CWD,
                                                                   session_id=session))
        tail = self.guest_sessions.LiveTail.for_session("claude", self.gs.CWD, session_id=session,
                                                        account="work")
        self.assertEqual(tail.path, path)
        self.assertEqual(tail.parsed["preset"], "guest:claude:work")
        follower = self.guest_sessions.GuestTail(min_poll=0.0)
        self.assertTrue(follower.start(self.index, "claude", self.gs.CWD, session_id=session,
                                       account="work"))
        follower.stop()
        (item,) = self.index.search("", sources=["claude"], scope="all", limit=5, sort="recent")["items"]
        self.assertEqual(item["preset"], "guest:claude:work")
        self.assertIsNone(self.guest_sessions.LiveTail.for_session(
            "claude", self.gs.CWD, session_id=session, account="gone"))


if __name__ == "__main__":
    unittest.main()

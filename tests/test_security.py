"""The Security section's policy (card #3KB7): the command denylist, extra readable folders and
extra secret patterns, as the worker enforces them."""
import unittest
from pathlib import Path

from relay_core import security


def policy(**kwargs) -> security.Policy:
    return security.policy_from(security.validate(kwargs))


class ValidateTests(unittest.TestCase):
    def test_absent_keys_change_nothing(self):
        self.assertEqual(security.validate({}), {})
        self.assertTrue(security.policy_from({}).is_empty())

    def test_an_empty_list_is_a_value_not_an_absence(self):
        # Clearing the denylist in Options must reach the worker as "no rules", not as "unchanged".
        self.assertEqual(security.validate({"command_denylist": []}), {"command_denylist": []})

    def test_lists_are_trimmed_deduplicated_and_order_kept(self):
        out = security.validate({"command_denylist": [" rm ", "rm", "shutdown", "  "]})
        self.assertEqual(out["command_denylist"], ["rm", "shutdown"])

    def test_a_list_must_be_a_list_of_strings(self):
        for bad in ({"command_denylist": "rm"}, {"command_denylist": [3]}):
            with self.assertRaises(ValueError):
                security.validate(bad)

    def test_rules_are_bounded(self):
        out = security.validate({"command_denylist": ["x" * (security.MAX_RULE + 1), "rm"]})
        self.assertEqual(out["command_denylist"], ["rm"])
        many = security.validate({"command_denylist": [f"r{n}" for n in range(security.MAX_RULES + 50)]})
        self.assertLessEqual(len(many["command_denylist"]), security.MAX_RULES)

    def test_a_readable_folder_must_be_absolute_and_is_expanded(self):
        with self.assertRaises(ValueError):
            security.validate({"readable_roots": ["relative/dir"]})
        out = security.validate({"readable_roots": ["~/notes/../notes"]})
        self.assertTrue(out["readable_roots"][0].endswith("/notes"))
        self.assertTrue(out["readable_roots"][0].startswith("/"))

    def test_a_bad_secret_pattern_is_reported_not_swallowed(self):
        with self.assertRaises(ValueError) as caught:
            security.validate({"secret_patterns": ["([unclosed"]})
        self.assertIn("regular expression", str(caught.exception))

    def test_a_bad_pattern_that_somehow_arrives_does_not_break_the_worker(self):
        # policy_from takes already-validated values; a worker handed a bad one must still run.
        made = security.policy_from({"secret_patterns": ["([unclosed", "vault"]})
        self.assertFalse(security.extra_secret(made, "([unclosed"))
        self.assertTrue(security.extra_secret(made, "vault.txt"))


class DenylistTests(unittest.TestCase):
    def test_no_rules_denies_nothing(self):
        self.assertIsNone(security.denied_command(security.EMPTY, "rm -rf /"))

    def test_a_bare_rule_names_a_program(self):
        p = policy(command_denylist=["rm"])
        self.assertEqual(security.denied_command(p, "rm -rf build"), "rm")
        self.assertEqual(security.denied_command(p, "/bin/rm -rf build"), "rm")

    def test_a_bare_rule_does_not_match_a_longer_program_or_an_argument(self):
        p = policy(command_denylist=["rm"])
        self.assertIsNone(security.denied_command(p, "rmdir build"))
        self.assertIsNone(security.denied_command(p, "cat rm.txt"))
        self.assertIsNone(security.denied_command(p, "echo 'rm -rf'"))

    def test_it_looks_past_a_separator(self):
        p = policy(command_denylist=["rm"])
        for command in ("ls && rm -rf x", "ls; rm x", "ls | rm x", "ls || rm x"):
            self.assertEqual(security.denied_command(p, command), "rm", command)

    def test_it_steps_over_sudo_and_env_prefixes(self):
        p = policy(command_denylist=["apt"])
        for command in ("sudo apt install x", "sudo -H apt install x", "env FOO=bar apt install x",
                        "nohup apt install x"):
            self.assertEqual(security.denied_command(p, command), "apt", command)

    def test_sudo_itself_can_be_the_rule(self):
        p = policy(command_denylist=["sudo"])
        self.assertEqual(security.denied_command(p, "sudo apt install x"), "sudo")

    def test_a_glob_rule_matches_the_whole_line(self):
        p = policy(command_denylist=["*--force*"])
        self.assertEqual(security.denied_command(p, "git push --force origin main"), "*--force*")
        self.assertIsNone(security.denied_command(p, "git push origin main"))

    def test_the_first_matching_rule_is_the_one_reported(self):
        p = policy(command_denylist=["shutdown", "rm"])
        self.assertEqual(security.denied_command(p, "rm -rf x"), "rm")

    def test_an_unlexable_line_still_gets_checked(self):
        p = policy(command_denylist=["rm"])
        self.assertEqual(security.denied_command(p, "rm -rf 'unclosed"), "rm")

    def test_blank_and_non_string_commands_are_not_denied(self):
        p = policy(command_denylist=["rm"])
        self.assertIsNone(security.denied_command(p, "   "))
        self.assertIsNone(security.denied_command(p, None))

    def test_the_refusal_names_the_rule_and_forbids_respelling(self):
        text = security.refusal("rm")
        self.assertIn("'rm'", text)
        self.assertIn("Options › Security", text)
        self.assertIn("another way to spell it", text)


class ReadableRootTests(unittest.TestCase):
    def test_nothing_configured_widens_nothing(self):
        self.assertIsNone(security.readable_root(security.EMPTY, Path("/home/e/notes/a.md")))

    def test_a_path_inside_a_configured_folder_is_found(self):
        p = policy(readable_roots=["/home/e/notes"])
        self.assertEqual(security.readable_root(p, Path("/home/e/notes/a.md")), Path("/home/e/notes"))
        self.assertEqual(security.readable_root(p, Path("/home/e/notes")), Path("/home/e/notes"))

    def test_a_sibling_with_a_shared_prefix_is_not_inside(self):
        p = policy(readable_roots=["/home/e/notes"])
        self.assertIsNone(security.readable_root(p, Path("/home/e/notes-private/a.md")))

    def test_a_path_outside_every_folder_is_not_found(self):
        p = policy(readable_roots=["/home/e/notes"])
        self.assertIsNone(security.readable_root(p, Path("/etc/passwd")))


class SecretPatternTests(unittest.TestCase):
    def test_nothing_configured_adds_nothing(self):
        self.assertFalse(security.extra_secret(security.EMPTY, "id_rsa"))

    def test_a_user_pattern_blocks_a_path_component(self):
        p = policy(secret_patterns=[r"\.vault$", "credentials"])
        self.assertTrue(security.extra_secret(p, "prod.vault"))
        self.assertTrue(security.extra_secret(p, "aws-credentials"))
        self.assertFalse(security.extra_secret(p, "notes.md"))


class WiringTests(unittest.TestCase):
    """The policy reaching the tools. The module above is pure; this is the half that can silently
    not be connected."""

    def setUp(self):
        import tempfile, threading
        from relay_core.tools import ToolExecutor
        self.tmp = tempfile.TemporaryDirectory()
        self.outside = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "in.txt").write_text("inside")
        Path(self.outside.name, "out.txt").write_text("outside")
        (self.root / "prod.vault").write_text("shh")
        (self.root / ".env").write_text("SECRET=1")   # must exist: the missing-file
        # error would otherwise be what refuses it, and prove nothing about the guard
        self.events = []
        self.make = lambda **kw: ToolExecutor(self.tmp.name, self.events.append, threading.Event(),
                                              policy=policy(**kw))

    def tearDown(self):
        self.tmp.cleanup()
        self.outside.cleanup()

    def test_a_denied_command_is_refused_before_it_runs(self):
        tools = self.make(command_denylist=["rm"])
        with self.assertRaises(ValueError) as caught:
            tools.prepare("run_command", {"command": "rm -rf ."})
        self.assertIn("denylist", str(caught.exception))
        self.assertTrue((self.root / "in.txt").exists())

    def test_an_allowed_command_still_runs(self):
        tools = self.make(command_denylist=["rm"])
        result = tools.execute(tools.prepare("run_command", {"command": "printf ok"}))
        self.assertEqual(result["output"], "ok")

    def test_the_denylist_is_rechecked_at_execution(self):
        tools = self.make()
        prepared = tools.prepare("run_command", {"command": "rm -f nothing"})
        tools.policy = policy(command_denylist=["rm"])   # the user changed it mid-turn
        with self.assertRaises(ValueError):
            tools.execute(prepared)

    def test_a_read_outside_the_workspace_needs_the_folder_listed(self):
        blocked = self.make()
        with self.assertRaises(ValueError):
            blocked.prepare("read_file", {"path": str(Path(self.outside.name, "out.txt"))})
        allowed = self.make(readable_roots=[self.outside.name])
        result = allowed.execute(allowed.prepare("read_file", {"path": str(Path(self.outside.name, "out.txt"))}))
        self.assertEqual(result["content"], "outside")

    def test_a_listed_folder_widens_reading_only_never_writing(self):
        tools = self.make(readable_roots=[self.outside.name])
        with self.assertRaises(ValueError):
            tools.prepare("write_file", {"path": str(Path(self.outside.name, "new.txt")),
                                         "content": "no"})
        self.assertFalse(Path(self.outside.name, "new.txt").exists())

    def test_a_user_secret_pattern_blocks_a_read_inside_the_workspace(self):
        tools = self.make(secret_patterns=[r"\.vault$"])
        with self.assertRaises(ValueError) as caught:
            tools.prepare("read_file", {"path": "prod.vault"})
        self.assertIn("Options", str(caught.exception))

    def test_the_built_in_secret_guard_still_applies_with_an_empty_policy(self):
        tools = self.make()
        with self.assertRaises(ValueError):
            tools.prepare("read_file", {"path": ".env"})

    def test_set_agent_options_reaches_the_executor_and_leaves_other_lists_alone(self):
        from relay_core import agent as agent_module
        tools = self.make(secret_patterns=[r"\.vault$"])

        class FakeAgent:
            executor = tools
            set_security = agent_module.Agent.set_security
        FakeAgent().set_security({"command_denylist": ["rm"]})
        self.assertEqual(tools.policy.command_denylist, ("rm",))
        self.assertEqual(tools.policy.secret_patterns, (r"\.vault$",))   # untouched
        self.assertIs(tools.workspace.policy, tools.policy)


if __name__ == "__main__":
    unittest.main()

"""Which actions stop and ask (card #K2FV): the capability set, the classifiers and the policy."""
import os
import tempfile
import unittest

from relay_core import approvals


def asking(*capabilities) -> approvals.Policy:
    return approvals.Policy(ask=frozenset(capabilities), chosen=True)


class ValidateTests(unittest.TestCase):
    def test_absent_keys_change_nothing(self):
        self.assertEqual(approvals.validate({}), {})

    def test_an_empty_list_is_a_value_not_an_absence(self):
        # Unticking the last box has to reach the worker as "ask for nothing", not as "unchanged".
        self.assertEqual(approvals.validate({"approvals_ask": []}), {"approvals_ask": []})

    def test_it_sorts_and_deduplicates(self):
        out = approvals.validate({"approvals_ask": ["network", "edit", "edit"]})
        self.assertEqual(out["approvals_ask"], ["edit", "network"])

    def test_an_unknown_capability_is_named_in_the_error(self):
        with self.assertRaises(ValueError) as caught:
            approvals.validate({"approvals_ask": ["edit", "sudo_everything"]})
        self.assertIn("sudo_everything", str(caught.exception))

    def test_types_are_checked(self):
        for bad in ({"approvals_ask": "edit"}, {"approvals_ask": [3]}, {"approvals_chosen": "yes"}):
            with self.assertRaises(ValueError):
                approvals.validate(bad)

    def test_there_is_no_row_for_running_a_command(self):
        # Owner, 2026-09-19: "leave it out." A card before every run_command would make Relay
        # unusable, and the two classifiers cover the commands worth stopping.
        self.assertNotIn("command", approvals.CAPABILITIES)
        with self.assertRaises(ValueError):
            approvals.validate({"approvals_ask": ["command"]})


class PolicyTests(unittest.TestCase):
    def test_allow_all_asks_for_nothing(self):
        for capability in approvals.CAPABILITIES:
            self.assertFalse(approvals.ALLOW_ALL.asks(capability), capability)

    def test_before_the_choice_is_made_the_cautious_set_applies(self):
        fresh = approvals.Policy()          # nothing chosen yet
        self.assertTrue(fresh.asks(approvals.EDIT))
        self.assertTrue(fresh.asks(approvals.DELETE_OR_MOVE))
        self.assertFalse(fresh.asks(approvals.CREATE))
        self.assertFalse(fresh.asks(approvals.NETWORK))

    def test_an_empty_policy_that_was_chosen_asks_for_nothing(self):
        # This is the difference the owner asked for: picking "allow everything" is not the same
        # state as never having been asked.
        self.assertFalse(approvals.Policy(chosen=True).asks(approvals.EDIT))

    def test_a_chosen_policy_asks_exactly_its_own_set(self):
        policy = asking(approvals.NETWORK)
        self.assertTrue(policy.asks(approvals.NETWORK))
        self.assertFalse(policy.asks(approvals.EDIT))   # even though edit is in the cautious set


class ClassifierTests(unittest.TestCase):
    def test_a_destructive_program_anywhere_in_the_line(self):
        for command in ("rm -rf build", "ls && sudo rm -rf build", "mv a b", "shred x", "dd if=a of=b"):
            self.assertIn(approvals.DELETE_OR_MOVE, approvals.command_capabilities(command), command)

    def test_an_ordinary_command_touches_nothing(self):
        for command in ("echo hi", "ls -la", "python3 -m pytest", "grep rm notes.txt"):
            self.assertEqual(approvals.command_capabilities(command), [], command)

    def test_permissions_only_count_when_recursive(self):
        self.assertIn(approvals.DELETE_OR_MOVE, approvals.command_capabilities("chmod -R 777 ."))
        self.assertEqual(approvals.command_capabilities("chmod 644 notes.txt"), [])

    def test_network_programs_and_the_git_subcommands_that_reach_out(self):
        for command in ("curl https://x", "wget x", "scp a b:", "ssh host ls", "git push origin main"):
            self.assertIn(approvals.NETWORK, approvals.command_capabilities(command), command)
        for command in ("git status", "git commit -m x", "git log"):
            self.assertEqual(approvals.command_capabilities(command), [], command)

    def test_one_line_can_touch_two_capabilities(self):
        self.assertEqual(approvals.command_capabilities("curl https://x -o y && rm y"),
                         [approvals.DELETE_OR_MOVE, approvals.NETWORK])

    def test_a_truncating_redirect_over_an_existing_file_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = os.path.join(directory, "notes.txt")
            open(existing, "w").close()
            missing = os.path.join(directory, "new.txt")
            self.assertIn(approvals.DELETE_OR_MOVE, approvals.command_capabilities(f"echo x > {existing}"))
            # Appending does not truncate, and a new file destroys nothing.
            self.assertEqual(approvals.command_capabilities(f"echo x >> {existing}"), [])
            self.assertEqual(approvals.command_capabilities(f"echo x > {missing}"), [])

    def test_a_stream_redirect_is_not_a_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = os.path.join(directory, "log")
            open(existing, "w").close()
            self.assertEqual(approvals.command_capabilities(f"run 2>&1 | tee {existing}"), [])

    def test_a_blank_or_non_string_command_touches_nothing(self):
        self.assertEqual(approvals.command_capabilities("   "), [])
        self.assertEqual(approvals.command_capabilities(None), [])


class NeededTests(unittest.TestCase):
    def test_an_edit_of_an_existing_file_against_a_create(self):
        policy = asking(approvals.EDIT)
        self.assertEqual(approvals.needed(policy, "write_file", {}, exists=True), [approvals.EDIT])
        self.assertEqual(approvals.needed(policy, "write_file", {}, exists=False), [])

    def test_a_read_only_counts_when_it_left_the_workspace(self):
        policy = asking(approvals.READ_OUTSIDE)
        self.assertEqual(approvals.needed(policy, "read_file", {}, outside_workspace=True),
                         [approvals.READ_OUTSIDE])
        self.assertEqual(approvals.needed(policy, "read_file", {}, outside_workspace=False), [])

    def test_the_terminal_and_program_tools(self):
        self.assertEqual(approvals.needed(asking(approvals.TERMINAL), "run_in_terminal", {}),
                         [approvals.TERMINAL])
        self.assertEqual(approvals.needed(asking(approvals.PROGRAM), "type_into_program", {}),
                         [approvals.PROGRAM])

    def test_a_command_asks_only_for_the_rows_that_are_ticked(self):
        command = {"command": "curl https://x -o y && rm y"}
        self.assertEqual(approvals.needed(asking(approvals.NETWORK), "run_command", command),
                         [approvals.NETWORK])
        self.assertEqual(approvals.needed(approvals.ALLOW_ALL, "run_command", command), [])

    def test_a_tool_the_checklist_does_not_cover_never_asks(self):
        self.assertEqual(approvals.needed(asking(*approvals.CAPABILITIES), "list_directory", {},
                                          outside_workspace=False), [])


class WordingTests(unittest.TestCase):
    def test_every_capability_has_a_label(self):
        for capability in approvals.CAPABILITIES:
            self.assertIn(capability, approvals.LABELS)
            header, question = approvals.prompt(capability, "src/Pane.h")
            self.assertTrue(header and question)
            self.assertIn("src/Pane.h", question)

    def test_the_refusal_tells_the_model_not_to_route_around_it(self):
        text = approvals.refusal(approvals.EDIT)
        self.assertIn("did not allow", text)
        self.assertIn("another way", text)


if __name__ == "__main__":
    unittest.main()

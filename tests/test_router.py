import os
import tempfile
import unittest
from pathlib import Path
from relay_core.router import classify, validate_input

class RouterTests(unittest.TestCase):
    def test_shell_commands(self):
        for command in ["git status", "ls -la", "cd ..", "printf '%s\\n' hello", "find . -type f -size +100M", "VAR=3", "./scripts/build.sh", "echo 'why does this fail?'", "for i in 1 2; do echo $i; done", "cat <<'EOF'\nhello\nEOF\n"]:
            with self.subTest(command=command):
                result = classify(command, path=os.environ["PATH"])
                self.assertEqual(result.route, "shell")
                self.assertTrue(result.syntax_ok, result.syntax_error)

    def test_agent_requests(self):
        for text in ["why is this build failing?", "find the largest files in this repo", "fix the failing tests", "explain the last error", "can you inspect this?", "help me debug it", "write a unit test"]:
            with self.subTest(text=text):
                self.assertEqual(classify(text).route, "agent")

    def test_invalid_commands_go_to_agent(self):
        for text in ["frobnicate", "something unclear", "hello", "flurble --version"]:
            with self.subTest(text=text):
                result = classify(text)
                self.assertEqual(result.route, "agent")
                self.assertFalse(result.valid)
                self.assertIn("command not found", result.invalid_reason)
        # A natural-language phrase that is also a live function runs as that function.
        self.assertEqual(classify("explain the build", known_commands=["explain"]).route, "shell")
        self.assertNotIn("ambiguous", {classify(t).route for t in ["zzz", "why", "ls", "don't"]})

    def test_alias_and_function(self):
        self.assertEqual(classify("gco main", known_commands=["gco"]).route, "shell")

    def test_explicit_destinations(self):
        self.assertEqual(classify("/shell flurble --help").route, "shell")
        self.assertEqual(classify("/agent git status").route, "agent")
        self.assertEqual(classify("git status", "agent").route, "agent")
        self.assertEqual(classify("why is this broken", "shell").route, "shell")

    def test_multiline_and_syntax(self):
        result = classify("echo 'unfinished", "shell")
        self.assertFalse(result.syntax_ok)
        self.assertIn("unexpected", result.syntax_error)
        self.assertEqual(classify(" ").route, "empty")
        self.assertEqual(validate_input("a\r\nb"), "a\nb")

    def test_control_character_guard(self):
        for text in ["echo\x1b[200~", "a\x00b", "a\rb", "a\x7f"]:
            with self.assertRaises(ValueError):
                classify(text)
        with self.assertRaises(ValueError):
            classify("x" * 131073)

    def test_parse_does_not_execute(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "sentinel"
            result = classify(f"echo $(touch '{path}')", "shell")
            self.assertTrue(result.syntax_ok)
            self.assertFalse(path.exists())


PATH = os.environ["PATH"]


class ValidityTests(unittest.TestCase):
    def check(self, text, **kw):
        return classify(text, path=PATH, **kw)

    def assertValid(self, text, **kw):
        result = self.check(text, **kw)
        self.assertTrue(result.valid, f"{text!r}: {result.invalid_reason}")
        self.assertEqual(result.route, "shell")
        return result

    def assertInvalid(self, text, fragment, **kw):
        result = self.check(text, **kw)
        self.assertFalse(result.valid, text)
        self.assertEqual(result.route, "agent")
        self.assertIn(fragment, result.invalid_reason)
        self.assertIn("sent to the agent", result.reason)
        return result

    def test_pipelines_and_lists_check_every_command(self):
        self.assertValid("ls | grep x | sort")
        self.assertInvalid("ls | nonexistentcmd123", "command not found: nonexistentcmd123")
        self.assertInvalid("true && nope123", "command not found: nope123")
        self.assertInvalid("true || nope123; ls", "nope123")
        self.assertInvalid("ls\nnope123", "nope123")
        self.assertInvalid("ls & nope123", "nope123")

    def test_assignments_wrappers_and_redirects(self):
        self.assertValid("FOO=1 BAR=2 ls")
        self.assertValid("VAR=3")
        self.assertValid("env FOO=1 ls")
        self.assertInvalid("env FOO=1 nope123", "nope123")
        self.assertValid("nice -n 10 ls")
        self.assertValid("time -p ls")
        self.assertValid("ls 2>&1 > /dev/null < /dev/null")
        self.assertValid("> out.txt echo hi")

    def test_subshells_groups_and_substitutions(self):
        self.assertValid("(cd /tmp && ls)")
        self.assertInvalid("(cd /tmp && nope123)", "nope123")
        self.assertValid("{ ls; }")
        self.assertValid("! grep -q x /dev/null")
        self.assertValid("echo $(ls) `pwd`")
        self.assertInvalid("echo $(nope123 x)", "nope123")
        self.assertInvalid('echo "$(nope123)"', "nope123")
        self.assertInvalid("echo `nope123`", "nope123")
        self.assertValid("echo '$(nope123)'")

    def test_compound_commands_and_functions(self):
        self.assertValid("for i in a b; do echo $i; done")
        self.assertValid("if true; then ls; fi")
        self.assertInvalid("if true; then nope123; fi", "nope123")
        self.assertValid("[[ -f x ]] && echo y")
        self.assertValid("f() { ls; }; f")
        self.assertValid("function g { ls; }; g")
        # Not modelled: trusted after bash -n, first word still checked.
        self.assertValid("x=$((1 + 2))")
        self.assertValid("cat <<'EOF'\nhello\nEOF\n")
        self.assertInvalid("nope123 <<'EOF'\nhello\nEOF\n", "nope123")
        self.assertValid('case x in x) ls;; esac')

    def test_alias_and_function_from_live_shell(self):
        self.assertInvalid("gco main", "gco")
        self.assertValid("gco main", known_commands=["gco"])
        self.assertValid("ls | ll", known_commands=["ll"])

    def test_path_words(self):
        with tempfile.TemporaryDirectory() as d:
            script = Path(d) / "run.sh"
            script.write_text("#!/bin/sh\necho hi\n")
            self.assertInvalid("./run.sh", "not executable: ./run.sh", cwd=d)
            script.chmod(0o755)
            self.assertValid("./run.sh --flag", cwd=d)
            self.assertValid(f"{script}")
            self.assertInvalid("./missing.sh", "no such file", cwd=d)
            self.assertInvalid("./", "is a directory", cwd=d)

    def test_syntax_errors(self):
        result = self.assertInvalid("don't break the build", "syntax error:")
        self.assertFalse(result.syntax_ok)
        self.assertNotIn("bash: line", result.invalid_reason)

    def test_natural_language_still_agent(self):
        for text in ["why is this build failing?", "find the largest files in this repo"]:
            result = self.check(text)
            self.assertEqual(result.route, "agent")
            self.assertTrue(result.valid)
            self.assertEqual(result.invalid_reason, "")

    def test_sentence_naming_a_path_or_glob_is_not_a_command(self):
        # Owner report 2026-09-17: "look" is an installed command, and the glob used to suppress
        # the ambiguity check entirely, so this ran in the shell.
        result = self.check("look at some of my other letters in ~/admin/Advisees.*.docx for my writing style")
        self.assertEqual(result.route, "agent")
        self.assertTrue(result.needs_assist)
        # Real commands with globs and paths stay in the shell.
        for text in ["ls *.py", "grep -r foo src/*.cpp", "cat ~/.bashrc"]:
            self.assertEqual(self.check(text).route, "shell", text)

    def test_shell_mode_always_shell_but_reports_validity(self):
        result = classify("nope123 --now", "shell", path=PATH)
        self.assertEqual(result.route, "shell")
        self.assertFalse(result.valid)
        self.assertIn("nope123", result.invalid_reason)
        self.assertIn("agent will fix", result.reason)
        ok = classify("ls -la", "shell", path=PATH)
        self.assertEqual((ok.route, ok.valid), ("shell", True))
        self.assertEqual(classify("/shell nope123").route, "shell")
        self.assertFalse(classify("/shell nope123").valid)
        agent = classify("ls", "agent")
        self.assertEqual((agent.route, agent.valid), ("agent", True))

    def test_decision_dict_has_validity_fields(self):
        d = self.check("nope123").to_dict()
        self.assertEqual(d["valid"], False)
        self.assertIn("nope123", d["invalid_reason"])

    def test_validity_check_never_executes(self):
        with tempfile.TemporaryDirectory() as d:
            sentinel = Path(d) / "sentinel"
            for text in [f"touch '{sentinel}'", f"echo $(touch '{sentinel}')", f"echo `touch '{sentinel}'`",
                         f"nope123 && touch '{sentinel}'", f"(touch '{sentinel}')"]:
                for mode in ("auto", "shell"):
                    classify(text, mode, path=PATH, cwd=d)
            self.assertFalse(sentinel.exists())


if __name__ == '__main__':
    unittest.main()

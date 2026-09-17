import os
import tempfile
import unittest
from pathlib import Path
from relay_core.router import classify, validate_input

class RouterTests(unittest.TestCase):
    def test_shell_commands(self):
        for command in ["git status", "ls -la", "cd ..", "printf '%s\\n' hello", "find . -type f -size +100M", "VAR=3", "./build.sh", "echo 'why does this fail?'", "for i in 1 2; do echo $i; done", "cat <<'EOF'\nhello\nEOF\n"]:
            with self.subTest(command=command):
                result = classify(command, path=os.environ["PATH"])
                self.assertEqual(result.route, "shell")
                self.assertTrue(result.syntax_ok, result.syntax_error)

    def test_agent_requests(self):
        for text in ["why is this build failing?", "find the largest files in this repo", "fix the failing tests", "explain the last error", "can you inspect this?", "help me debug it", "write a unit test"]:
            with self.subTest(text=text):
                self.assertEqual(classify(text).route, "agent")

    def test_ambiguity(self):
        for text in ["frobnicate", "something unclear", "hello", "flurble --version"]:
            self.assertEqual(classify(text).route, "ambiguous")
        self.assertEqual(classify("explain the build", known_commands=["explain"]).route, "ambiguous")

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

if __name__ == '__main__': unittest.main()


class AmbiguousSyntaxTests(unittest.TestCase):
    def test_ambiguous_reports_bash_syntax(self):
        valid = classify("frobnicate the widgets")
        self.assertEqual(valid.route, "ambiguous")
        self.assertTrue(valid.syntax_ok)
        invalid = classify("don't break the build")
        self.assertEqual(invalid.route, "ambiguous")
        self.assertFalse(invalid.syntax_ok)

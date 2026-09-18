import os
import tempfile
import unittest
from pathlib import Path
from relay_core.router import (ASSIST_THRESHOLD, BARE_WORD_ODD, ENGLISH_COMMANDS, LITERAL_TEXT,
                               _one_edit_apart,
                               SIGNAL_WORDS, assist_signals, classify, validate_input)

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

    def test_arithmetic_and_bare_globs_are_not_commands(self):
        # Owner report 2026-09-18: "35*30" ran in the shell because a glob in command position was
        # treated as un-decidable; bare arithmetic and a lone glob belong to the agent.
        self.assertInvalid("35*30", "command not found: 35*30")
        self.assertInvalid("35 * 30", "command not found: 35")
        self.assertInvalid("*", "command not found: *")
        # A letter-bearing glob in command position stays un-decidable, so still runnable.
        self.assertValid("p* --version")

    def test_a_lone_loop_builtin_is_an_agent_prompt(self):
        # Owner report 2026-09-18: `continue` typed on its own went to the shell, which answered
        # "continue: only meaningful in a `for', `while', or `until' loop". It is a bash builtin,
        # so the router called it runnable; but nothing it could do at a prompt is what was meant.
        for text in ["continue", "break", "return", "  continue  "]:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertTrue(result.agent_signal, text)
                self.assertFalse(result.explain_invalid, text)
        # Inside a real loop, and with anything after it, the word is shell again.
        self.assertValid("for f in *; do continue; done")
        self.assertValid("continue 2")
        # An explicit terminal destination still runs it.
        self.assertEqual(classify("continue", "shell", path=PATH).route, "shell")
        # Terminal mode flags it as belonging to the agent (the wrong-mode hint).
        self.assertTrue(classify("continue", "shell", path=PATH).agent_signal)

    def test_only_a_mistyped_command_explains_itself(self):
        # Owner reports 2026-09-18: "symlink from ~/projects to here" and a lone "resume" were both
        # answered by the agent with "command not found: ..." printed under them, which reads as a
        # failed command. The note is for a command that was meant and mistyped, so it needs
        # evidence of that: shell syntax, flags, a name that is not an English word, or a word one
        # slip away from a command this machine has.
        for text in ["symlink from ~/projects to here", "resume", "again", "status", "deploy",
                     "add a note about this", "tell ryan about the meeting", "resume the meeting notes",
                     "35 * 30"]:
            result = self.check(text)
            self.assertEqual(result.route, "agent", text)
            self.assertFalse(result.explain_invalid, text)
        for text in ["nonexistentcmd123", "gti status", "docekr ps -a", "pyton script.py",
                     "ls | nonexistentcmd123", "lls"]:
            result = self.check(text)
            self.assertEqual(result.route, "agent", text)
            self.assertTrue(result.explain_invalid, text)
        # A syntax error is about a command, so it is explained — unless the only "quote" in the
        # line is an apostrophe inside a word (card #W954, below).
        self.assertTrue(self.check("echo 'unfinished").explain_invalid)

    def test_sentence_punctuation_is_not_a_mistyped_command(self):
        # Card #W954, owner report 2026-09-18: this went to the agent correctly with
        # "command not found: yeah," under it — the comma made "yeah," look like a name, and #T4JV
        # counted a name that is not a plain lowercase word as evidence a command was meant.
        owner = ("yeah, see if there is a clear issue to resolve. if not, lets unblock and start "
                 "backfilling at full capacity")
        prose = [owner, "yeah,", "yeah.", "ok.", "ok, go ahead", "okay: do it", "done!", "thanks!",
                 "great, thanks", "nope, try again", "right...", "wait... what", "hmm…", "yes!",
                 # capitalised, as phones and habit write it
                 "Yeah, see if there is a clear issue", "Yeah", "Sure", "Sounds good!", "Continue",
                 "Resume", "Great, thanks.",
                 # apostrophes: bash reads a lone one as an unterminated string
                 "let's unblock and start backfilling", "don't break the build", "it's broken",
                 "Let's go", "it's fine, isn't it", "don’t break it",
                 # dashes, and quotes that are not shell quotes
                 "yeah—do it", "yeah - do it", "ok -- do it", "“yeah” works",
                 # one-word replies that sit one edit from a command (ok/od, no/nl, cool/col)
                 "ok", "no", "good", "fine", "cool", "nope", "yep", "hi", "lets go", "ok go"]
        for text in prose:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertFalse(result.explain_invalid, f"{text!r}: {result.invalid_reason}")
        # Real slips keep their note, capitalised or not.
        for text in ["gti status", "pyton -m x", "ls -la | grpe x", "Gti status", "GTI status",
                     "Docker ps", "Ls", "gti stauts.", "gti --", "kubectl2 get pods",
                     "echo 'unfinished", 'echo "unfinished']:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertTrue(result.explain_invalid, text)
        with tempfile.TemporaryDirectory() as d:
            result = self.check("./run.sh", cwd=d)
            self.assertEqual((result.route, result.explain_invalid), ("agent", True))

    def test_a_question_word_is_not_a_glob(self):
        # Card #W954: "really?" and "ready?" were read as a glob in command position, which the
        # router cannot judge, so they ran in the shell. A `?` glob still counts inside a real one.
        for text in ["really?", "ready?", "hmm?", "really?!"]:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertFalse(result.explain_invalid, text)
        self.assertValid("p* --version")
        self.assertValid("ls -d /us?")

    def test_a_lone_yes_is_a_reply(self):
        # Card #W954: a lone "yes" ran `yes`, which prints y until interrupted.
        # A lone "wait", "true" or "false" ran a builtin that does nothing visible; a lone "done"
        # printed a bash syntax error under the agent's answer.
        for text in ["yes", "nice", "wait", "true", "false", "done", " wait "]:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertTrue(result.agent_signal, text)
                self.assertFalse(result.explain_invalid, text)
                self.assertIn("on its own is a reply", result.reason)
        for text in ["yes | head -3", "nice -n 10 ls", "wait %1", "wait $pid", "true && ls", "false || ls"]:
            self.assertValid(text)
        # Commands that do something on their own stay in the shell.
        for text in ["ls", "pwd", "clear", "history", "jobs", "times"]:
            with self.subTest(text=text):
                self.assertEqual(self.check(text).route, "shell", text)
        self.assertEqual(classify("wait", "shell", path=PATH).route, "shell")

    def test_a_semicolon_in_a_sentence_is_not_a_command(self):
        # Card #W954: "hmm; not sure" went to the agent with "command not found: hmm" under it. A
        # `;` is shell syntax, but the note needs some part of the line to look like a command.
        for text in ["hmm; not sure", "hmm;", "ok; thanks", "no; the other one", "nope; try again",
                     "ok; let me think", "hmm; don't know", "Hmm; no idea", "sure; ship it", "ok ; sure"]:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertFalse(result.explain_invalid, f"{text!r}: {result.invalid_reason}")
        # A part that starts with a real command, a slip of one, flags or a name keeps the note:
        # "hmm; ls" would have run `ls`, so the line is a command with a stray word before it.
        for text in ["hmm; ls", "hmm;ls", "gti; ls", "cd /tmp; mkae", "ok; Docker ps",
                     "hmm; not sure -v", "yeah; kubectl2 get", "ok; pyton script.py", "xyzzy; frob"]:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertTrue(result.explain_invalid, text)
        self.assertValid("echo hi; ls")

    def test_a_quoted_first_word_is_a_quotation(self):
        # Card #W954: '"yeah" is fine' printed "command not found: yeah". A quoted plain word is
        # judged as the unquoted line would be.
        for text in ['"yeah" is fine', "'ok' then", '"yeah", sure', '"Sure" works']:
            with self.subTest(text=text):
                result = self.check(text)
                self.assertEqual(result.route, "agent", text)
                self.assertFalse(result.explain_invalid, f"{text!r}: {result.invalid_reason}")
        self.assertValid('"ls" -la')
        for text in ['"gti" status', '"pip4" install x', '"yeah" is "fine"']:
            with self.subTest(text=text):
                self.assertTrue(self.check(text).explain_invalid, text)
        with tempfile.TemporaryDirectory() as d:
            result = self.check('"./run.sh"', cwd=d)
            self.assertEqual((result.route, result.explain_invalid), ("agent", True))

    def test_one_edit_apart(self):
        for typed, command in [("gti", "git"), ("pyton", "python"), ("docekr", "docker"),
                               ("lls", "ls"), ("gi", "git"), ("gitt", "git")]:
            self.assertTrue(_one_edit_apart(typed, command), (typed, command))
        for typed, command in [("resume", "resize"), ("status", "stat"), ("git", "git"),
                               ("deploy", "dpkg"), ("notes", "node")]:
            self.assertFalse(_one_edit_apart(typed, command), (typed, command))

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

    def test_a_command_word_followed_by_a_preposition_is_a_sentence(self):
        # Owner report 2026-09-17: "look for cleanup opportunities" ran in the shell. No command
        # takes "for", "at" or "the" as its first operand.
        for text in ["look for cleanup opportunities", "search for the leak", "check on the build",
                     "look up the error", "look at the tests"]:
            self.assertEqual(self.check(text).route, "agent", text)
        # Real invocations whose first operand is not a lead-in word stay in the shell.
        for text in ["tar xzf archive.tgz", "split file.txt", "watch -n1 ls", "time make", "make -j8"]:
            self.assertEqual(self.check(text).route, "shell", text)

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
                for mode in ("auto", "shell", "agent"):
                    classify(text, mode, path=PATH, cwd=d)
            self.assertFalse(sentinel.exists())


class WrongModeSignalTests(unittest.TestCase):
    """agent_signal and agent-mode validity: the wrong-mode hints (2026-09-17).

    Terminal mode flags text that clearly belongs to the agent; agent mode reports whether
    the text is a runnable command so the GUI can suggest the terminal when it fails."""

    def test_terminal_mode_requests_signal(self):
        # A NATURAL prefix that is no live alias, or runnable input that reads like a sentence.
        for text in ["why does this fail", "explain the last error", "please sort the output",
                     "find the largest files in this repo", "sort these results by date"]:
            with self.subTest(text=text):
                self.assertTrue(classify(text, "shell", path=PATH).agent_signal, text)

    def test_terminal_mode_commands_and_typos_do_not_signal(self):
        for text in ["ls -la", "git status", "make -j", "nope123 --now", "frobnicate",
                     "shutdown now", "make all", "echo 'why does this fail?'"]:
            with self.subTest(text=text):
                self.assertFalse(classify(text, "shell", path=PATH).agent_signal, text)

    def test_live_alias_named_like_a_word_is_not_a_signal(self):
        result = classify("explain the build", "shell", known_commands=["explain"], path=PATH)
        self.assertFalse(result.agent_signal)
        self.assertTrue(result.valid)

    def test_agent_mode_reports_runnability(self):
        command = classify("git stauts", "agent", path=PATH)
        self.assertEqual((command.route, command.valid, command.agent_signal), ("agent", True, False))
        request = classify("why does this fail", "agent", path=PATH)
        self.assertEqual(request.route, "agent")
        self.assertFalse(request.valid)
        self.assertTrue(request.agent_signal)
        self.assertIn("command not found", request.invalid_reason)
        typo = classify("nope123 x", "agent", path=PATH)
        self.assertFalse(typo.valid)
        self.assertFalse(typo.agent_signal)

    def test_signal_is_in_the_decision_dict(self):
        d = classify("explain this error", "shell", path=PATH).to_dict()
        self.assertTrue(d["agent_signal"])
        self.assertTrue(d["valid"] is False)


# Words that are both installed commands and ordinary English, paired as a request and as the real
# command (2026-09-17 review; derivation in docs/qa_evidence/2026-09-17-router-english-commands/).
# Before the review this table scored 70/99; the words below that were missing from ENGLISH_COMMANDS
# all read as shell commands.
ROUTING_TABLE = [
    # --- English sentences: the agent must see these -------------------------------------------
    ("look at some of my other letters in ~/admin/Advisees.*.docx for my writing style", "agent"),
    ("look for typos in the readme", "agent"),
    ("watch the logs for errors", "agent"),
    ("time the build and tell me what is slow", "agent"),
    ("split the file by lines", "agent"),
    ("find the config that sets the port", "agent"),
    ("make the tests pass", "agent"),
    ("test the new parser on my sample", "agent"),
    ("sort these results by date", "agent"),
    ("cut the preamble from the output", "agent"),
    ("join the two csv files on the id column", "agent"),
    ("install ripgrep", "agent"),
    ("open the settings dialog", "agent"),
    ("touch up the styling in the header", "agent"),
    ("just run the tests again", "agent"),
    ("from the logs tell me what failed", "agent"),
    ("route the request through the proxy", "agent"),
    ("at the top of the file add a comment", "agent"),
    ("prove that the fix works", "agent"),
    ("tidy up the imports in this module", "agent"),
    ("dump the table to a csv", "agent"),
    ("restore the backup from yesterday", "agent"),
    ("suspend the running job right away", "agent"),
    ("truncate the log file to zero", "agent"),
    ("shred the old key files", "agent"),
    ("sum the values in the second column", "agent"),
    ("cancel the pending print job", "agent"),
    ("eject the usb drive", "agent"),
    ("browse the docs for this api", "agent"),
    ("bind the shortcut to a new tab", "agent"),
    ("prune the branches that are merged", "agent"),
    ("transform the csv into json", "agent"),
    ("zip up the build output", "agent"),
    ("accept the changes in that patch", "agent"),
    ("reject the parts that are unrelated", "agent"),
    ("disable the pre commit hook", "agent"),
    ("spell check the readme for me", "agent"),
    ("talk to the api and summarise the reply", "agent"),
    ("pass the output to the next step", "agent"),
    ("sample the first thousand rows", "agent"),
    ("resume the download where it stopped", "agent"),
    ("batch the requests into groups of ten", "agent"),
    ("bundle the assets for production", "agent"),
    ("jot down the steps you took", "agent"),
    ("wipe the build directory", "agent"),
    ("tree the whole repo and summarise it", "agent"),
    ("go to the end of the file", "agent"),
    ("head over to the docs and check the api", "agent"),
    ("patch the config so it uses the new port", "agent"),
    ("log the output of every request", "agent"),
    ("play the recording of that session", "agent"),
    ("view the diff for this commit", "agent"),
    ("column the output so it lines up", "agent"),
    ("last time this worked it was on main", "agent"),
    # --- The same words as real commands: the shell must keep these ----------------------------
    ("ls", "shell"),
    ("ls -la", "shell"),
    ("git status", "shell"),
    ("make -j", "shell"),
    ("make test", "shell"),
    ("find . -name x", "shell"),
    ("grep -rn TODO src", "shell"),
    ("watch -n1 ls", "shell"),
    ("time make", "shell"),
    ("time git push", "shell"),
    ("split file.txt", "shell"),
    ("sort -u words.txt", "shell"),
    ("head -n 20 log.txt", "shell"),
    ("cut -d: -f1 /etc/passwd", "shell"),
    ("join a.txt b.txt", "shell"),
    ("touch newfile.txt", "shell"),
    ("date", "shell"),
    ("tar -czf out.tar.gz dir", "shell"),
    ("install -m 755 a b", "shell"),
    ("open .", "shell"),
    ("zip -r out.zip dir", "shell"),
    ("unzip archive.zip", "shell"),
    ("truncate -s 0 app.log", "shell"),
    ("route -n", "shell"),
    ("at 5pm", "shell"),
    ("just build", "shell"),
    ("prove t/basic.t", "shell"),
    ("look ante", "shell"),
    ("last reboot", "shell"),
    ("test -f README.md", "shell"),
    ("yes | head -3", "shell"),
    ("top -b -n1", "shell"),
    ("free -h", "shell"),
    ("file README.md", "shell"),
    ("which python3", "shell"),
    ("sum data.bin", "shell"),
    ("tree -L 2", "shell"),
    ("go build ./...", "shell"),
    ("say hello world", "shell"),
    ("banner release", "shell"),
    ("column -t data.tsv", "shell"),
    ("dump 0uf /dev/nst0 /home", "shell"),
    ("bundle exec rspec", "shell"),
    ("disable printer1", "shell"),
    ("shred -u secret.key", "shell"),
]
# Table words that are not installed on this machine but exist on macOS, BSD, other distributions or
# in common toolchains. Passed as known_commands so the table tests routing, not the local package set.
SIMULATED_COMMANDS = ["just", "tree", "go", "say", "tidy", "dump", "restore", "wipe", "spell",
                      "accept", "reject", "disable", "talk", "pass", "sample", "resume", "batch",
                      "bundle", "jot", "at", "banner", "play", "log"]


class EnglishWordCommandTests(unittest.TestCase):
    """Commands that are also ordinary English words, as sentences and as commands."""

    def routes(self):
        with tempfile.TemporaryDirectory() as cwd:
            for text, expected in ROUTING_TABLE:
                yield text, expected, classify(text, known_commands=SIMULATED_COMMANDS, path=PATH, cwd=cwd)

    def test_table_routes_correctly(self):
        wrong = [f"{text!r}: want {expected}, got {result.route} ({result.reason})"
                 for text, expected, result in self.routes() if result.route != expected]
        self.assertEqual(wrong, [], f"{len(wrong)}/{len(ROUTING_TABLE)} misrouted:\n" + "\n".join(wrong))

    def test_ambiguous_sentences_ask_instead_of_guessing_silently(self):
        # A sentence that would also run as a command must set needs_assist, so the model decides.
        for text, expected, result in self.routes():
            if expected == "agent" and result.valid:
                self.assertTrue(result.needs_assist, f"{text!r} guessed agent without asking")
                self.assertTrue(result.assist_reason, text)

    def test_plain_commands_never_ask(self):
        for text, expected, result in self.routes():
            if expected == "shell":
                self.assertFalse(result.needs_assist, f"{text!r} would ask the model")

    def test_word_lists_are_lowercase_and_deduplicated(self):
        for name, words in (("ENGLISH_COMMANDS", ENGLISH_COMMANDS), ("SIGNAL_WORDS", SIGNAL_WORDS),
                            ("BARE_WORD_ODD", BARE_WORD_ODD), ("LITERAL_TEXT", LITERAL_TEXT)):
            for word in words:
                self.assertEqual(word, word.lower(), f"{name}: {word!r}")
                self.assertTrue(word.isalpha() or "'" in word, f"{name}: {word!r}")
        self.assertTrue(BARE_WORD_ODD <= ENGLISH_COMMANDS, BARE_WORD_ODD - ENGLISH_COMMANDS)
        self.assertTrue(LITERAL_TEXT <= ENGLISH_COMMANDS, LITERAL_TEXT - ENGLISH_COMMANDS)

    def test_ordinary_command_shapes_never_reach_the_assist_threshold(self):
        # Every English-word command with ordinary arguments must score below the threshold.
        shapes = ["", " -h", " --help", " -v", " file.txt", " /etc/passwd", " src/main.py", " out.log",
                  " a.txt b.txt", " -n 5 log.txt", " 2", " x.o", " ./run.sh", " data/in.csv data/out.csv"]
        with tempfile.TemporaryDirectory() as cwd:
            asked = [word + shape for word in sorted(ENGLISH_COMMANDS) for shape in shapes
                     if assist_signals(word + shape, cwd)[0] >= ASSIST_THRESHOLD]
        self.assertEqual(asked, [])

    def test_one_weak_signal_word_is_not_enough(self):
        with tempfile.TemporaryDirectory() as cwd:
            for text in ["shutdown now", "make all", "watch more", "last back", "time again", "test out"]:
                score, reasons, _ = assist_signals(text, cwd)
                self.assertLess(score, ASSIST_THRESHOLD, f"{text}: {reasons}")


if __name__ == '__main__':
    unittest.main()

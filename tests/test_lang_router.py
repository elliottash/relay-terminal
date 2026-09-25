"""Language-aware routing for Python, IPython and Stata REPLs and workspaces (#33G0, #MEPR)."""
import unittest

from relay_core import lang_router
from relay_core.lang_router import classify_line, detect_repl, stata_command

P, A, S, I = "program", "agent", "shell", "incomplete"

# (text, destination) in auto mode, no names known. Each row is a line a person might type at the
# REPL; the comment says what makes it ambiguous when it is.
PYTHON = [
    ("x = 1", P),
    ('print("hi")', P),
    ("print('why is this negative?')", P),          # prose inside a string is still code
    ("df.groupby('a').mean()", P),
    ("import numpy as np", P),                       # all words, but `import` makes it code
    ("from os import path", P),
    ("del x", P),
    ("pass", P),
    ("assert ok", P),
    ("2 + 2", P),
    ("[i * i for i in range(3)]", P),
    ("x", P),                                        # a bare name: the REPL shows it
    ("a", P),                                        # single letters are names, not articles
    ("df", P),
    ("help", P),                                     # builtin: the REPL answers
    ("help(len)", P),
    ("what = 3", P),                                 # NATURAL's first word, but an assignment
    ("debug(x)", P),
    ("# a comment", P),
    ("def f():\n    return 1", P),                   # a complete block
    ("for i in range(3):\n    print(i)", P),
    ("if x:\n    y = 1\nelse:\n    y = 2", P),
    ("    x = 1\n    y = 2", P),                     # a pasted indented block is dedented
    ("def f():", I),
    ("class A:", I),
    ("x = (1,", I),
    ("x = [1,\n 2,", I),
    ('s = """abc', I),
    ("@decorator", I),
    ("if x:\n    pass\nelse:", I),
    ("print('a'", I),
    ("why is the coefficient negative", A),
    ("why is the coefficient negative?", A),
    ("explain", A),                                  # parses as a name, but NATURAL
    ("summarize the regression", A),
    ("how do I merge these dataframes", A),
    ("can you plot x against y", A),
    ("hello", A),                                    # parses as a name; a greeting
    ("thanks", A),
    ("ok", A),
    ("yes", A),
    ("continue", A),                                 # LOOP_ONLY
    ("not done", A),                                 # parses: `not done`; reads as prose
    ("yes or no", A),                                # parses: `yes or no`
    ("ok, thanks", A),                               # parses as a tuple
    ("hello world", A),
    ("list the files", A),
    ("check my provider (glm", A),                   # words, then an open bracket
    ("Can you look at this:\nx = foo(", A),          # prose first line, code after
    ("print hello world", A),                        # not Python 3; the agent can say why
    ("foo bar", A),                                  # not Python and not prose: agent explains
    ("%timeit f()", A),                              # magics are IPython only
    ("df?", A),
    ("!ls", A),                                      # plain Python has no shell escape
    ("*a, b = xs", P),                               # pasted: starred assignment
]

IPYTHON = [
    ("%timeit f()", P),
    ("%matplotlib inline", P),
    ("%%bash\nls -l\necho done", P),                 # cell magic owns its body
    ("%%writefile x.txt\nwhy is this here", P),
    ("df?", P),
    ("df??", P),
    ("?df", P),
    ("np.mean?", P),
    ("np.*load*?", P),
    ("print?", P),
    ("files = !ls", P),
    ("for f in files:\n    !echo {f}", P),           # `!` inside a block is IPython's
    ("x = 1\n%time f()", P),
    ("cd data", P),                                  # automagic
    ("pwd", P),
    ("ls", P),
    ("pip install pandas", P),
    ("run model.py", P),
    ("x = 1", P),
    ("def f():", I),
    ("really?", A),                                  # a question, not help on `really`
    ("why?", A),
    ("done?", A),
    ("what??", A),
    ("run the tests", A),                            # automagic word in a sentence
    ("cd into the data folder", A),
    ("why is the coefficient negative", A),
    ("hello", A),
    ("!ls", P),                                      # pasted: IPython's shell escape
    ("!!ls", P),                                     # pasted: IPython's list-returning escape
]

STATA = [
    ("regress price mpg weight", P),
    ("reg price mpg, robust", P),
    ("reg y x, vce(cluster id)", P),
    ("su price", P),
    ("sum price, detail", P),
    ("summarize", P),
    ("summarize price mpg", P),                      # NATURAL's `summarize`, but a varlist
    ("gen lnp = ln(price)", P),
    ("g x = 1", P),
    ("egen m = mean(x), by(id)", P),
    ("replace x = 0 if missing(x)", P),
    ("tab foreign", P),
    ("tab us", P),                                   # `us` is a common variable name
    ("tab a b", P),                                  # single letters are variable names
    ("di 2+2", P),
    ('display "why is this negative?"', P),          # prose in a string is still code
    ("list", P),                                     # bare `list` lists the data
    ("list make price in 1/5", P),
    ("l make in 1/3", P),
    ("describe", P),
    ("d", P),
    ("help", P),
    ("help regress", P),
    ("h regress", P),
    ("set more off", P),                             # English keywords, still code
    ("clear all", P),
    ("sysuse auto, clear", P),
    ("use auto", P),
    ("use http://www.stata-press.com/data/r18/auto", P),   # `//` in a URL is not a comment
    ("webuse nlswork", P),
    ("capture drop z", P),
    ("cap noi reg y x", P),
    ("quietly: summarize price", P),
    ("qui reg y x", P),
    ("by foreign: summarize price", P),
    ("bysort id (year): gen lag = x[_n-1]", P),
    ("bys id: egen t = total(x)", P),
    ("svy: mean income", P),
    ("xi: reg y i.g", P),
    ("version 16: reg y x", P),
    ("estimates store m1", P),
    ("est tab m1 m2", P),
    ("esttab m1 m2 using t.tex, replace", P),
    ("reghdfe y x, absorb(id year)", P),
    ("local k = 5", P),
    ("global controls x1 x2", P),
    ("`cmd' price", P),                              # a macro names the command
    ("$analysis", P),
    ("// just a comment", P),
    ("/* block */ reg y x", P),
    ("reg y x // trailing comment", P),
    ("reg y x1 ///\n  x2 x3, robust", P),            # continuation joined
    ("* comment line\nreg y x", P),                  # `*` below the first line is Stata's
    ("foreach v of varlist x y {\n  summarize `v'\n}", P),
    ("forvalues i = 1/3 {\n  di `i'\n}", P),
    ("program define hi\n  display \"hi\"\nend", P),
    ("mata:\nx = 1\nend", P),
    ("mata: x = 1", P),
    ("#delimit ;\nreg y x\n  , robust;\n#delimit cr\nsu y", P),
    ("!ls", P),                                      # pasted: Stata's own shell escape
    ("* pasted header comment\nsu x", P),           # pasted: a `*` comment, not Relay's prefix
    ("reg y x ///", I),
    ("forvalues i = 1/3 {", I),
    ("program define hi\n  display 1", I),
    ("mata:\nx = 1", I),
    ("/* still open", I),
    ("#delimit ;\nreg y x", I),
    ("list the files", A),                           # `list` is a command; this is English
    ("list all the variables", A),
    ("summarize the results", A),
    ("describe this dataset", A),
    ("help me", A),
    ("help me with this regression", A),
    ("use my data", A),
    ("sort by price", A),
    ("do it", A),
    ("run the model again", A),
    ("why is the coefficient negative", A),
    ("what does xtreg do?", A),
    ("thanks", A),
    ("ok", A),
    ("continue", A),
    ("Describe the data", A),                        # Stata commands are lower case
    ("x = 5", A),                                    # not a Stata statement
    ("2+2", A),
    ("regressions are hard", A),                     # not an abbreviation of regress
    ("foo x y", A),                                  # unknown command: conservative
    ("by foreign summarize price", A),               # `by` without its colon
]


class TableTest(unittest.TestCase):
    def check(self, language, rows):
        for text, expected in rows:
            with self.subTest(language=language, text=text):
                decision = classify_line(text, language)
                self.assertEqual(expected, decision.destination, decision.reason)
                self.assertEqual(language, decision.language)
                self.assertFalse(decision.forced)

    def test_python(self):
        self.check("python", PYTHON)

    def test_ipython(self):
        self.check("ipython", IPYTHON)

    def test_stata(self):
        self.check("stata", STATA)


class NamesTest(unittest.TestCase):
    def test_a_defined_name_is_code_even_when_it_is_a_word(self):
        self.assertEqual(A, classify_line("done", "python").destination)
        self.assertEqual(P, classify_line("done", "python", names=["done"]).destination)
        self.assertEqual(A, classify_line("hello", "python").destination)
        self.assertEqual(P, classify_line("hello", "python", names={"hello"}).destination)

    def test_bare_word_expressions_run_only_over_defined_names(self):
        self.assertEqual(A, classify_line("x, y", "python").destination)
        self.assertEqual(P, classify_line("x, y", "python", names=["x", "y"]).destination)
        self.assertEqual(P, classify_line("not done", "python", names=["done"]).destination)

    def test_ipython_help_on_a_defined_word(self):
        self.assertEqual(A, classify_line("really?", "ipython").destination)
        self.assertEqual(P, classify_line("really?", "ipython", names=["really"]).destination)

    def test_user_stata_commands(self):
        self.assertEqual(A, classify_line("mycmd y x", "stata").destination)
        self.assertEqual(P, classify_line("mycmd y x", "stata", known_commands=["mycmd"]).destination)


class PrefixTest(unittest.TestCase):
    """Relay's `!`/`*` first, the program's own character second (module docstring)."""

    def test_typed_prefix_forces_shell_or_agent(self):
        # The composer consumed the typed `!`/`*` and says so with the mode.
        for language in lang_router.LANGUAGES:
            with self.subTest(language=language):
                shell = classify_line("ls -l", language, mode="shell")
                self.assertEqual((S, "ls -l", True), (shell.destination, shell.normalized_text, shell.forced))
                agent = classify_line("reg y x", language, mode="agent")
                self.assertEqual((A, "reg y x", True), (agent.destination, agent.normalized_text, agent.forced))
                self.assertEqual(A, classify_line("x = 1", language, mode="agent").destination)

    def test_doubled_bang_is_the_programs_shell_escape(self):
        # Typed `!!ls`: the first `!` became the chip, the second arrives as text.
        for language in lang_router.LANGUAGES:
            with self.subTest(language=language):
                decision = classify_line("!ls -l", language, mode="shell")
                self.assertEqual((P, "!ls -l", True),
                                 (decision.destination, decision.normalized_text, decision.forced))

    def test_doubled_star_is_the_programs_star(self):
        decision = classify_line("* a Stata comment", "stata", mode="agent")
        self.assertEqual((P, "* a Stata comment", True),
                         (decision.destination, decision.normalized_text, decision.forced))
        starred = classify_line("*a, b = xs", "python", mode="agent")
        self.assertEqual((P, "*a, b = xs"), (starred.destination, starred.normalized_text))

    def test_slash_prefixes(self):
        self.assertEqual(S, classify_line("/shell ls", "python").destination)
        self.assertEqual((A, "x = 1"), (classify_line("/agent x = 1", "stata").destination,
                                        classify_line("/agent x = 1", "stata").normalized_text))

    def test_prefix_only_counts_at_the_start(self):
        # `!` inside a block is IPython's; `*` below the first line is a Stata comment.
        self.assertEqual(P, classify_line("x = 1\n!ls", "ipython").destination)
        self.assertEqual(P, classify_line("su x\n* note", "stata").destination)

    def test_empty(self):
        self.assertEqual("empty", classify_line("   ", "python").destination)
        self.assertEqual("empty", classify_line("/shell  ", "stata").destination)
        self.assertEqual("empty", classify_line("", "stata", mode="shell").destination)


class NormalizationTest(unittest.TestCase):
    def test_pasted_block_is_dedented_and_trimmed(self):
        decision = classify_line("\n    x = 1\n    y = 2   \n\n", "python")
        self.assertEqual("x = 1\ny = 2", decision.normalized_text)

    def test_syntax_error_is_reported_for_the_agent(self):
        decision = classify_line("foo bar", "python")
        self.assertIn("invalid syntax", decision.syntax_error)
        self.assertEqual("", classify_line("x = 1", "python").syntax_error)

    def test_to_dict(self):
        data = classify_line("x = 1", "python").to_dict()
        self.assertEqual({"destination", "reason", "normalized_text", "language", "syntax_error", "forced"},
                         set(data))

    def test_rejects_unknown_language_mode_and_control_characters(self):
        with self.assertRaises(ValueError):
            classify_line("x", "R")
        with self.assertRaises(ValueError):
            classify_line("x", "python", mode="kernel")
        with self.assertRaises(ValueError):
            classify_line("x\x1b[A", "python")

    def test_uses_the_bash_routers_natural_language_pattern(self):
        # Imported, not copied: a change to router.NATURAL reaches every language.
        from relay_core import router
        self.assertIs(router.NATURAL, lang_router.NATURAL)


class StataCommandTest(unittest.TestCase):
    def test_abbreviations(self):
        for word, command in [("reg", "regress"), ("regr", "regress"), ("su", "summarize"),
                              ("summ", "summarize"), ("ta", "tabulate"), ("tab", "tabulate"),
                              ("di", "di"), ("dis", "display"), ("g", "generate"), ("gen", "generate"),
                              ("d", "describe"), ("des", "describe"), ("l", "list"), ("h", "help"),
                              ("cap", "capture"), ("qui", "quietly"), ("noi", "noisily"),
                              ("loc", "local"), ("gl", "global"), ("forv", "forvalues"),
                              ("bys", "bysort"), ("est", "estimates"), ("ren", "rename"),
                              ("hist", "histogram"), ("tw", "twoway"), ("sc", "scatter")]:
            with self.subTest(word=word):
                self.assertEqual(command, stata_command(word))

    def test_too_short_or_not_a_prefix(self):
        for word in ("re", "s", "q", "ca", "his", "regression", "Regress", "summarise", "lis t"):
            with self.subTest(word=word):
                self.assertEqual("", stata_command(word))


class DetectReplTest(unittest.TestCase):
    CASES = [
        (["python"], "python"),
        (["python3"], "python"),
        (["/usr/bin/python3.12"], "python"),
        ("python3.11", "python"),
        (["python3", "-u"], "python"),
        (["python", "-i", "script.py"], "python"),
        (["python", "script.py"], None),
        (["python", "-c", "print(1)"], None),
        (["python", "-m", "http.server"], None),
        (["python", "-m", "IPython"], "ipython"),
        (["python3", "-m", "jupyter", "console"], "ipython"),
        (["python", "-m", "ptpython"], "python"),
        (["ipython"], "ipython"),
        (["ipython3", "--pylab"], "ipython"),
        (["ipython", "run.py"], None),
        (["ipython", "-i", "run.py"], "ipython"),
        (["jupyter-console"], "ipython"),
        (["jupyter", "console"], "ipython"),
        (["jupyter", "console", "--kernel", "stata"], "stata"),
        (["jupyter", "console", "--kernel=ir"], None),
        (["jupyter", "notebook"], None),
        # An entry-point script under its venv's interpreter, as the process table shows it (#83YV).
        (["/venv/bin/python3", "/venv/bin/jupyter-console", "--existing", "k.json"], "ipython"),
        (["python3", "/venv/bin/jupyter", "console", "--existing", "k.json"], "ipython"),
        (["python3", "/venv/bin/jupyter", "notebook"], None),
        (["python3", "-X", "dev", "/venv/bin/ipython"], "ipython"),
        (["python3", "/venv/bin/ipython", "run.py"], None),
        (["ptpython"], "python"),
        (["bpython"], "python"),
        ("uv run ipython", "ipython"),
        ("conda run -n analysis python", "python"),
        ("env FOO=1 python3", "python"),
        (["stata"], "stata"),
        (["stata-mp"], "stata"),
        (["stata-se", "-q"], "stata"),
        (["xstata"], "stata"),
        (["xstata-mp"], "stata"),
        (["/usr/local/stata18/stata-mp"], "stata"),
        (["StataMP-64.exe"], "stata"),
        (["stata-mp", "-b", "do", "run.do"], None),
        (["bash"], None),
        (["vim", "x.py"], None),
        (["pythonista"], None),
        (["R"], None),
        ([], None),
        (None, None),
        ("", None),
    ]

    def test_table(self):
        for argv, expected in self.CASES:
            with self.subTest(argv=argv):
                self.assertEqual(expected, detect_repl(argv))


if __name__ == "__main__":
    unittest.main()

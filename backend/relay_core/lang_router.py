# SPDX-License-Identifier: AGPL-3.0-or-later
"""Where a composer line goes when the pane's program is a Python, IPython or Stata REPL (#33G0).

`router.classify` answers "shell or agent?" for a Bash prompt. When the foreground program is a
language REPL — or the pane is a Python/Stata workspace with its own kernel (#MEPR plugin 2) —
the question is "program, shell or agent?", and step 5 of classify (`bash -n` plus PATH lookup)
is replaced by a language check. Everything else is shared with the Bash router, imported rather
than copied: input validation, `/shell` and `/agent`, the natural-language pattern, the lone
replies and the sentence detectors. So "why is the coefficient negative" reaches the agent in
every language, and "list the files" does even though `list` is a Stata command.

Nothing here executes the text. Python is judged with `ast`/`codeop`; Stata with a conservative
first-word check against its command list and minimum abbreviations.

Destinations
    program     the REPL or kernel: the line is runnable (or deliberately forced) there.
    agent       a request for the agent, or text that is not runnable in the language.
    shell       the pane's Bash. In Phase 0 (a REPL in the visible pty) the REPL *is* the
                terminal, so the caller types a shell-destined line into it as well; in a kernel
                workspace the pane keeps a Bash shell underneath and `!` runs there.
    incomplete  a block that needs more lines (`def f():`, `x = (1,`, a Stata line ending in
                `///`, an open `{`). The composer keeps it and waits for the rest; nothing is sent.
    empty       nothing to send.

Prefixes. The composer turns a `!` or `*` *typed* as the first character into its terminal or
agent chip and sends `mode="shell"` or `"agent"` without the character (Pane.h, the prefix chip);
pasted text keeps it and arrives in `auto`, where — as in `router.classify` — it is ordinary text,
so a pasted `* header` comment or `!ls` reaches Stata or IPython as written. Typed:
    !text       Relay's: force the shell.
    *text       Relay's: force the agent.
    !!cmd       the program's own `!`: `!cmd` goes to the program — IPython's and Stata's shell
                escape. The first `!` is always Relay's, the second is the program's.
    **text      the program's own `*`: `*text` goes to the program — a Stata `*` comment, or a
                Python starred assignment (`**a, b = xs` sends `*a, b = xs`). `//` comments need
                no escape in Stata and are the recommended spelling.
Only the first character of the composer is a prefix; `!` and `*` further down a block are the
program's (an indented `!ls` in an IPython cell, a `* note` line inside a Stata block).
`/shell ` and `/agent ` work as they do in `router.classify`.
"""
from __future__ import annotations

import ast
import builtins
import codeop
import keyword
import os
import re
import shlex
import textwrap
import tokenize
import io
import warnings
from dataclasses import asdict, dataclass
from typing import Iterable, Sequence

from .router import (ASSIST_THRESHOLD, LEAD_IN, LONE_REPLY, LOOP_ONLY, NATURAL, REPLY_WORDS,
                     SIGNAL_WORDS, _remote_prose, assist_signals, validate_input)

LANGUAGES = ("python", "ipython", "stata")
DESTINATIONS = ("program", "agent", "shell", "incomplete", "empty")


@dataclass(frozen=True)
class Decision:
    destination: str
    reason: str
    # What to deliver: the Relay prefix removed, a doubled prefix collapsed to the program's
    # character, a pasted Python block dedented, trailing whitespace dropped.
    normalized_text: str
    language: str
    syntax_error: str = ""
    # A prefix or an explicit mode chose the destination, not the text.
    forced: bool = False

    def to_dict(self) -> dict:
        return asdict(self)


# ----- Prefixes --------------------------------------------------------------------------------------
def _apply_prefixes(text: str, mode: str) -> tuple[str, str]:
    """(text, mode) after `/shell ` or `/agent `. A leading `!` or `*` is not stripped here: the
    composer consumed it when it was typed, and a pasted one is the program's."""
    for prefix, destination in (("/shell ", "shell"), ("/agent ", "agent")):
        if text.startswith(prefix):
            return text[len(prefix):], destination
    return text, mode


def _forced(text: str, mode: str, language: str) -> Decision:
    # The doubled prefix: the character after Relay's is the program's (see the module docstring).
    if mode == "shell" and text.startswith("!"):
        return Decision("program", f"`!!` sends `!` to the program ({_label(language)} shell escape)",
                        text.rstrip(), language, forced=True)
    if mode == "agent" and text.startswith("*"):
        what = "a Stata `*` comment" if language == "stata" else "a line starting with `*`"
        return Decision("program", f"`**` sends {what} to the program", text.rstrip(), language,
                        forced=True)
    if not text.strip():
        return Decision("empty", "Nothing after the prefix.", "", language, forced=True)
    if mode == "shell":
        return Decision("shell", "Explicit terminal destination.", text.strip(), language, forced=True)
    return Decision("agent", "Explicit agent destination; nothing runs in the program.", text.strip(),
                    language, forced=True)


def _label(language: str) -> str:
    return {"python": "Python", "ipython": "IPython", "stata": "Stata"}[language]


# ----- Prose shared by both languages ----------------------------------------------------------------
PY_BUILTINS = frozenset(dir(builtins))
# Words that open a sentence but that NATURAL does not list (it wants a following word).
QUESTION_WORDS = frozenset("why how what when who where which whose".split())


def _prose_word(word: str) -> bool:
    """A lone word nobody types meaning a name in a REPL: "thanks", "ok", "why", "the"."""
    lowered = word.lower()
    if len(lowered) < 2:
        return False                  # `a`, `i`, `x`: names first, whatever English says
    return (lowered in REPLY_WORDS or lowered in LONE_REPLY or lowered in LOOP_ONLY
            or lowered in QUESTION_WORDS or SIGNAL_WORDS.get(lowered, 0) >= 2
            or bool(NATURAL.match(lowered)))


def _prose_reason(line: str) -> str:
    """Why a line reads as a sentence, "" when it does not. The router's shape-only detector
    (built for ssh prompts, where nothing local can be consulted) plus its NATURAL pattern."""
    if NATURAL.match(line):
        return "reads like a request"
    score, signals, _ = assist_signals(line, local_files=False)
    if score >= ASSIST_THRESHOLD:
        return "a command word used in a sentence: " + ", ".join(signals[:3])
    return _remote_prose(line)


def _words_then_bracket(line: str) -> bool:
    """"check my provider (glm": three or more plain words before the first bracket or quote. Code
    that is still open has a name, an operator or a keyword there: `f(1,`, `x = [1,`, `def f(`."""
    head = re.split(r"[(\[{'\"]", line, maxsplit=1)[0].split()
    return len(head) >= 3 and all(w.isalpha() and not keyword.iskeyword(w) for w in head)


def _request(prose: str, block: str, language: str, syntax_error: str = "") -> Decision:
    why = "Natural-language request" if prose == "reads like a request" else f"Reads like a request ({prose})"
    return Decision("agent", why + " · sent to the agent", block, language, syntax_error=syntax_error)


# ----- Python and IPython ----------------------------------------------------------------------------
# A cell magic owns its whole body (%%bash, %%writefile, %%timeit): the body is not Python.
IPY_CELL_MAGIC = re.compile(r"^%%[A-Za-z_]\w*")
# Help: `name?`, `name??`, `?name`, `obj.attr?`, wildcards `np.*load*?`, `%magic?`.
IPY_HELP = re.compile(r"^(\?{1,2})?%{0,2}[A-Za-z_*][\w.*]*(\?{1,2})?$")
# `x = !ls`, `files = %sx ls`: an assignment from a shell escape or a magic.
IPY_ASSIGN_ESCAPE = re.compile(r"^([A-Za-z_][\w.]*(?:\s*,\s*[A-Za-z_][\w.]*)*)\s*=\s*[!%]")
# IPython's default automagics a person types bare. `run`, `cd`, `ls` are also English words, so
# a line using one still has to pass the prose check.
IPY_AUTOMAGIC = frozenset("""
alias autoreload bookmark cd clear colors conda config debug dhist dirs doctest_mode edit env
gui history less load load_ext logstart ls macro matplotlib mkdir more notebook page pastebin
pdb pdef pdoc pfile pinfo pinfo2 pip popd pprint precision prun psearch psource pushd pwd pycat
pylab quickref recall rehashx reload_ext rerun reset reset_selective rm rmdir run save sc store
sx system tb time timeit unalias unload_ext who who_ls whos xdel xmode
""".split())
# The statements whose keyword alone makes an all-words line code: `import os`, `del x`, `pass`.
PY_STATEMENT_KEYWORDS = frozenset("import from del global nonlocal pass raise assert return yield "
                                  "await lambda".split())


def _ipython_help(line: str) -> bool:
    return "?" in line and bool(IPY_HELP.match(line))


def _ipython_to_python(text: str) -> tuple[str | None, str]:
    """IPython syntax replaced by Python of the same shape, so `ast` can judge the rest.

    Returns (source, note): source None means the text is an IPython construct the Python parser
    cannot judge and that is runnable as a whole (a cell magic); note names what was replaced."""
    lines = text.split("\n")
    first = next((ln.strip() for ln in lines if ln.strip()), "")
    if IPY_CELL_MAGIC.match(first):
        return None, "an IPython cell magic"
    out, notes = [], set()
    for ln in lines:
        body = ln.lstrip()
        indent = ln[:len(ln) - len(body)]
        if body.startswith(("%", "!")):
            out.append(indent + "pass")
            notes.add("magic" if body[0] == "%" else "shell escape")
        elif _ipython_help(body.rstrip()):
            out.append(indent + "pass")
            notes.add("help")
        elif (m := IPY_ASSIGN_ESCAPE.match(body)):
            out.append(indent + m.group(1) + " = None")
            notes.add("magic assignment")
        else:
            out.append(ln)
    return "\n".join(out), ", ".join(sorted(notes))


def _python_status(source: str) -> tuple[str, str]:
    """("ok" | "incomplete" | "error", message). Never executes anything."""
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        try:
            ast.parse(source)
            return "ok", ""
        except SyntaxError as exc:
            message = f"{exc.msg} (line {exc.lineno})" if exc.lineno else exc.msg
        except (ValueError, MemoryError, RecursionError) as exc:  # null bytes, absurd nesting
            return "error", str(exc)
        try:
            if codeop.compile_command(source, "<composer>", "exec") is None:
                return "incomplete", ""
        except (SyntaxError, ValueError, OverflowError):
            pass
    return "error", message


def _python_tokens(source: str) -> list[tokenize.TokenInfo] | None:
    try:
        return [t for t in tokenize.generate_tokens(io.StringIO(source).readline)
                if t.type not in (tokenize.NEWLINE, tokenize.NL, tokenize.ENDMARKER, tokenize.INDENT,
                                  tokenize.DEDENT, tokenize.COMMENT)]
    except (tokenize.TokenError, SyntaxError, IndentationError):
        return None


def _python_words_only(source: str, names: set[str]) -> str:
    """For a line that parses: "" when it is code, else why it reads as a sentence.

    Prose that Python accepts is made of bare words: "hello", "not done", "yes or no",
    "ok, thanks" (a tuple). Anything with an operator, a call, a literal or a second line is code."""
    tokens = _python_tokens(source)
    if not tokens or "\n" in source.strip():
        return ""
    if any(t.type != tokenize.NAME and not (t.type == tokenize.OP and t.string == ",") for t in tokens):
        return ""
    words = [t.string for t in tokens]
    if words[0] in PY_STATEMENT_KEYWORDS:
        return ""
    idents = [w for w in words if w != "," and not keyword.iskeyword(w)]
    if len(words) == 1:
        word = words[0]
        if word in names or word in PY_BUILTINS or keyword.iskeyword(word) or not _prose_word(word):
            return ""
        return f"“{word}” on its own reads as a reply, not a name"
    if idents and all(w in names or w in PY_BUILTINS for w in idents):
        return ""
    return "only words; reads like a sentence, and those names are not defined"


def _code_shaped(source: str) -> bool:
    """Parses, and is not bare words: an operator, a call, a literal or several lines."""
    tokens = _python_tokens(source)
    return bool(tokens) and ("\n" in source.strip()
                             or any(t.type != tokenize.NAME and not (t.type == tokenize.OP and t.string == ",")
                                    for t in tokens))


def _first_word(line: str) -> str:
    m = re.match(r"\s*([A-Za-z_]\w*)", line)
    return m.group(1) if m else ""


def _classify_python(text: str, language: str, names: set[str]) -> Decision:
    ipython = language == "ipython"
    label = _label(language)
    block = textwrap.dedent(text.strip("\n")).rstrip()
    first_line = block.split("\n", 1)[0].strip()
    single = "\n" not in block

    if single and first_line in LOOP_ONLY:
        return Decision("agent", f"“{first_line}” means nothing outside a loop; sent to the agent",
                        block, language)
    if single and first_line in LONE_REPLY and first_line not in names:
        return Decision("agent", f"“{first_line}” on its own is a reply; sent to the agent", block, language)

    source, note = (_ipython_to_python(block) if ipython else (block, ""))
    if source is None:
        return Decision("program", f"Runnable in {label}: {note}.", block, language)

    if ipython and single:
        # `cd data`, `pwd`, `pip install pandas`, `run model.py`: automagics, unless a sentence.
        word = _first_word(first_line)
        if word in IPY_AUTOMAGIC and word not in names and not _prose_reason(first_line):
            status, _ = _python_status(block)
            if status != "ok":
                return Decision("program", f"Runnable in IPython: the %{word} automagic.", block, language)
        if _ipython_help(first_line):
            name = first_line.strip("?")
            if "." not in name and "*" not in name and name not in names and name not in PY_BUILTINS \
                    and (_prose_word(name) or name.lower() in SIGNAL_WORDS):
                return Decision("agent", f"“{first_line}” reads as a question, not help on a name",
                                block, language)

    status, message = _python_status(source)
    if status == "ok":
        prose = _python_words_only(source, names)
        if not prose and NATURAL.match(first_line) and not _code_shaped(source) \
                and _first_word(first_line) not in names:
            prose = "reads like a request"
        if prose:
            return _request(prose, block, language)
        what = f"Runnable in {label}" + (f" ({note})" if note else "")
        return Decision("program", what + ".", block, language)

    word = _first_word(first_line)
    exempt = keyword.iskeyword(word) or word in names or word in PY_BUILTINS
    prose = "" if exempt and not NATURAL.match(first_line) else _prose_reason(first_line)
    if not exempt and not prose and _words_then_bracket(first_line):
        prose = "words before an open bracket"
    if status == "incomplete":
        if prose:
            return _request(prose, block, language)
        return Decision("incomplete", f"Incomplete {label} block; keep typing (Shift+Enter for a new line).",
                        block, language)
    if prose:
        return _request(prose, block, language, message)
    return Decision("agent", f"Not valid {label} ({message}) · sent to the agent", block, language,
                    syntax_error=message)


# ----- Stata ----------------------------------------------------------------------------------------
# Commands and the shortest abbreviation Stata accepts (the underlined part in the manual): "reg"
# for regress is "regress:3". No number means only the full name. Conservative on purpose: a word
# not listed here goes to the agent, and `known_commands` adds the user's installed ado commands.
STATA_COMMANDS_SPEC = """
about ado adopath anova append areg arima args assert bar biprobit boottest bootstrap bs bsample
browse:2 by bysort:3 capture:3 cd char ci cii clear clonevar cls cluster cmdlog codebook
coefplot collapse collect compress confirm constraint continue contract contrast copy correlate:3
count:3 creturn cross csdid decode define describe:1 destring dfuller di dir discard display:2
distinct do drop dtable duplicates edit:2 egen else encode end erase estadd estat estimates:3
estout estpost eststo esttab etable exit expand export factor file fillin findit format fp frame
frames fre gcollapse generate:1 gegen global:2 glm graph:2 gsem gsort heckman help:1 histogram:4
if import include infile infix input insheet inspect ivreg2 ivreghdfe ivregress jackknife jk
joinby kdensity keep kwallis label:2 levelsof lincom line list:1 local:3 log logistic logit lowess
lpoly ls macro margins marginsplot marksample mata matrix:3 mean melogit merge meglm mfp mi mixed
misstable mkdir mlogit more mvdecode mvencode nbreg nestreg net newey nlcom noisily:1 notes
numlabel odbc ologit oprobit order outreg2 outsheet pause pca pctile permute poisson ppmlhdfe
prais predict predictnl preserve probit program:2 prtest putdocx putexcel pwcompare pwcorr pwd
pwmean python qreg query quietly:3 ranksum recode reg3 reghdfe regress:3 rename:3 replace reshape
restore return rmdir rolling rreg run sample save scalar:3 scatter:2 search sem set shell
signrank simulate sleep sort spearman ssc sreturn stack statsby stcox stepwise streg sts stset
stsplit summarize:2 sureg svy svyset sw synth syntax sysuse tab1 tab2 table tabstat tabulate:2
tempfile tempname tempvar test testparm teffects timer tobit tostring translate tsline tsset ttest
twoway:2 type unique update use var varsoc version:4 webuse which while winexec winsor2 xi xpose
xtabond xtdescribe xtile xtivreg xtlogit xtpoisson xtprobit xtreg xtset xtsum rdrobust
binscatter asdoc foreach forvalues:4 sencode labmask tabout didregress xtdidregress
""".split()
STATA_COMMANDS: dict[str, int] = {}
for _entry in STATA_COMMANDS_SPEC:
    _name, _, _min = _entry.partition(":")
    STATA_COMMANDS[_name] = int(_min) if _min else len(_name)
# Prefixes that take a colon before the command they modify (`by id: gen …`, `svy: mean x`).
STATA_COLON_PREFIXES = frozenset("by bysort xi svy mi bootstrap bs jackknife jk statsby rolling "
                                 "permute simulate nestreg stepwise sw fp mfp eststo version".split())
# Prefixes whose colon is optional (`quietly reg y x`, `capture: drop z`, `capture noisily …`).
STATA_BARE_PREFIXES = frozenset("capture quietly noisily".split())
# Words Stata syntax uses as operands; they say nothing about English.
STATA_SYNTAX_WORDS = frozenset("if in using of us".split())
STATA_WORD = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
STATA_DELIMIT = re.compile(r"^#d(?:e(?:l(?:i(?:m(?:i(?:t)?)?)?)?)?)?\s+(;|cr)\s*$")


def stata_command(word: str, known: Iterable[str] = ()) -> str:
    """The full Stata command `word` names (an abbreviation resolves), or ""."""
    if word in STATA_COMMANDS or word in known:
        return word
    for name, minimum in STATA_COMMANDS.items():
        if len(word) >= minimum and name.startswith(word):
            return name
    return ""


def _scan_stata_line(line: str, state: dict) -> tuple[str, bool]:
    """One physical line without comments: (code, continues). `state` carries an open `/*`."""
    out, i, n = [], 0, len(line)
    quote = False
    while i < n:
        if state["block"]:
            end = line.find("*/", i)
            if end < 0:
                return "".join(out), False
            state["block"], i = False, end + 2
            out.append(" ")
            continue
        c = line[i]
        if c == '"':
            quote = not quote
        elif not quote:
            if line.startswith("/*", i):
                state["block"], i = True, i + 2
                continue
            if line.startswith("//", i) and (i == 0 or line[i - 1].isspace()):
                return "".join(out), line.startswith("///", i)
        out.append(c)
        i += 1
    return "".join(out), False


def _stata_statements(text: str) -> tuple[list[str], str]:
    """Split a block into statements, comments removed: (statements, why it is incomplete)."""
    state = {"block": False}
    statements: list[str] = []
    delim = "cr"
    pending = ""
    for raw in text.split("\n"):
        code, continues = _scan_stata_line(raw, state)
        if delim == "cr":
            if not pending and code.lstrip().startswith("*"):
                continue                      # a `*` comment line
            pending += code + (" " if continues else "")
            if continues or state["block"]:
                continue
            stmt, pending = pending.strip(), ""
            if stmt:
                statements.append(stmt)
                m = STATA_DELIMIT.match(stmt)
                if m:
                    delim = m.group(1)
            continue
        if not pending.strip() and STATA_DELIMIT.match(code.strip()):
            statements.append(code.strip())   # `#delimit cr` needs no `;`
            delim = STATA_DELIMIT.match(code.strip()).group(1)
            continue
        pending += code + " "
        while True:
            cut = _unquoted_find(pending, ";")
            if cut < 0:
                break
            stmt, pending = pending[:cut].strip(), pending[cut + 1:]
            if not stmt or stmt.startswith("*"):
                continue
            statements.append(stmt)
            m = STATA_DELIMIT.match(stmt)
            if m:
                delim = m.group(1)
                if delim == "cr":
                    # The rest of the line after `#delimit cr;` is read line by line again.
                    break
    if state["block"]:
        return statements, "a /* comment is still open"
    if pending.strip():
        return statements, ("the last line ends with ///" if delim == "cr"
                            else "#delimit ; is on and the last statement has no ;")
    return statements, ""


def _unquoted_find(text: str, char: str) -> int:
    quote = False
    for i, c in enumerate(text):
        if c == '"':
            quote = not quote
        elif c == char and not quote:
            return i
    return -1


def _colon_end(text: str) -> int:
    """Index after the `:` that ends a prefix clause, outside quotes and parentheses; -1 if none."""
    depth, quote = 0, False
    for i, c in enumerate(text):
        if c == '"':
            quote = not quote
        elif quote:
            continue
        elif c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == ":" and depth <= 0:
            return i + 1
    return -1


def _stata_command_of(stmt: str, known: set[str]) -> tuple[str, str, str]:
    """(command, rest, error) for one statement, with capture/quietly/by …: prefixes removed."""
    rest = stmt.strip()
    for _ in range(8):
        if not rest:
            return "", "", "a prefix with no command after it"
        if rest[0] in "{}":
            return rest[0], rest[1:], ""
        if rest[0] in "`$":
            return "macro", rest, ""       # `cmd' or $cmd: a macro expands to the command
        if rest.startswith("#"):
            return ("#delimit", rest, "") if STATA_DELIMIT.match(rest) else ("", rest, "not a Stata directive")
        if rest.startswith("!"):
            return "shell", rest[1:], ""
        m = STATA_WORD.match(rest)
        if not m:
            return "", rest, f"“{rest.split()[0]}” is not a Stata command"
        word = m.group(0)
        command = stata_command(word, known)
        if not command:
            return "", rest, f"“{word}” is not a Stata command"
        after = rest[m.end():]
        if command in STATA_BARE_PREFIXES:
            rest = after.lstrip()
            rest = rest[1:].lstrip() if rest.startswith(":") else rest
            continue
        if command in STATA_COLON_PREFIXES and (command != "version" or ":" in after):
            end = _colon_end(after)
            if end < 0:
                if command == "by":
                    return "", after, "`by` needs a colon before the command"
                return command, after, ""
            rest = after[end:].lstrip()
            continue
        return command, after, ""
    return "", rest, "too many prefixes"


def _stata_prose(command: str, args: str, word: str) -> str:
    """Why a line whose first word is a Stata command reads as a sentence: "list the files",
    "help me", "sort by price", "use my data". Only plain words are judged; `=`, options,
    quotes or numbers make it code. Only strong signals count (articles, pronouns, question words,
    please/thanks, a lead-in), because Stata keywords are English: `set more off` is code."""
    words = args.split()
    if not words or command in {"macro", "{", "}", "#delimit", "shell"}:
        return ""
    if any(not re.fullmatch(r"[A-Za-z’']+[,.?!]?", w) for w in words):
        return ""
    bare = [w.rstrip(",.?!").lower() for w in words]
    score, why = 0, []
    if (len(bare[0]) > 1 and bare[0] in LEAD_IN) or (bare[0] == "by" and command not in {"by", "bysort"}):
        score, why = 2, [f"“{bare[0]}” after “{word}”"]
    for w in bare:
        if len(w) > 1 and w not in STATA_SYNTAX_WORDS and SIGNAL_WORDS.get(w, 0) >= 2 and w not in why:
            score += 2
            why.append(w)
    if words[-1].endswith("?"):
        score += 2
        why.append("a question")
    if "’" in args or "'" in args:
        score += 2
        why.append("an apostrophe")
    return ", ".join(why) if score >= 2 else ""


def _classify_stata(text: str, known: set[str]) -> Decision:
    block = text.strip("\n").rstrip()
    first_line = block.lstrip().split("\n", 1)[0].strip()
    single = "\n" not in block.strip()
    if single and first_line in LOOP_ONLY:
        return Decision("agent", f"“{first_line}” means nothing outside a loop; sent to the agent",
                        block, "stata")
    statements, incomplete = _stata_statements(block)
    if incomplete:
        prose = _prose_reason(first_line)
        if prose:
            return _request(prose, block, "stata")
        return Decision("incomplete", f"Incomplete Stata block: {incomplete}; keep typing.", block, "stata")
    if not statements:
        return Decision("program", "A Stata comment.", block, "stata")

    depth, programs, raw_block = 0, 0, ""   # braces, program…end, mata/python…end
    for index, stmt in enumerate(statements):
        if raw_block:
            if stmt.strip() == "end":
                raw_block = ""
            continue
        command, args, error = _stata_command_of(stmt, known)
        if error:
            prose = _prose_reason(first_line)
            if prose:
                return _request(prose, block, "stata")
            return Decision("agent", f"Not runnable in Stata ({error}) · sent to the agent", block, "stata",
                            syntax_error=error)
        if index == 0:
            prose = _stata_prose(command, args, _first_word(stmt) or command)
            if prose:
                return _request(prose, block, "stata")
        depth += _count_unquoted(stmt, "{") - _count_unquoted(stmt, "}")
        if command == "program" and (args.split() or [""])[0] not in {"drop", "dir", "list"}:
            programs += 1
        elif command == "end":
            programs -= 1
        elif command in {"mata", "python"} and args.strip() in {"", ":"}:
            raw_block = command
    if raw_block:
        return Decision("incomplete", f"Incomplete Stata block: {raw_block} needs its end; keep typing.",
                        block, "stata")
    if depth > 0:
        return Decision("incomplete", "Incomplete Stata block: a { is still open; keep typing.", block, "stata")
    if programs > 0:
        return Decision("incomplete", "Incomplete Stata block: program needs its end; keep typing.",
                        block, "stata")
    return Decision("program", "Runnable in Stata.", block, "stata")


def _count_unquoted(text: str, char: str) -> int:
    count, quote = 0, False
    for c in text:
        if c == '"':
            quote = not quote
        elif c == char and not quote:
            count += 1
    return count


# ----- Entry points ---------------------------------------------------------------------------------
def classify_line(text: str, language: str, mode: str = "auto", names: Iterable[str] = (),
                  known_commands: Iterable[str] = ()) -> Decision:
    """Where a composer submission goes when the pane's program speaks `language`.

    `mode` is the composer's: "auto", or "shell"/"agent" when a chip or the typed `!`/`*` prefix
    chose it. `names` are names defined in the program (a kernel's variables) — a bare word that is
    a defined name is code, not a reply. `known_commands` adds Stata commands (installed ado files).
    Raises ValueError for an unknown language or mode, or input the composer never sends."""
    if language not in LANGUAGES:
        raise ValueError(f"Unknown language {language!r}; expected one of {', '.join(LANGUAGES)}.")
    if mode not in {"auto", "shell", "agent"}:
        raise ValueError("Unknown input mode.")
    text = validate_input(text)
    text, mode = _apply_prefixes(text, mode)
    if mode != "auto":
        return _forced(text, mode, language)
    if not text.strip():
        return Decision("empty", f"Type {_label(language)} code or an agent request.", "", language)
    if language == "stata":
        return _classify_stata(text, set(known_commands))
    return _classify_python(text, language, set(names))


# ----- Which REPL is in the foreground? --------------------------------------------------------------
PYTHON_BINARY = re.compile(r"^(?:python|pypy)(?:\d+(?:\.\d+)*)?[a-z]?(?:-dbg)?(?:\.exe)?$", re.I)
IPYTHON_BINARY = re.compile(r"^ipython\d*(?:\.exe)?$", re.I)
PY_REPL_BINARY = re.compile(r"^(?:ptpython|bpython)\d*(?:\.exe)?$", re.I)
# stata, stata-mp, stata-se, stata-be, xstata-mp, StataMP-64.exe, "Stata 18"
STATA_BINARY = re.compile(r"^x?stata(?:[-_ ]?(?:mp|se|ic|be|sm))?(?:[-_ ]?\d+)?(?:\.exe)?$", re.I)
# Launchers whose argument is the real program: `uv run ipython`, `conda run -n x python`.
LAUNCHERS = {"env": 0, "uv": 1, "poetry": 1, "pipenv": 1, "pixi": 1, "conda": 1, "mamba": 1,
             "micromamba": 1, "hatch": 1, "pdm": 1, "rye": 1, "nice": 0, "nohup": 0, "time": 0}
# Python options that take a value, so the word after them is not the script.
PY_VALUE_OPTS = {"-W", "-X", "-Q"}


def _basename(word: str) -> str:
    return re.split(r"[\\/]", word)[-1]


def detect_repl(argv: str | Sequence[str] | None) -> str | None:
    """The language of the REPL `argv` runs, or None when it is not an interactive REPL we route.

    `argv` is the foreground process's argument vector, or a string: a command line (split with
    shell rules) or a bare process name. With only a name (`python3.12`) the arguments are unknown,
    so an interpreter name counts as a REPL; pass the argv when you have it, and `python app.py`
    is correctly not one."""
    if argv is None:
        return None
    if isinstance(argv, str):
        try:
            words = shlex.split(argv, posix=os.name != "nt") if " " in argv.strip() else [argv.strip()]
        except ValueError:
            words = argv.split()
    else:
        words = [str(w) for w in argv]
    words = [w for w in words if w]
    # Strip launchers: `env A=1 python`, `uv run ipython`, `conda run -n env python`.
    for _ in range(4):
        if not words:
            return None
        name = _basename(words[0]).lower()
        if name not in LAUNCHERS:
            break
        rest = words[1:]
        if LAUNCHERS[name]:
            if not rest or rest[0] not in {"run", "exec", "shell"}:
                return None
            rest = rest[1:]
        while rest and (rest[0].startswith("-") or "=" in rest[0]):
            takes_value = rest[0] in {"-n", "--name", "-p", "--prefix", "-u", "--with", "-e", "--env"}
            rest = rest[2:] if takes_value else rest[1:]
        words = rest
    if not words:
        return None
    name, args = _basename(words[0]), words[1:]
    if IPYTHON_BINARY.match(name):
        return "ipython" if _interactive(args, ipython=True) else None
    if name.lower() in {"jupyter-console", "jupyter"}:
        if name.lower() == "jupyter":
            if not args or args[0] != "console":
                return None
            args = args[1:]
        return _console_kernel(args)
    if PY_REPL_BINARY.match(name):
        return "python"
    if PYTHON_BINARY.match(name):
        if "-m" in args:
            module = args[args.index("-m") + 1] if args.index("-m") + 1 < len(args) else ""
            if module == "IPython":
                return "ipython" if _interactive(args[args.index("-m") + 2:], ipython=True) else None
            if module == "jupyter" and args[args.index("-m") + 2:args.index("-m") + 3] == ["console"]:
                return _console_kernel(args[args.index("-m") + 3:])
            if module in {"jupyter_console"}:
                return _console_kernel(args[args.index("-m") + 2:])
            if module in {"ptpython", "bpython", "code"}:
                return "python"
            return "python" if "-i" in args[:args.index("-m")] else None
        # An entry-point script run by its interpreter: that is what the process table shows for
        # `ipython` or `jupyter console` installed in a venv (the shebang names the venv's
        # python), so the pty of a Python console pane reads `python3 …/jupyter-console
        # --existing k.json` (#83YV).
        script = _script_of(args)
        if script is not None and (IPYTHON_BINARY.match(_basename(args[script]))
                                   or _basename(args[script]).lower() in {"jupyter", "jupyter-console"}):
            return detect_repl(args[script:])
        return "python" if _interactive(args, ipython=False) else None
    if STATA_BINARY.match(name):
        # Batch mode (`stata -b do x.do`, `-e`) runs and exits; everything else is interactive.
        return None if any(a in {"-b", "-e", "/b", "/e"} for a in args) else "stata"
    return None


def _script_of(args: Sequence[str]) -> int | None:
    """The index of the script an interpreter's argv runs, or None (`-c`, `-m`, stdin, none)."""
    i = 0
    while i < len(args):
        a = args[i]
        if a in {"-c", "-m", "-"}:
            return None
        if a in {"-X", "-W", "-Q"}:           # options that take the next word
            i += 2
            continue
        if not a.startswith("-"):
            return i
        i += 1
    return None


def _interactive(args: Sequence[str], *, ipython: bool) -> bool:
    """No script and no -c, or -i with them."""
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-i" or (not ipython and a.startswith("-") and not a.startswith("--") and "i" in a[1:]
                         and a[1:].isalpha()):
            return True
        if a in {"-c", "-m"} or (not a.startswith("-") and a != "-"):
            return False
        if a == "-" and not ipython:
            return False
        if a in PY_VALUE_OPTS:
            i += 1
        i += 1
    return True


def _console_kernel(args: Sequence[str]) -> str | None:
    """`jupyter console [--kernel NAME]`: IPython unless the kernel is another language."""
    kernel = "python3"
    for i, a in enumerate(args):
        if a.startswith("--kernel="):
            kernel = a.split("=", 1)[1]
        elif a == "--kernel" and i + 1 < len(args):
            kernel = args[i + 1]
    kernel = kernel.lower()
    if kernel.startswith("python") or kernel in {"ipython", "ipykernel"}:
        return "ipython"
    if "stata" in kernel:
        return "stata"
    return None

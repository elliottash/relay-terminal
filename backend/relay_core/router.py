# SPDX-License-Identifier: GPL-3.0-or-later
"""Conservative, local-only input classification. Parsing NEVER executes input."""
from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
import threading
from dataclasses import asdict, dataclass
from typing import Iterable

MAX_INPUT = 131072
BUILTINS = frozenset("alias bg bind break builtin cd command compgen complete compopt continue declare dirs disown echo enable eval exec exit export false fc fg getopts hash help history jobs kill let local logout mapfile popd printf pushd pwd read readarray readonly return set shift shopt source test times trap true type typeset ulimit umask unalias unset wait . : [ [[ !".split())
NATURAL = re.compile(r"^(?:why\b|how\b|what\b|when\b|who\b|where (?:is|are|can|should)\b|(?:can|could|would|will) you\b|please\b|explain\b|debug\b|summari[sz]e\b|refactor\b|implement\b|fix (?:the|this|my|a|all)\b|help me\b|(?:show|tell) me\b|(?:write|create|make|build) (?:a|an|the|me)\b|find (?:the|my|all the)\b|list (?:the|my|all the)\b)", re.I)
KEYWORDS_CMD = frozenset("if then else elif do while until ! { time coproc".split())
KEYWORDS_END = frozenset("fi done esac }".split())
KEYWORDS = KEYWORDS_CMD | KEYWORDS_END | frozenset("for select case in function [[ ]]".split())
# Prefix commands whose next word is also in command position, with options that take a value.
WRAPPERS = {"env": {"-u", "-C", "-S", "--unset", "--chdir"}, "command": set(), "builtin": set(),
            "exec": {"-a"}, "nice": {"-n", "--adjustment"}, "nohup": set(),
            "sudo": {"-u", "-g", "-C", "-D", "-h", "-p", "-U", "-r", "-t"}}
ASSIGNMENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])?\+?=")
OPERATORS = ["<<<", "<<-", ";;&", "&>>", "<<", "<(", ">(", "&&", "||", ";;", ";&", "|&", "&>", ">>", ">&", "<&",
             ">|", "<>", "(", ")", ";", "|", "&", "<", ">"]
SEPARATORS = {";", "&", "&&", "||", "|", "|&"}
REDIRECTS = {"<", ">", ">>", "<&", ">&", "&>", "&>>", "<>", ">|", "<<<"}
PLACEHOLDER = "__relay_subst__"
MAX_DEPTH = 5


@dataclass(frozen=True)
class Decision:
    route: str
    text: str
    reason: str
    syntax_ok: bool = True
    syntax_error: str = ""
    # Whether the text looks runnable: valid syntax and every command word resolves.
    valid: bool = True
    invalid_reason: str = ""
    # Protocol 11: local rules could not decide (a command that is also an English word, used in a
    # sentence). route is the local best guess; the GUI may ask the model with route_assist.
    needs_assist: bool = False
    assist_reason: str = ""

    def to_dict(self) -> dict:
        return asdict(self)


def validate_input(text: str) -> str:
    if not isinstance(text, str):
        raise ValueError("Input must be text.")
    text = text.replace("\r\n", "\n")
    if len(text.encode("utf-8")) > MAX_INPUT:
        raise ValueError("Input is too large (128 KiB maximum).")
    if any((ord(c) < 32 and c not in "\n\t") or ord(c) == 127 for c in text):
        raise ValueError("Control characters are not allowed in the composer; use native terminal input.")
    return text


def bash_syntax(text: str) -> tuple[bool, str]:
    try:
        proc = subprocess.run(["/bin/bash", "--noprofile", "--norc", "-n"], input=text,
                              text=True, capture_output=True, timeout=2,
                              env={"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8"})
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, f"Bash syntax check unavailable: {exc}"
    return proc.returncode == 0, proc.stderr.strip()[:1000]


class _TooComplex(Exception):
    """Input uses syntax this tokenizer does not model; trust bash -n plus the first word."""


def _extract_substitutions(text: str) -> tuple[str, list[str]]:
    """Replace $(...) and `...` outside single quotes with a placeholder word.

    Returns the outer text and the inner command texts, which are checked separately.
    """
    out: list[str] = []
    inner: list[str] = []
    i, n = 0, len(text)
    single = double = False
    while i < n:
        c = text[i]
        if single:
            out.append(c)
            single = c != "'"
            i += 1
            continue
        if c == "\\":
            out.append(text[i:i + 2]); i += 2
            continue
        if c == "'" and not double:
            single = True; out.append(c); i += 1
            continue
        if c == '"':
            double = not double; out.append(c); i += 1
            continue
        if text.startswith("$((", i):
            raise _TooComplex("arithmetic expansion")
        if text.startswith("$(", i):
            depth, j, q = 1, i + 2, None
            while j < n and depth:
                ch = text[j]
                if q:
                    if ch == q:
                        q = None
                    elif ch == "\\" and q == '"':
                        j += 1
                elif ch in "'\"":
                    q = ch
                elif ch == "\\":
                    j += 1
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                j += 1
            if depth:
                raise _TooComplex("unterminated command substitution")
            inner.append(text[i + 2:j - 1]); out.append(PLACEHOLDER); i = j
            continue
        if c == "`":
            j = i + 1
            while j < n and text[j] != "`":
                j += 2 if text[j] == "\\" else 1
            if j >= n:
                raise _TooComplex("unterminated backtick")
            inner.append(text[i + 1:j]); out.append(PLACEHOLDER); i = j + 1
            continue
        out.append(c); i += 1
    return "".join(out), inner


def _tokens(text: str) -> list[tuple[str, str]]:
    """Split into ("word", value) and ("op", operator) tokens. Newlines become ";"."""
    text = text.replace("\\\n", " ")
    lex = shlex.shlex(text.replace("\n", " ; "), posix=True, punctuation_chars=True)
    lex.whitespace_split = True
    result: list[tuple[str, str]] = []
    for token in lex:
        if token and all(ch in "();<>|&" for ch in token):
            k = 0
            while k < len(token):
                op = next(o for o in OPERATORS if token.startswith(o, k))
                result.append(("op", op)); k += len(op)
        else:
            result.append(("word", token))
    return result


def _resolve(word: str, known: set[str], path: str, cwd: str) -> str:
    """Return "" if the word can run as a command, else the reason it cannot."""
    if PLACEHOLDER in word or "$" in word or any(ch in word for ch in "*?["):
        return ""  # Expanded at run time; cannot be judged statically.
    if word in BUILTINS or word in KEYWORDS or word in known:
        return ""
    if "/" in word:
        target = os.path.expanduser(word)
        if not os.path.isabs(target):
            target = os.path.join(cwd, target)
        if os.path.isdir(target):
            return f"is a directory: {word}"
        if not os.path.exists(target):
            return f"no such file: {word}"
        if not os.access(target, os.X_OK):
            return f"not executable: {word}"
        return ""
    return "" if on_path(word, path) else f"command not found: {word}"


def _check_words(text: str, known: set[str], path: str, cwd: str, depth: int = 0) -> str:
    if depth > MAX_DEPTH:
        raise _TooComplex("nesting")
    outer, inner = _extract_substitutions(text)
    for sub in inner:
        reason = _check_words(sub, known, path, cwd, depth + 1)
        if reason:
            return reason
    # Arithmetic commands, case statements, heredocs and arrays are not modelled.
    if re.search(r"(?:^|[\s;&|(])\(\(", outer):
        raise _TooComplex("arithmetic command")
    try:
        tokens = _tokens(outer)
    except ValueError:
        raise _TooComplex("unbalanced quoting") from None
    expect_cmd, skip_redirect, header, dbl_bracket, fn_name = True, False, False, False, False
    wrapper_opts: set[str] | None = None
    i = 0
    while i < len(tokens):
        kind, value = tokens[i]
        i += 1
        if kind == "op":
            if value in {"<<", "<<-", ";;", ";&", ";;&"}:
                raise _TooComplex("heredoc or case")
            if value in REDIRECTS:
                skip_redirect = True
            elif value in SEPARATORS:
                expect_cmd, header, wrapper_opts = True, False, None
            elif value in {"(", "<(", ">("}:
                expect_cmd = True
            elif value == ")":
                expect_cmd = False
            continue
        if skip_redirect:
            skip_redirect = False
            continue
        if header:
            continue
        if dbl_bracket:
            if value == "]]":
                dbl_bracket, expect_cmd = False, False
            continue
        if fn_name:
            fn_name = False
            known.add(value)  # Defined earlier in this input, so later calls resolve.
            if tokens[i:i + 2] == [("op", "("), ("op", ")")]:
                i += 2
            continue
        if not expect_cmd:
            continue
        if wrapper_opts is not None and value.startswith("-"):
            if value in wrapper_opts:
                skip_redirect = True  # The option's value is the next word.
            continue
        if ASSIGNMENT.match(value):
            if value.endswith("=") and i < len(tokens) and tokens[i] == ("op", "("):
                raise _TooComplex("array assignment")
            continue
        if value in KEYWORDS_CMD:
            wrapper_opts = {"-p"} if value == "time" else None
            continue
        if value in KEYWORDS_END:
            expect_cmd = False
            continue
        if value in {"for", "select"}:
            header, expect_cmd = True, False
            continue
        if value == "case":
            raise _TooComplex("case")
        if value == "function":
            fn_name = True
            continue
        if value == "[[":
            dbl_bracket = True
            continue
        if tokens[i:i + 2] == [("op", "("), ("op", ")")]:
            i += 2  # name() { ...; } defines a function; the name need not exist yet.
            known.add(value)
            continue
        reason = _resolve(value, known, path, cwd)
        if reason:
            return reason
        if value in WRAPPERS:
            wrapper_opts = WRAPPERS[value]
            continue
        expect_cmd, wrapper_opts = False, None
    return ""


# ----- PATH executables (cached per directory, refreshed when the directory changes) -----------------
_EXEC_LOCK = threading.Lock()
_EXEC_CACHE: dict[str, tuple[tuple[int, int, int], frozenset[str]]] = {}
MAX_EXEC_ENTRIES = 50_000


def _dir_executables(directory: str) -> frozenset[str]:
    """Executable names in one PATH directory. Cached by (device, inode, mtime_ns): installing or
    removing a program changes the directory's mtime, so new programs appear without a restart."""
    try:
        st = os.stat(directory)
    except OSError:
        return frozenset()
    key = (st.st_dev, st.st_ino, st.st_mtime_ns)
    with _EXEC_LOCK:
        cached = _EXEC_CACHE.get(directory)
        if cached is not None and cached[0] == key:
            return cached[1]
    names = set()
    try:
        with os.scandir(directory) as entries:
            for count, entry in enumerate(entries):
                if count >= MAX_EXEC_ENTRIES:
                    break
                try:
                    if entry.is_file() and os.access(entry.path, os.X_OK):
                        names.add(entry.name)
                except OSError:
                    continue
    except OSError:
        return frozenset()
    result = frozenset(names)
    with _EXEC_LOCK:
        _EXEC_CACHE[directory] = (key, result)
    return result


def path_executables(path: str | None = None) -> set[str]:
    """All executable names on PATH (union of the cached per-directory scans)."""
    names: set[str] = set()
    for directory in (path or os.environ.get("PATH") or os.defpath).split(os.pathsep):
        if directory and os.path.isabs(directory):
            names |= _dir_executables(directory)
    return names


def on_path(word: str, path: str) -> bool:
    for directory in path.split(os.pathsep):
        if directory and os.path.isabs(directory) and word in _dir_executables(directory):
            return True
    # A chmod +x does not change the directory mtime; fall back to a direct lookup on a miss.
    return shutil.which(word, path=path) is not None


# ----- Routing assist: commands that are also everyday English words -----------------------------------
# Inclusive on purpose: a word only matters when it also resolves as a command on this machine (PATH,
# builtin, alias or function), and only triggers an assist when the rest of the input reads like a
# sentence. Checked 2026-09-17 on Ubuntu (coreutils, util-linux, bsdextrautils, bsdutils, procps,
# psmisc, bind9-host, iputils, man-db, make, findutils, ImageMagick, xdg-utils) plus common developer tools.
ENGLISH_COMMANDS = frozenset("""
    go install make find open test build start stop run time date help man watch sort head tail cut join split
    which who whoami yes true false sleep kill top less more file fold look write wall last link unlink touch
    mount umount echo print printf read apply dig host ping see say tree clear reset history source alias
    export set unset env nice wait jobs type trap exit logout login script paste patch diff comm expand units
    factor seq shuf tac tee tr cat copy move free id groups users ps pr nl od fmt locate update upgrade
    shutdown reboot halt sync uptime logger mail view edit play record convert display import compare identify
    montage animate stream composite fetch pull push commit merge search info check verify sign lock unlock log
    list show new serve deploy lint format bench clean init add remove delete purge ask tell explain summarize
    fix debug do done let local return break continue select case shift trust hash enable command builtin rename
    column look grep split base dirname basename expr numfmt tsort timeout pinky stat sum rev hostname
    whereis whatis apropos notify send chat code note notes todo task tasks plan review
""".split())

# Words that make an input read like a sentence, with weights.
SIGNAL_WORDS = {
    **{w: 2 for w in "a an the".split()},
    **{w: 2 for w in "i me my mine we us our you your it its this that these those them their they he she him her".split()},
    **{w: 2 for w in "what why how where when who whom whose which".split()},
    **{w: 3 for w in "please thanks thank pls".split()},
    **{w: 1 for w in ("to and or but for from into onto with without about of in on at by is are was were be been "
                      "can could should would will does did not so then if all some any every why's what's "
                      "there here up out over under again").split()},
}
# `go <word>` where <word> is not a go subcommand is English ("go to", "go ahead", "go home").
GO_SUBCOMMANDS = frozenset("bug build clean doc env fix fmt generate get help install list mod run telemetry test "
                           "tool version vet work".split())
# Commands whose arguments are files or options: a single bare word that is no file here is unusual
# ("install ripgrep", "open settings").
BARE_WORD_ODD = frozenset("install open less more head tail file sort cut join split link unlink mount source "
                          "see view edit display compare identify convert import copy move fold nl od tac".split())
# Their arguments are literal text, so a sentence after them is still most likely a shell command.
LITERAL_TEXT = frozenset("echo printf print say logger wall write notify send".split())
ASSIST_THRESHOLD = 2
SHELLISH = re.compile(r"[|&;<>()`$\\{}\[\]*=]")


def assist_signals(text: str, cwd: str | None = None) -> tuple[int, list[str], str | None]:
    """Score how much a runnable input reads like an English request.

    Returns (score, reasons, first_word). Score 0 means no ambiguity: the first word is not an
    English-word command, or the input uses flags, operators, expansions, globs or quotes.
    """
    trimmed = text.strip()
    if not trimmed or "\n" in trimmed or SHELLISH.search(trimmed) or '"' in trimmed:
        return 0, [], None
    try:
        words = shlex.split(trimmed, posix=True)
    except ValueError:
        return 0, [], None
    if not words:
        return 0, [], None
    first = words[0]
    if first.lower() != first or first not in ENGLISH_COMMANDS:
        return 0, [], first
    args = words[1:]
    if "'" in trimmed or any(a.startswith(("-", "+")) for a in args):
        return 0, [], first  # quoting and flags are shell-typical
    score, reasons = 0, []
    lowered = [a.lower() for a in args]
    for word in lowered:
        bare = word.strip("?.,!:")
        weight = SIGNAL_WORDS.get(bare, 0)
        if weight and bare not in reasons:
            score += weight
            reasons.append(bare)
    if args and args[-1].endswith("?") and args[-1][:-1].isalpha():
        score += 2
        reasons.append("trailing ?")
    if any(a.endswith(",") for a in args) or (args and args[-1].endswith(".") and args[-1][:-1].isalpha()):
        score += 1
        reasons.append("punctuation")
    pathlike = any("/" in a or "~" in a or ("." in a.strip(".,?!") and not a.endswith(".")) for a in args)
    if len(words) > 4 and not pathlike:
        score += 1
        reasons.append(f"{len(words)} plain words")
    if args:
        operand = args[0].strip("?.,!")
        plain = operand.isalpha()
        exists = plain and os.path.exists(os.path.join(cwd or os.getcwd(), operand))
        if plain and not exists:
            if first == "go" and operand not in GO_SUBCOMMANDS:
                score += 2
                reasons.append(f"go {operand} is not a go subcommand")
            elif first in BARE_WORD_ODD and len(args) <= 3:
                score += 2
                reasons.append(f"{first} with a bare word")
    return score, reasons, first


def _first_word_reason(text: str, known: set[str], path: str, cwd: str) -> str:
    try:
        words = shlex.split(text, posix=True)
    except ValueError:
        return ""
    for word in words:
        if ASSIGNMENT.match(word) and any(ch in word for ch in "$(`"):
            return ""  # An expansion inside the assignment hides where the next word starts.
        if ASSIGNMENT.match(word) or word in KEYWORDS_CMD or word in WRAPPERS or word.startswith("-"):
            continue
        return _resolve(word, known, path, cwd)
    return ""


def check_runnable(text: str, known_commands: Iterable[str] = (), path: str | None = None,
                   cwd: str | None = None) -> tuple[bool, str, bool, str]:
    """Static runnability check. NEVER executes input.

    Returns (valid, invalid_reason, syntax_ok, syntax_error).
    """
    ok, error = bash_syntax(text)
    if not ok:
        first = error.splitlines()[0] if error else "invalid Bash syntax"
        return False, "syntax error: " + re.sub(r"^(?:/bin/)?bash: line \d+: ", "", first), ok, error
    known = set(known_commands)
    path = path or os.environ.get("PATH") or os.defpath
    cwd = cwd or os.getcwd()
    try:
        reason = _check_words(text, known, path, cwd)
    except _TooComplex:
        # Heredocs, arithmetic, case and arrays: bash -n already passed, so only the
        # first command word is checked.
        reason = _first_word_reason(text, known, path, cwd)
    return not reason, reason, ok, error


def classify(text: str, mode: str = "auto", known_commands: Iterable[str] = (),
             path: str | None = None, cwd: str | None = None) -> Decision:
    text = validate_input(text)
    if mode not in {"auto", "shell", "agent"}:
        raise ValueError("Unknown input mode.")
    # Prefixes are only interpreted at the start of the composer, not inside scripts.
    explicit = None
    for prefix, destination in (("/shell ", "shell"), ("/agent ", "agent")):
        if text.startswith(prefix):
            text, explicit = text[len(prefix):], destination
            break
    forced = mode if mode != "auto" else explicit
    trimmed = text.strip()
    if not trimmed:
        return Decision("empty", text, "Type a shell command or an agent request.")
    if forced == "agent":
        return Decision("agent", text, "Explicit agent destination; nothing runs in the shell.")
    if forced == "shell":
        valid, reason, ok, error = check_runnable(text, known_commands, path, cwd)
        why = "Explicit terminal destination." if valid else f"Explicit terminal destination · {reason}; the agent will fix it."
        return Decision("shell", text, why, ok, error, valid, reason)

    known = set(known_commands)
    if NATURAL.match(trimmed):
        # A user-defined function named "explain" can still be a real command.
        try:
            first = shlex.split(trimmed, posix=True)[0]
        except (ValueError, IndexError):
            first = trimmed.split(maxsplit=1)[0]
        if first in known and first not in BUILTINS:
            valid, reason, ok, error = check_runnable(text, known, path, cwd)
            if valid:
                return Decision("shell", text, f"Runnable shell alias/function: {first}", ok, error, valid, reason)
        elif first in ENGLISH_COMMANDS and first not in KEYWORDS and "\n" not in trimmed:
            # "make the tests pass", "find the config": a real command in a sentence. Agent is the
            # local guess; the model can confirm.
            valid, reason, ok, error = check_runnable(text, known, path, cwd)
            if valid:
                why = f"“{first}” is a command and an English word; reads like a request"
                return Decision("agent", text, why + " · best guess: agent request", ok, error, valid, reason,
                                True, why)
        return Decision("agent", text, "Natural-language request. Sent only after you submit.")
    valid, reason, ok, error = check_runnable(text, known, path, cwd)
    if valid:
        score, signals, first = assist_signals(trimmed, cwd)
        if score >= ASSIST_THRESHOLD:
            guess = "shell" if first in LITERAL_TEXT else "agent"
            why = f"“{first}” is a command and an English word; reads like a sentence ({', '.join(signals[:4])})"
            return Decision(guess, text, why + (" · best guess: agent request" if guess == "agent"
                                                  else " · best guess: shell command"),
                            ok, error, valid, reason, True, why)
        return Decision("shell", text, "Runnable shell command.", ok, error, valid, reason)
    return Decision("agent", text, f"Not a runnable command ({reason}) · sent to the agent", ok, error, valid, reason)

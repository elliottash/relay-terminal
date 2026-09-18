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
    # Wrong-mode hints: the text reads like a natural-language request, not a command.
    # Terminal mode shows a "switch to agent" hint on errors when this is set; agent mode
    # treats valid text without it as a shell command. High confidence only
    # (_reads_like_request), so typos and real commands never trigger it.
    agent_signal: bool = False
    # May the GUI print invalid_reason under a line auto-routed to the agent? The note explains a
    # mistyped command ("gti status" -> command not found: gti); under a plain request it reads as
    # the failure of a command the user never meant to run (owner report, 2026-09-18:
    # "symlink from ~/projects to here" answered fine, with "command not found: symlink" under it).
    explain_invalid: bool = True

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


def _is_question_word(word: str) -> bool:
    """"really?", "ready?!", "hmm?": a word with its question mark, not a glob. A `?` glob in
    command position only runs when it happens to expand to a program name on PATH, while these
    are the commonest one-word replies there are — they used to route to the shell as
    "runnable" (card #W954)."""
    bare = word.rstrip("?!.,")
    return "?" in word[len(bare):] and bare.isalpha() and not any(ch in bare for ch in "*?[")


def _resolve(word: str, known: set[str], path: str, cwd: str) -> str:
    """Return "" if the word can run as a command, else the reason it cannot."""
    if PLACEHOLDER in word or "$" in word:
        return ""  # Expanded at run time; cannot be judged statically.
    if any(ch in word for ch in "*?[") and not _is_question_word(word):
        # A glob in command position only runs by matching an executable file name, and names a
        # person means to run carry letters. A word with no letters at all — "35*30", "*",
        # "3?" — is arithmetic or a stray glob, never a command (owner report, 2026-09-18).
        if not any(ch.isalpha() for ch in word):
            return f"command not found: {word}"
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
# sentence. The bar for membership is "a person could plausibly start a request with this word":
# "sort these by date" is a request, while `as`, `col`, `dd`, `ed`, `gs`, `w` and application names
# (`dolphin`, `evince`, `totem`) are English or word-like but nobody opens a sentence with them.
# Checked 2026-09-17 against, on this Ubuntu machine, the 4327 executables on PATH intersected with
# /usr/share/dict/{american,british}-english (241 hits), plus `compgen -b`, `compgen -k` and
# `busybox --list`. Package families in that intersection: coreutils, util-linux, bsdextrautils,
# bsdutils, procps, findutils, grep, diffutils, patch, tar, less, ncurses-bin, man-db, bind9-host,
# bind9-dnsutils, iputils, mailutils, cups-client, xdg-utils, ImageMagick, binutils, texlive,
# graphviz, git and the python/node/ruby toolchains. Words that are not installed here come from
# documentation and are kept because Relay also runs elsewhere: macOS/BSD (say, banner, jot, sample,
# apply, log, talk, fetch), other distros (tree, units, spell, tidy, wipe, dump, restore, at, batch,
# accept, reject, disable), non-bash shells (where in zsh/csh, print in ksh) and common
# developer tools (go, just, task, todo, note, plan, review, bundle, pass, code, chat).
# Derivation and measurements: docs/qa_evidence/2026-09-17-router-english-commands/.
ENGLISH_COMMANDS = frozenset("""
    accept add alias animate apply apropos ask at banner basename batch bench bind break browse build
    builtin bundle cancel case cat chat check clean clear code column comm command commit compare
    composite continue convert copy cut date debug delete deploy diff dig dirname disable display do done
    dump echo edit eject enable env exit expand explain export expr factor false fetch file find fix fmt
    fold format free from go grep groups halt hash head help history host hostname id identify import info
    init install jobs join jot just kill last less let link lint list local locate lock log logger login
    logout look mail make man merge montage more mount move new nice nl note notes notify numfmt od open
    pass paste patch ping pinky plan play please pr print printf prove prune ps pull purge push read
    reboot record reject remove rename reset restore resume return rev review route run sample say screen
    script search see select send seq serve set shift show shred shuf shutdown sign sleep sort source
    spell split start stat stop stream strip sum summarize suspend sync tac tail talk tar task tasks tee
    tell test tidy time timeout todo top touch tr transform trap tree true truncate trust tsort type
    umount units unlink unlock unset unzip update upgrade uptime users verify view wait wall watch whatis
    where whereis which who whoami wipe write yes zip
""".split())

# Words that make an input read like a sentence, with weights. Two independent weight-1 words (or one
# article or pronoun) are enough to ask; a lone weight-1 word is not, so `shutdown now` and `make all`
# stay in the shell. Contractions are unreachable in practice — an apostrophe zeroes the score above,
# and bash -n rejects the unbalanced quote first — but are kept for readers.
SIGNAL_WORDS = {
    **{w: 2 for w in "a an the".split()},
    **{w: 2 for w in "i me my mine we us our you your it its this that these those them their they he she him her".split()},
    **{w: 2 for w in "what why how where when who whom whose which".split()},
    **{w: 2 for w in ("myself yourself itself himself herself ourselves themselves everyone everybody someone "
                      "somebody anyone anybody nobody everything something anything nothing").split()},
    **{w: 3 for w in "please thanks thank pls".split()},
    **{w: 1 for w in ("to and or but for from into onto with without about of in on at by is are was were be been "
                      "can could should would will does did not so then if all some any every why's what's "
                      "there here up out over under again").split()},
    **{w: 1 for w in ("through between across against along among around before after behind below beside besides "
                      "beyond during except inside outside per since than toward towards until upon via within "
                      "near like off down back away together because while whether unless though although "
                      "instead").split()},
    **{w: 1 for w in ("have has had am being doing done might must shall need needs want wants let lets "
                      "also just still only very too really actually maybe probably properly correctly "
                      "now today tomorrow yesterday once ever never always often sometimes").split()},
    **{w: 1 for w in ("much more most less least few many several each both another other others same such "
                      "everywhere somewhere anywhere nowhere").split()},
}
# `go <word>` where <word> is not a go subcommand is English ("go to", "go ahead", "go home").
GO_SUBCOMMANDS = frozenset("bug build clean doc env fix fmt generate get help install list mod run telemetry test "
                           "tool version vet work".split())
# Commands whose arguments are files or options: a single bare word that is no file here is unusual
# ("install ripgrep", "open settings"). Left out on purpose: `make` and `watch` (targets and commands
# are not files), `grep` (a bare pattern is normal), `look` (its argument is a word, not a file) and
# `unzip` (`unzip archive` is normal: it appends .zip itself).
BARE_WORD_ODD = frozenset("install open less more head tail file sort cut join split link unlink mount source "
                          "see view edit display compare identify convert import copy move fold nl od tac "
                          "find patch strip truncate shred prove tidy spell sum transform".split())
# Their arguments are literal text, so a sentence after them is still most likely a shell command.
LITERAL_TEXT = frozenset("echo printf print say logger wall write notify send banner".split())

# Builtins that only mean something inside a loop or a function: bash refuses them at a prompt
# ("continue: only meaningful in a `for', `while', or `until' loop"), so a line that is one of
# these words alone can only have been meant for the agent — and "continue" is the word people
# use to tell it to carry on (owner report, 2026-09-18). Inside a longer line they are ordinary
# shell, and an explicit terminal destination still runs them.
LOOP_ONLY = frozenset("continue break return".split())
ASSIST_THRESHOLD = 2
# Operators and expansions that only appear in shell input. Globs (`*`, `[...]`) and `~` are left out
# on purpose: "look at my letters in ~/admin/Advisees.*.docx for my style" is an English request that
# happens to name a path, and `look` is also a command (2026-09-17 owner report).
SHELLISH = re.compile(r"[|&;<>()`$\\{}=]")
GLOBBISH = re.compile(r"[*\[\]]")


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
    # A glob or a path alone does not make it a command, but it is weak evidence for the shell.
    if GLOBBISH.search(trimmed):
        score -= 1
        reasons.append("glob")
    if args and args[-1].endswith("?") and args[-1][:-1].isalpha():
        score += 2
        reasons.append("trailing ?")
    if any(a.endswith(",") for a in args) or (args and args[-1].endswith(".") and args[-1][:-1].isalpha()):
        score += 1
        reasons.append("punctuation")
    # "look for cleanup opportunities", "search for the leak", "check on the build": an
    # English-word command followed straight away by a preposition or an article is a sentence,
    # not an invocation — no command takes one of these as its first operand (owner report,
    # 2026-09-17: "look for cleanup opportunities" ran in the shell).
    # Only words no command takes as an operand. "all", "up", "out", "in" and "on" are left out:
    # `make all`, `look up`, `git in` and friends are real.
    LEAD_IN = {"for", "at", "into", "through", "about", "the", "a", "an", "my", "our",
               "these", "those", "whether", "why", "how", "what"}
    if args and args[0].strip("?.,!:").lower() in LEAD_IN and "lead-in" not in reasons:
        score += 2
        reasons.append("lead-in")
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


# Words no command takes as its first operand. A second word from this set means the line is a
# sentence about something, not an invocation with an argument.
SENTENCE_LEAD = frozenset("""
a an the this that these those my our your its from to into onto with without for about
at here there please and or of in on over under again back now then some any every
""".split())


def _one_edit_apart(typed: str, command: str) -> bool:
    """True when one insertion, deletion, substitution or swap turns `typed` into `command`.

    The shapes a typo actually takes at a prompt: gti/git, pyton/python, docekr/docker, lls/ls.
    Two edits apart is a different word, not a slip — resume/resize must not match.
    """
    if typed == command:
        return False
    short, long = sorted((typed, command), key=len)
    if len(long) - len(short) > 1:
        return False
    if len(typed) == len(command):
        differing = [i for i, (a, b) in enumerate(zip(typed, command)) if a != b]
        if len(differing) == 1:
            return True
        return (len(differing) == 2 and differing[1] == differing[0] + 1
                and typed[differing[0]] == command[differing[1]]
                and typed[differing[1]] == command[differing[0]])
    i = j = skipped = 0
    while i < len(short) and j < len(long):
        if short[i] != long[j]:
            if skipped:
                return False
            skipped, j = 1, j + 1
            continue
        i, j = i + 1, j + 1
    return True


def _looks_mistyped(word: str, known: set[str], path: str) -> bool:
    """True when `word` is one slip away from a command this machine actually has."""
    if len(word) < 2:
        return False
    candidates = known | path_executables(path)
    return any(_one_edit_apart(word, name) for name in candidates
               if abs(len(name) - len(word)) <= 1 and name[:1] in {word[:1], word[1:2]})


# Replies and sentence openers nobody types meaning a program. The typo test below cannot tell them
# from slips — "ok" is one edit from `od`, "no" from `nl`, "cool" from `col`, "lets" from `let` —
# and they are the commonest short lines there are (card #W954). "wait" is also a real command; a
# lone "wait" goes to the agent through LONE_REPLY, and this keeps the note quiet for "Wait" or "wait!".
REPLY_WORDS = frozenset("""
ok okay k kk yes yep yeah yea yup ya no nope nah sure cool good great fine nice perfect awesome
thanks thx ty hi hey hello sorry right alright agreed lgtm hmm oh oops well go stop wait
lets maybe also actually anyway btw
""".split())
# Words that, typed alone, are a reply rather than an invocation: a lone `yes` prints "y" until
# interrupted, a lone `nice` prints the niceness. `wait`, `true` and `false` are builtins that do
# nothing a person can see at a prompt (`wait` returns at once without background jobs, the other two
# only set $?), while "wait", "true" and "false" are everyday answers to the agent. `done` alone is
# a bash syntax error, and "done" is the commonest way to say a step is finished. Sent to the agent
# like LOOP_ONLY; with any argument (`yes | rm -i *`, `nice -n 5 make`, `wait %1`, `true && ls`) they
# are shell again, and an explicit terminal destination still runs them. Left in the shell on
# purpose: `times`, `test`, `exit` and friends, which are not replies — and `ls`, `pwd`, `clear`,
# `history`, `jobs`, which print something useful on their own (card #W954).
LONE_REPLY = frozenset("yes nice wait true false done".split())
# Sentence punctuation that sticks to a word: "yeah,", "ok.", "wait...", "hmm…", "really?!".
SENTENCE_TAIL = ",.:;?!…\"'”’)"
DASHES = "—–"
CONTRACTION = re.compile(r"(?<=[A-Za-z])['’](?=[A-Za-z])")


def _contractions_only(text: str) -> bool:
    """True when every quote in the line is an apostrophe inside a word — "don't", "let's",
    "it's" — and nothing else in it is shell. Bash reads the lone apostrophe as an unterminated
    string, but that syntax error is English, not a command (card #W954)."""
    if not CONTRACTION.search(text):
        return False
    rest = CONTRACTION.sub("", text)
    if "'" in rest or '"' in rest or "\n" in rest or SHELLISH.search(rest) or GLOBBISH.search(rest):
        return False
    return not any(w.startswith(("-", "+")) and w not in {"-", "--"} for w in rest.split()[1:])


def _bare_word(word: str) -> tuple[str, bool]:
    """The word without sentence punctuation stuck to it, and whether there was any.

    "yeah," -> ("yeah", True); "yeah—do" -> ("yeah", True); "don’t" -> ("dont", True);
    "“yeah”" -> ("yeah", True);
    "kubectl2" -> ("kubectl2", False), which the caller still reads as a name."""
    bare, punctuated = word, False
    for dash in DASHES:
        if dash in bare:
            head, *tail = bare.split(dash)
            if all(part.isalpha() or not part for part in tail):
                bare, punctuated = head, True
    stripped = bare.lstrip("“‘(").rstrip(SENTENCE_TAIL)
    if stripped != bare:
        bare, punctuated = stripped, True
    joined = CONTRACTION.sub("", bare)
    if joined != bare:
        bare, punctuated = joined, True
    return bare, punctuated


# A first word in straight quotes with nothing but sentence punctuation after it: "yeah" is fine.
QUOTED_FIRST = re.compile(r"""^(["'])([^\W\d_]+)\1(?=[,.:;?!…]*(?:\s|$))""")
# A semicolon written as punctuation: a space or the end of the line after it ("hmm; not sure").
PROSE_SEMICOLON = re.compile(r";(?=\s|$)")


def _unquote_first_word(text: str) -> str:
    """The line with straight quotes around its first word removed, when that word is letters only.

    `"yeah" is fine` and `'ok' then` quote a word the way prose does; the shell reads the same
    command name either way, so the line is judged as if unquoted — the typo test and the
    real-command test still apply (`"gti" status` is still a slip). A quoted path or a name with
    other characters (`"./run.sh"`, `"pip4" x`) does not match and keeps today's behaviour (card #W954)."""
    match = QUOTED_FIRST.match(text)
    return match.group(2) + text[match.end():] if match else text


def _semicolon_prose(text: str, known: Iterable[str], path: str | None) -> bool:
    """True for a line whose only shell syntax is sentence semicolons, when no segment reads as an
    attempted command: "hmm; not sure", "ok; let me think".

    Each `;`-segment must start with something that is not a command — a reply word, a function
    word (SIGNAL_WORDS, SENTENCE_LEAD), or a first word explain_invalid would stay quiet about on
    its own ("try again", "don't know") — or with an English-word command in a sentence
    (assist_signals clears the threshold: "let me think").
    A segment that starts with a real command or a near-typo of one (`hmm; ls`, `gti; ls`,
    `cd /tmp; mkae`) means a command was meant, and the note stays. The whole line must also carry
    at least one reply or function word, so `xyzzy; frob` is not a sentence (card #W954)."""
    if not PROSE_SEMICOLON.search(text) or ";" in PROSE_SEMICOLON.sub("", text):
        return False
    rest = CONTRACTION.sub("", PROSE_SEMICOLON.sub("", text))
    if "\n" in rest or "'" in rest or '"' in rest or SHELLISH.search(rest) or GLOBBISH.search(rest):
        return False
    known = set(known)
    path = path or os.environ.get("PATH") or os.defpath
    function_words = REPLY_WORDS | LOOP_ONLY | SENTENCE_LEAD | SIGNAL_WORDS.keys()
    for segment in (s.strip() for s in text.split(";")):
        words = segment.split()
        if not words:
            continue
        if any(w.startswith(("-", "+")) and w not in {"-", "--"} for w in words[1:]):
            return False                        # flags: a command was meant
        first = words[0]
        if first in known or first in BUILTINS or first in KEYWORDS or on_path(first, path):
            if assist_signals(segment)[0] < ASSIST_THRESHOLD:
                return False                    # "hmm; ls": ls would run, so a command was meant
            continue                            # "ok; let me think": a command word in a sentence
        if _bare_word(first)[0].lower() in function_words:
            continue                            # "not sure", "the other one": "not" is no slip of `nl`
        if explain_invalid(segment, f"command not found: {first}", known, path):
            return False                        # "gti; ls", "cd /tmp; mkae", "ok; Docker ps"
    words = (_bare_word(w)[0].lower() for w in text.replace(";", " ").split())
    return any(w in function_words for w in words)


def explain_invalid(text: str, reason: str, known: Iterable[str] = (), path: str | None = None) -> bool:
    """Whether the GUI should print `reason` under a line auto-routed to the agent.

    The note is for the moment a command was meant and mistyped ("gti status"), so it wants real
    evidence of that: shell syntax, flags, a name that is not a plain English word, or a word one
    slip away from a command that exists here. Everything else is language — a lone "resume", or a
    sentence whose first word happens not to be a program — and naming its first word reads as the
    failure of a command the user never ran (owner reports, 2026-09-18).

    Two kinds of shell syntax are read as writing too (card #W954): a first word in straight quotes
    (`"yeah" is fine`, judged as the unquoted line) and semicolons in a sentence none of whose parts
    starts with a command or a slip of one (`hmm; not sure`, see _semicolon_prose).

    Sentence punctuation is language too (card #W954, "yeah, see if there is a clear issue…" got
    "command not found: yeah,"): a comma, full stop, colon, question or exclamation mark, ellipsis,
    closing quote or dash stuck to the first word, an apostrophe inside a word ("let's", "don't"),
    and a capitalised first word ("Yeah", "Sure") are how people write, not how commands look.
    """
    trimmed = text.strip()
    unquoted = _unquote_first_word(trimmed)
    if unquoted != trimmed:
        return explain_invalid(unquoted, reason, known, path)   # '"yeah" is fine' reads as 'yeah is fine'
    if reason and _semicolon_prose(trimmed, known, path):
        return False                            # "hmm; not sure": a semicolon in a sentence
    if reason and _contractions_only(trimmed):
        return False                            # "don't break the build": the quote is English
    prefix = "command not found: "
    if not reason.startswith(prefix):
        return bool(reason)                     # a syntax error is about a command either way
    word = reason[len(prefix):]
    if not any(ch.isalpha() for ch in word):
        return False                            # "35 * 30" -> "command not found: 35"
    if "\n" in trimmed or SHELLISH.search(trimmed) or GLOBBISH.search(trimmed) or '"' in trimmed:
        return True
    try:
        words = shlex.split(trimmed, posix=True)
    except ValueError:
        return True
    if any(a.startswith(("-", "+")) and a not in {"-", "--"} for a in words[1:]):
        return True                             # flags are nobody's English; a lone dash is
    bare, punctuated = _bare_word(word)
    if not bare.isalpha():
        return True                             # kubectl2, pip3, ./run.sh: a name, not a word
    lowered = bare.lower()
    if lowered in REPLY_WORDS or lowered in LOOP_ONLY:
        return False                            # "ok", "Yes", "Continue", "nope."
    if len(words) > 1:
        # Short English words sit one edit from some command or other ("add" from "adb"), so a line
        # that reads as a sentence is not rescued by the typo test below.
        if words[1].strip(SENTENCE_TAIL).lower() in SENTENCE_LEAD:
            return False
        operandish = any("/" in a or "~" in a or "." in a.strip(".,?!…") for a in words[1:])
        if len(words) > 3 and not operandish:
            return False
    if punctuated:
        return False                            # "yeah,", "ok.", "wait... what": a sentence
    known = set(known)
    path = path or os.environ.get("PATH") or os.defpath
    if lowered != bare and (lowered in known or lowered in BUILTINS or on_path(lowered, path)):
        return True                             # "Docker ps", "Ls": a real command, capitalised
    # "Gti status" is still a typo with a capital; "Yeah" and "Sounds good" are not near anything.
    return _looks_mistyped(lowered, known, path)


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


def _reads_like_request(trimmed: str, known: set[str], valid: bool, cwd: str | None) -> bool:
    """True when the text reads like a natural-language request rather than a command.

    High confidence only, in two shapes: a NATURAL prefix whose first word is not a live
    alias/function ("explain this error"), or runnable input whose assist score clears
    the threshold ("find the largest files", "sort these results by date"). Used by the
    fixed modes for the wrong-mode hints; auto mode routes on its own.
    """
    if trimmed in LOOP_ONLY:
        return True    # a lone "continue" in terminal mode is a wrong-mode submission
    if not NATURAL.match(trimmed):
        return valid and assist_signals(trimmed, cwd)[0] >= ASSIST_THRESHOLD
    try:
        first = shlex.split(trimmed, posix=True)[0]
    except (ValueError, IndexError):
        first = trimmed.split(maxsplit=1)[0]
    if first in known and first not in BUILTINS:
        return False   # a live alias or function named like the word still runs
    return True


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
    known = set(known_commands)
    if forced in {"agent", "shell"}:
        # Fixed modes get the full picture so the GUI can flag a wrong-mode submission:
        # agent mode also learns whether the text is a runnable command, and both learn
        # whether it reads like a request (agent_signal). Auto mode decides below.
        valid, reason, ok, error = check_runnable(text, known_commands, path, cwd)
        signal = _reads_like_request(trimmed, known, valid, cwd)
        if forced == "agent":
            return Decision("agent", text, "Explicit agent destination; nothing runs in the shell.",
                            syntax_ok=ok, syntax_error=error, valid=valid, invalid_reason=reason,
                            agent_signal=signal)
        why = "Explicit terminal destination." if valid else f"Explicit terminal destination · {reason}; the agent will fix it."
        return Decision("shell", text, why, syntax_ok=ok, syntax_error=error, valid=valid,
                        invalid_reason=reason, agent_signal=signal)

    if trimmed in LOOP_ONLY:
        # Nothing to explain under the line: the word is spelt correctly and no command was meant,
        # so a "command not found" note would read as the failure of something never run.
        return Decision("agent", text, f"“{trimmed}” means nothing outside a loop; sent to the agent",
                        agent_signal=True, explain_invalid=False)
    if trimmed in LONE_REPLY:
        # A lone "yes" would print y until interrupted, a lone "wait" does nothing: at a prompt
        # they are answers, not commands.
        return Decision("agent", text, f"“{trimmed}” on its own is a reply; sent to the agent",
                        agent_signal=True, explain_invalid=False)
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
        return Decision("agent", text, "Natural-language request. Sent only after you submit.", agent_signal=True)
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
    return Decision("agent", text, f"Not a runnable command ({reason}) · sent to the agent", ok, error, valid, reason,
                    explain_invalid=explain_invalid(text, reason, known, path))

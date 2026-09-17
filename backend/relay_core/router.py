# SPDX-License-Identifier: GPL-3.0-or-later
"""Conservative, local-only input classification. Parsing NEVER executes input."""
from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
from dataclasses import asdict, dataclass
from typing import Iterable

MAX_INPUT = 131072
BUILTINS = frozenset("alias bg bind break builtin cd command compgen complete compopt continue declare dirs disown echo enable eval exec exit export false fc fg getopts hash help history jobs kill let local logout mapfile popd printf pushd pwd read readarray readonly return set shift shopt source test times trap true type typeset ulimit umask unalias unset wait . : [ [[ !".split())
NATURAL = re.compile(r"^(?:why\b|how\b|what\b|when\b|who\b|where (?:is|are|can|should)\b|(?:can|could|would|will) you\b|please\b|explain\b|debug\b|summari[sz]e\b|refactor\b|implement\b|fix (?:the|this|my|a|all)\b|help me\b|(?:show|tell) me\b|(?:write|create|make|build) (?:a|an|the|me)\b|find (?:the|my|all the)\b|list (?:the|my|all the)\b)", re.I)
SHELL_START = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_]*=|[./~][^\s]*|\$\{|\$[A-Za-z_]|(?:for|while|until|if|case|function|select)\s|\(\(|\[\[|\{|\(|>|<)")

@dataclass(frozen=True)
class Decision:
    route: str
    text: str
    reason: str
    syntax_ok: bool = True
    syntax_error: str = ""

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


def classify(text: str, mode: str = "auto", known_commands: Iterable[str] = (),
             path: str | None = None) -> Decision:
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
        ok, error = bash_syntax(text)
        return Decision("shell", text, "Explicit terminal destination.", ok, error)

    # Live shell aliases/functions take precedence over language heuristics.
    try:
        words = shlex.split(trimmed, posix=True)
        first = words[0] if words else ""
    except ValueError:
        first = trimmed.split(maxsplit=1)[0]
    known = set(known_commands)
    if first in known and not NATURAL.match(trimmed):
        ok, error = bash_syntax(text)
        return Decision("shell", text, f"Recognized shell command: {first}", ok, error)
    if NATURAL.match(trimmed):
        # A user-defined function named "explain" can still be a real command.
        if first in known and first not in BUILTINS:
            ok, error = bash_syntax(text)
            return Decision("ambiguous", text, "This resembles a request and a shell alias/function. Choose a destination.", ok, error)
        return Decision("agent", text, "Natural-language request. Sent only after you submit.")
    if first in BUILTINS or shutil.which(first, path=path or os.defpath):
        ok, error = bash_syntax(text)
        return Decision("shell", text, f"Recognized executable or shell builtin: {first}", ok, error)
    if SHELL_START.match(trimmed):
        ok, error = bash_syntax(text)
        return Decision("shell", text, "Explicit shell path, assignment, or shell syntax.", ok, error)
    # Syntax is reported so a terminal-first frontend can skip the shell for non-Bash text.
    ok, error = bash_syntax(text)
    return Decision("ambiguous", text, "Unrecognized or ambiguous input.", ok, error)

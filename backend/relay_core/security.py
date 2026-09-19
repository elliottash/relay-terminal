# SPDX-License-Identifier: GPL-3.0-or-later
"""The Security section's policy, as the worker enforces it (card #3KB7).

Owner, 2026-09-19: "add a security options menu with various secruity options like that, not just
relay - relay, but more of the approvals options on warp."

Relay allows by default and never interrupts to ask: `docs/ROADMAP.md` line 43 settled against
per-action approvals, and nothing here reintroduces one. So where Warp's execution profiles offer
`always_allow` / `always_ask` / `never` per capability, this module offers only the two ends —
and where Warp reaches for "ask", Relay denies, confines, or bounds:

* **A command denylist.** There is no allowlist twin. An allowlist only means something when the
  default is "ask" or "deny", and Relay's is "allow".
* **Extra readable folders.** The file tools are confined to one workspace
  (`tools.Workspace.resolve`); this widens *reading* to folders the user named, and never writing.
* **Extra secret patterns.** `tools.looks_secret` has a fixed list; this extends it, and cannot
  shrink it.

**What a denylist is and is not.** It is a guardrail against a model doing the wrong obvious thing,
not a sandbox against an adversary: `run_command` runs a Bash line, and a line can always be spelled
another way (`r''m`, a variable, a script). The real containment is the workspace, the secret-file
guard and systemd isolation (`src/Isolation.h`). This is checked before the command runs so that a
rule the user wrote is honoured and *said*, not so that it cannot be evaded. Anything that claims
otherwise in a setting's detail line would be a lie.

Everything here is pure: the settings arrive over the protocol (section 12.1, beside `max_steps`),
and the Pane sends them from `QSettings`. Nothing reads the filesystem except `readable_root`,
which only resolves paths.
"""
from __future__ import annotations

import fnmatch
import os
import re
import shlex
from dataclasses import dataclass, field
from pathlib import Path

# A rule, a pattern or a folder longer than this is a mistake, not a policy.
MAX_RULE = 200
MAX_RULES = 200
# A user's secret pattern is a regex; a catastrophic one must not hang the worker on every path
# component, so the pattern itself is bounded and compiled once.
MAX_PATTERN = 200


@dataclass(frozen=True)
class Policy:
    """What the Security section allows and refuses. Empty everywhere is today's behaviour."""

    command_denylist: tuple[str, ...] = ()
    readable_roots: tuple[Path, ...] = ()
    secret_patterns: tuple[str, ...] = ()
    _compiled: tuple[re.Pattern, ...] = field(default=(), repr=False, compare=False)

    def is_empty(self) -> bool:
        return not (self.command_denylist or self.readable_roots or self.secret_patterns)


EMPTY = Policy()


def _clean(values, *, limit: int = MAX_RULE) -> tuple[str, ...]:
    """The list as the GUI sent it: strings only, trimmed, deduplicated, order kept, bounded."""
    if values is None:
        return ()
    if not isinstance(values, list):
        raise ValueError("A security list must be an array of strings.")
    out: list[str] = []
    for value in values[:MAX_RULES]:
        if not isinstance(value, str):
            raise ValueError("A security list must be an array of strings.")
        text = value.strip()
        if not text or len(text) > limit or "\n" in text or "\r" in text:
            continue
        if text not in out:
            out.append(text)
    return tuple(out)


def validate(request: dict) -> dict:
    """The security keys of `configure` / `set_agent_options` (protocol 12.1). Only keys that are
    present are returned, so an older GUI changes nothing."""
    out: dict = {}
    if request.get("command_denylist") is not None:
        out["command_denylist"] = list(_clean(request["command_denylist"]))
    if request.get("readable_roots") is not None:
        roots = []
        for value in _clean(request["readable_roots"], limit=4096):
            expanded = os.path.expanduser(value)
            if not os.path.isabs(expanded):
                raise ValueError("A readable folder must be an absolute path.")
            roots.append(os.path.normpath(expanded))
        out["readable_roots"] = roots
    if request.get("secret_patterns") is not None:
        patterns = []
        for value in _clean(request["secret_patterns"], limit=MAX_PATTERN):
            try:
                re.compile(value)
            except re.error as error:
                raise ValueError(f"Secret pattern {value!r} is not a valid regular expression: {error}") from None
            patterns.append(value)
        out["secret_patterns"] = patterns
    return out


def policy_from(options: dict) -> Policy:
    """A Policy from already-validated option values. A pattern that will not compile is dropped
    rather than raised: `validate` is where a bad one is reported, and a worker that has somehow
    been handed one must still run."""
    patterns = tuple(options.get("secret_patterns") or ())
    compiled = []
    for pattern in patterns:
        try:
            compiled.append(re.compile(pattern))
        except re.error:
            continue
    return Policy(
        command_denylist=tuple(options.get("command_denylist") or ()),
        readable_roots=tuple(Path(root) for root in (options.get("readable_roots") or ())),
        secret_patterns=patterns,
        _compiled=tuple(compiled),
    )


# ----- the command denylist ---------------------------------------------------------------------

# Where one command ends and the next begins in a Bash line. Splitting on these is what lets a rule
# of `rm` refuse `ls && rm -rf x`, which matching the whole line as one string would miss.
_SEPARATORS = ("&&", "||", "|&", ";", "|", "\n")


def _segments(command: str) -> list[str]:
    parts = [command]
    for separator in _SEPARATORS:
        parts = [piece for part in parts for piece in part.split(separator)]
    return [part.strip() for part in parts if part.strip()]


def segments(command: str) -> list[str]:
    """Where one command in a Bash line ends and the next begins. Public because the recursive-walk
    cost guard in relay_core/tools.py (card #2Y96) has to split a line the same way this does."""
    return _segments(command)


def programs(segment: str) -> list[str]:
    """Every program name one segment runs: the prefix words it runs through (`sudo`, `env`, …) and
    then the program itself. Public for the same reason `segments` is — the approval classifier
    (relay_core/approvals.py, card #K2FV) has to read a line exactly as the denylist does, or the
    two would disagree about what `sudo rm` is."""
    return _programs(segment)


# Words that stand in front of the program without being the thing that runs. A rule of `apt` has
# to refuse `sudo apt`, so these are stepped over to find the program — and a rule of `sudo` has to
# refuse `sudo apt` too, which is the whole reason `_programs` returns them as well.
_PREFIXES = {"sudo", "doas", "command", "builtin", "nohup", "time", "exec", "env", "nice", "ionice",
             "stdbuf", "xargs"}


def _programs(segment: str) -> list[str]:
    """Every program name a segment names: the prefix words it runs *through* (`sudo`, `env`, …)
    and then the program itself. Falls back to whitespace splitting when the segment will not lex,
    so an unbalanced quote does not make a segment unmatchable."""
    try:
        words = shlex.split(segment)
    except ValueError:
        words = segment.split()
    found: list[str] = []
    for word in words:
        if "=" in word and not word.startswith("-") and word.split("=", 1)[0].isidentifier():
            continue  # FOO=bar before the program
        if word.startswith("-"):
            continue
        base = os.path.basename(word)
        found.append(base)
        if base not in _PREFIXES:
            break   # this is the program; anything after it is an argument
    return found


def _matches(rule: str, command: str, segments: list[str]) -> bool:
    names = [name for part in segments for name in _programs(part)]
    if any(character in rule for character in "*?["):
        if fnmatch.fnmatchcase(command, rule) or any(fnmatch.fnmatchcase(part, rule) for part in segments):
            return True
        return any(fnmatch.fnmatchcase(name, rule) for name in names)
    # A bare rule names a program: `rm` refuses `rm -rf`, `sudo rm` and `ls && rm x`, but not a file
    # called rm.txt and not `rmdir`. `sudo` refuses `sudo apt`, because a prefix word is a program
    # the line runs too.
    return os.path.basename(rule) in names


def denied_command(policy: Policy, command: str) -> str | None:
    """The first denylist rule that refuses `command`, or None. See the module docstring on what a
    denylist is worth: this is honoured, not unevadable."""
    if not policy.command_denylist or not isinstance(command, str) or not command.strip():
        return None
    segments = _segments(command)
    for rule in policy.command_denylist:
        if _matches(rule, command.strip(), segments):
            return rule
    return None


def refusal(rule: str) -> str:
    """What the model is told. It names the rule so the user can find it in Options, and says who
    can change it, because the model retrying a spelling that evades the rule is the wrong move."""
    return (f"Refused by this Relay's command denylist (the rule is {rule!r}, in Options › Security). "
            f"Do not look for another way to spell it: tell the user what you wanted to run and why.")


# ----- extra readable folders -------------------------------------------------------------------


def readable_root(policy: Policy, resolved: Path) -> Path | None:
    """The configured folder that `resolved` sits in, or None. Reading only: a write is never
    widened past the workspace, so the caller asks about this for a read and not for a write."""
    for root in policy.readable_roots:
        try:
            if resolved == root or resolved.is_relative_to(root):
                return root
        except ValueError:
            continue
    return None


# ----- extra secret patterns --------------------------------------------------------------------


def extra_secret(policy: Policy, part: str) -> bool:
    """Whether a user pattern blocks this one path component. `tools.looks_secret`'s built-ins are
    checked separately and always: this can only add."""
    return any(pattern.search(part) for pattern in policy._compiled)

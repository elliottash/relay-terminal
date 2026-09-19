# SPDX-License-Identifier: AGPL-3.0-or-later
"""Which actions stop and ask before they happen (card #K2FV).

Owner, 2026-09-19: "i think we need to add ask back for the unapproved risky things that
risk-averse users will want -- eg file removals, moves, edits, reads outside the project, etc" and
"i want relay to be a smoother experience than most by default."

This does not undo `docs/ROADMAP.md:43`. Allowing everything stays the recommended setting and stays
what a smooth Relay does; it is now a choice the user makes on the first launch rather than one
Relay makes silently, and a user who wants approvals gets a checklist instead of having to leave.

Each capability is `allow` or `ask`. There is no "never": `relay_core/security.py`'s command
denylist is where never lives, and two mechanisms for one thing would be two places to look.

**`DELETE_OR_MOVE` and `NETWORK` are classifiers, not proofs.** They read the program names out of
a Bash line, so they catch `rm -rf build` and `sudo mv a b` and miss anything spelled another way —
a variable, a script, a here-doc. That is the same honest limit the denylist has, and the settings
row says so. What actually contains a command is the workspace, the secret-file guard and systemd
isolation; this is here so that a user who asked to be stopped is stopped at the obvious moment.

There is deliberately **no row for running a command at all** (owner: "leave it out"). It is the row
a cautious user most wants and the one that would make Relay unusable, since a single turn runs
dozens of commands; the two classifiers cover the commands worth stopping.
"""
from __future__ import annotations

import os
from dataclasses import dataclass

from . import security

# The capabilities, in the order the checklist shows them.
EDIT = "edit"
CREATE = "create"
DELETE_OR_MOVE = "delete_or_move"
READ_OUTSIDE = "read_outside"
TERMINAL = "terminal"
PROGRAM = "program"
NETWORK = "network"

CAPABILITIES = (EDIT, CREATE, DELETE_OR_MOVE, READ_OUTSIDE, TERMINAL, PROGRAM, NETWORK)

# What each row says it is for, one line, reused by the card and by the settings row so the two
# cannot drift into describing different things.
LABELS = {
    EDIT: "change a file that already exists",
    CREATE: "create a new file",
    DELETE_OR_MOVE: "delete or move files",
    READ_OUTSIDE: "read a file outside this pane's workspace",
    TERMINAL: "run a command in your own terminal",
    PROGRAM: "type into the program running in your terminal",
    NETWORK: "run a command that reaches the network",
}

# Before the first-launch choice is answered, these ask. Not allow-everything: "you have to
# explicitly pick that" only means something if not picking is different. `create` and `network`
# stay out of it so a fresh Relay can still do the ordinary thing without a card on every step.
CAUTIOUS = (EDIT, DELETE_OR_MOVE, READ_OUTSIDE, TERMINAL, PROGRAM)

# Programs that delete or move. `chmod`/`chown` are here only in their recursive form, which is
# checked separately, because the plain form is not what the owner asked to be stopped for.
_DESTRUCTIVE = {"rm", "rmdir", "unlink", "shred", "mv", "truncate", "dd", "mkfs", "fdisk",
                "parted", "wipefs"}
_RECURSIVE_PERMS = {"chmod", "chown", "chgrp"}
# Programs that reach the network. `git` only for the subcommands that do.
_NETWORK = {"curl", "wget", "scp", "sftp", "rsync", "ssh", "nc", "ncat", "telnet", "ftp"}
_NETWORK_GIT = {"push", "fetch", "pull", "clone", "remote", "submodule"}


@dataclass(frozen=True)
class Policy:
    """Which capabilities ask. Everything absent means allow, which is the recommended setting."""

    ask: frozenset[str] = frozenset()
    chosen: bool = False          # has the user answered the first-launch screen?

    def asks(self, capability: str) -> bool:
        return capability in (self.ask if self.chosen else frozenset(CAUTIOUS))


ALLOW_ALL = Policy(ask=frozenset(), chosen=True)


def validate(request: dict) -> dict:
    """The approval keys of `configure` / `set_agent_options` (protocol 12.1). Only keys present are
    returned, so an older GUI changes nothing."""
    out: dict = {}
    if request.get("approvals_ask") is not None:
        value = request["approvals_ask"]
        if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
            raise ValueError("approvals_ask must be an array of strings.")
        unknown = [item for item in value if item not in CAPABILITIES]
        if unknown:
            raise ValueError(f"Unknown approval capability: {', '.join(sorted(unknown))}.")
        out["approvals_ask"] = sorted(set(value))
    if request.get("approvals_chosen") is not None:
        if not isinstance(request["approvals_chosen"], bool):
            raise ValueError("approvals_chosen must be a boolean.")
        out["approvals_chosen"] = request["approvals_chosen"]
    return out


def policy_from(options: dict) -> Policy:
    return Policy(ask=frozenset(options.get("approvals_ask") or ()),
                  chosen=bool(options.get("approvals_chosen")))


# ----- what a call needs approval for ------------------------------------------------------------


def _recursive(segment: str) -> bool:
    words = segment.split()
    return any(word in ("-R", "-r", "--recursive") for word in words[1:])


def command_capabilities(command: str) -> list[str]:
    """The capabilities a Bash line touches, in checklist order. Reads the program names out of each
    segment (`security.segments`), so `ls && sudo rm -rf x` is seen as a delete."""
    if not isinstance(command, str) or not command.strip():
        return []
    found: set[str] = set()
    for segment in security.segments(command):
        names = security.programs(segment)
        if any(name in _DESTRUCTIVE for name in names):
            found.add(DELETE_OR_MOVE)
        if any(name in _RECURSIVE_PERMS for name in names) and _recursive(segment):
            found.add(DELETE_OR_MOVE)
        if any(name in _NETWORK for name in names):
            found.add(NETWORK)
        if "git" in names:
            words = segment.split()
            if any(word in _NETWORK_GIT for word in words[1:]):
                found.add(NETWORK)
        # `> existing` truncates as surely as rm does. Only a plain truncating redirect counts:
        # `>>` appends and `2>` is a stream, and neither is what the user asked to be stopped for.
        for index, character in enumerate(segment):
            if character != ">":
                continue
            if index and segment[index - 1] in ">&0123456789":
                continue
            if index + 1 < len(segment) and segment[index + 1] == ">":
                continue
            target = segment[index + 1:].strip().split()
            if target and os.path.exists(os.path.expanduser(target[0])):
                found.add(DELETE_OR_MOVE)
            break
    return [capability for capability in CAPABILITIES if capability in found]


def file_capability(tool: str, *, exists: bool, outside_workspace: bool) -> str | None:
    """The capability a file tool's call touches, or None when it is not one the checklist covers."""
    if tool in ("write_file", "edit_file"):
        return EDIT if exists else CREATE
    if tool in ("read_file", "list_directory"):
        return READ_OUTSIDE if outside_workspace else None
    return None


def needed(policy: Policy, tool: str, arguments: dict, *, exists: bool = False,
           outside_workspace: bool = False) -> list[str]:
    """Every capability this call needs approval for, given the policy. Empty means go ahead."""
    wanted: list[str] = []
    if tool == "run_command":
        wanted = command_capabilities(arguments.get("command", ""))
    elif tool == "run_in_terminal":
        wanted = [TERMINAL]
    elif tool == "type_into_program":
        wanted = [PROGRAM]
    else:
        one = file_capability(tool, exists=exists, outside_workspace=outside_workspace)
        wanted = [one] if one else []
    return [capability for capability in wanted if policy.asks(capability)]


def prompt(capability: str, subject: str) -> tuple[str, str]:
    """The card's header and question. `subject` is the file or the command it is about."""
    return LABELS[capability].capitalize(), f"Allow the agent to {LABELS[capability]}?\n{subject}"


def refusal(capability: str) -> str:
    """What the model is told when the user denies. It must not read as a bug to route around: the
    same rule as the denylist's refusal, for the same reason."""
    return (f"The user did not allow this ({LABELS[capability]}). Do not look for another way to do "
            f"it: say what you wanted to do and why, and carry on with what you can.")

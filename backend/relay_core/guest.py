# SPDX-License-Identifier: AGPL-3.0-or-later
"""Guest agent registry: Claude Code and Codex running inside a Relay pane (issue GT7X).

A "guest" is an agent CLI the user starts in a pane's shell, exactly as they would in any
other terminal. Relay does not spawn or own it (that is Tier A, deferred); the pane
classifies the foreground command line, publishes the guest id in `program_state.guest`
(protocol 26), and every surface built on top — the Claude IDE bridge, the hooks and
statusline shim, the composer translator, the sessions index — reads this one registry
rather than growing private lists of binary names and config paths.

Two kinds of truth live here:

* **Static identity** — each guest's id, display name, the binary names that classify a
  command line as that guest, and the well-known paths under its config directory
  (`~/.claude`, `~/.codex`). `classify_command` encodes the *rule* the C++ pane mirrors
  (`guestProgram` in `src/Pane.h`): the first token's basename, or — when that is a known
  launcher such as `node` or `npx` — the first non-flag token after it, matched by its own
  basename or by the npm package it lives in (`node …/@anthropic-ai/claude-code/cli.js`).
* **This machine** — `detect_installations()` resolves the binaries on PATH, their versions
  and the config dirs that actually exist. It is a probe, never a requirement: an absent
  guest is reported as absent, and an unknown guest id is a `ValueError`, not a guess.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26. Card:
issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X).
"""
from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
from dataclasses import dataclass

VERSION_TIMEOUT = 5.0     # seconds for `<binary> --version`; a hung CLI must never stall a pane

# Script extensions stripped before matching a launcher's target (`claude.js` → `claude`).
_SCRIPT_EXTENSIONS = (".js", ".mjs", ".cjs", ".ts")


@dataclass(frozen=True)
class GuestSpec:
    """What never changes about a guest, wherever it is installed."""
    id: str                      # the id in `program_state.guest`: "claude" / "codex"
    name: str                    # display name: "Claude Code" / "Codex"
    binaries: tuple[str, ...]    # argv basenames that classify a command line as this guest
    packages: tuple[str, ...]    # npm package names, scoped as published
    config_dir: str              # its directory under the user's home


GUESTS: tuple[GuestSpec, ...] = (
    GuestSpec("claude", "Claude Code", ("claude", "claude-code"),
              ("@anthropic-ai/claude-code",), ".claude"),
    GuestSpec("codex", "Codex", ("codex", "codex-cli"), ("@openai/codex",), ".codex"),
)

# Launchers whose argv[0] never names the guest even when it runs one: the guest is the
# script or package named after the launcher's own flags (`node …/bin/codex` for the
# shebang-installed CLI, `npx -y claude`, `node …/claude-code/cli.js` for the npm shim).
LAUNCHERS = ("node", "nodejs", "bun", "bunx", "deno", "npx")

_SPECS = {spec.id: spec for spec in GUESTS}


def spec(guest_id: str) -> GuestSpec:
    """The static record for one guest; `ValueError` for an id the registry does not know."""
    try:
        return _SPECS[guest_id]
    except KeyError:
        raise ValueError(f"unknown guest {guest_id!r}; known guests: {', '.join(sorted(_SPECS))}.") from None


def guest_ids() -> tuple[str, ...]:
    return tuple(_SPECS)


def classify_command(argv) -> str | None:
    """The guest a tokenized command line runs, or None. `argv` is the command line split
    into tokens (no shell quoting); the pane passes what `/proc/<pid>/cmdline` holds.

    Mirrored in C++ (`guestProgram`, `src/Pane.h`): keep the two rules in step."""
    if not argv:
        return None
    first = os.path.basename(argv[0])
    found = _guest_named(first)
    if found or first not in LAUNCHERS:
        return found
    # A launcher: the guest is its script or package — the first token that is not one of the
    # launcher's own flags. Only two things in that token may name a guest: the script's own
    # basename (`codex.js` → codex) or the npm package it lives in
    # (`…/node_modules/@anthropic-ai/claude-code/cli.js`). *Any* path component used to count,
    # which made `node /home/codex/server.js` a codex session (review of 51587e3).
    for token in argv[1:]:
        if token.startswith("-"):
            continue
        return _guest_named(_script_leaf(token)) or _guest_packaged(token)
    return None


def _script_leaf(token: str) -> str:
    """A launcher target's own name: its basename with a script extension stripped."""
    leaf = os.path.basename(token)
    for extension in _SCRIPT_EXTENSIONS:
        if leaf.endswith(extension):
            return leaf[: -len(extension)]
    return leaf


def _guest_packaged(token: str) -> str | None:
    """The guest whose npm package this path lies in, or None.

    Two shapes count, and nothing else: a scoped package directory anywhere in the path
    (`@anthropic-ai/claude-code`), and the package directory immediately under a
    `node_modules` (`…/node_modules/codex/bin/index.js`). A directory that merely shares a
    guest's name (`/home/codex`, `/var/www/claude`) is not a package and never matches.
    """
    components = [component for component in token.split(os.sep) if component]
    for index, component in enumerate(components[:-1]):
        following = components[index + 1]
        if component.startswith("@"):
            found = _guest_with_package(component + "/" + following)
            if found:
                return found
        elif component == "node_modules":
            found = _guest_named(following) or _guest_with_package(following)
            if found:
                return found
    return None


def _guest_with_package(package: str) -> str | None:
    for candidate in GUESTS:
        if package in candidate.packages:
            return candidate.id
    return None


def _guest_named(basename: str) -> str | None:
    for candidate in GUESTS:
        if basename in candidate.binaries:
            return candidate.id
    return None


# ----- this machine -----------------------------------------------------------------------


@dataclass(frozen=True)
class Installation:
    """One guest as installed here; `binary`/`version` are None when it is not."""
    guest: str           # guest id
    binary: str | None   # resolved path of the first of its binaries found on PATH
    version: str | None  # first line of `<binary> --version`
    config_dir: str      # absolute config path, whether or not it exists
    config_present: bool


def detect_installations(path: str | None = None, home: str | None = None) -> list[Installation]:
    """Every known guest, installed or not, in registry order. `path` is a PATH string
    (os.defpath when omitted) and `home` the home directory, both injectable for tests."""
    return [_detect_one(candidate, path, home or os.path.expanduser("~")) for candidate in GUESTS]


def _detect_one(candidate: GuestSpec, path: str | None, home: str) -> Installation:
    binary = None
    for name in candidate.binaries:
        binary = shutil.which(name, path=path)
        if binary:
            break
    config = config_dir(candidate.id, home)
    return Installation(candidate.id, binary, _version(binary) if binary else None,
                        config, os.path.isdir(config))


def _version(binary: str) -> str | None:
    """The first line of `<binary> --version` (stdout, else stderr); None when it cannot say.
    Probing a hung or crashing CLI is expected input, so every subprocess failure is one."""
    try:
        run = subprocess.run([binary, "--version"], capture_output=True, text=True,
                             timeout=VERSION_TIMEOUT)
    except (OSError, subprocess.SubprocessError):
        return None
    for stream in (run.stdout, run.stderr):
        for line in stream.splitlines():
            if line.strip():
                return line.strip()
    return None


# ----- well-known paths --------------------------------------------------------------------


def config_dir(guest_id: str, home: str | None = None) -> str:
    """The guest's config directory, absolute (it need not exist)."""
    root = home if home is not None else os.path.expanduser("~")
    return os.path.join(root, spec(guest_id).config_dir)


def claude_ide_lock_dir(home: str | None = None) -> str:
    """Where a running claude looks for `<port>.lock` to find its editor's MCP bridge."""
    return os.path.join(config_dir("claude", home), "ide")


def claude_projects_dir(home: str | None = None) -> str:
    """Claude's session transcripts: `<cwd-slug>/<session-id>.jsonl` under here."""
    return os.path.join(config_dir("claude", home), "projects")


def codex_sessions_dir(home: str | None = None) -> str:
    """Codex's rollout transcripts: `YYYY/MM/DD/rollout-*.jsonl` under here."""
    return os.path.join(config_dir("codex", home), "sessions")


def codex_state_db(home: str | None = None) -> str | None:
    """Codex's threads database. The suffix rolls with the schema (`state_5.sqlite` today),
    so the highest `state_*.sqlite` present is the one; None when there is no database."""
    found = glob.glob(os.path.join(config_dir("codex", home), "state_*.sqlite"))
    versioned = []
    for path in found:
        match = re.fullmatch(r"state_(\d+)\.sqlite", os.path.basename(path))
        if match:
            versioned.append((int(match.group(1)), path))
    return max(versioned)[1] if versioned else None


# ----- the IDE bridge (claude only) ----------------------------------------------------------


def bridge_env(guest_id: str, port: int) -> dict[str, str]:
    """The environment a pane shell needs for the guest to find Relay's IDE bridge.
    Codex has no IDE-bridge equivalent, so its dict is empty — inject nothing rather than
    guess at a parallel mechanism. The "SSE" name is upstream's; the transport the lock
    file selects is WebSocket."""
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError(f"port must be an integer from 1 to 65535, got {port!r}.")
    if guest_id == "codex":
        return {}
    spec(guest_id)   # an unknown guest is a ValueError, like everywhere else here
    return {"CLAUDE_CODE_SSE_PORT": str(port), "ENABLE_IDE_INTEGRATION": "true"}

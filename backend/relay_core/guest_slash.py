# SPDX-License-Identifier: GPL-3.0-or-later
"""Slash-command catalogs for guest CLIs (GT7X, protocol 26.8).

The desktop composer owns this registry rather than asking a guest TUI to render its
menu.  Claude's catalog is its built-ins plus the skill and legacy-command locations it
documents; Codex's is the stable TUI command set.  ``--emit`` publishes the result through
the normal guest-event helper, so the pane consumes the same ``slash`` event whether the
catalog is a fallback or was learned by a future guest bridge.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from typing import Iterable

_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")

CLAUDE_BUILTINS = (
    "/add-dir", "/agents", "/clear", "/compact", "/config", "/context", "/cost",
    "/doctor", "/exit", "/export", "/help", "/init", "/mcp", "/memory", "/model",
    "/permissions", "/plan", "/quit", "/resume", "/rewind", "/status", "/terminal-setup",
    "/vim",
)
CODEX_BUILTINS = (
    "/compact", "/diff", "/exit", "/help", "/init", "/logout", "/mention", "/model",
    "/new", "/permissions", "/quit", "/resume", "/review", "/status",
)


def _command(name: str) -> str | None:
    """Return a normalized slash spelling for a safe catalog name."""
    name = name.strip()
    if name.startswith("/"):
        name = name[1:]
    return "/" + name if _NAME.fullmatch(name) else None


def _paths_with_skill_names(root: Path) -> Iterable[str]:
    if not root.is_dir():
        return ()
    return (entry.name for entry in root.iterdir()
            if entry.is_dir() and (entry / "SKILL.md").is_file())


def _legacy_command_names(root: Path) -> Iterable[str]:
    if not root.is_dir():
        return ()
    return (path.stem for path in root.rglob("*.md") if path.is_file())


def claude_commands(cwd: str | os.PathLike[str] | None = None,
                    home: str | os.PathLike[str] | None = None) -> list[str]:
    """Claude built-ins and locally installed skills/legacy commands, sorted and deduped."""
    project = Path(cwd or os.getcwd())
    user_home = Path(home or Path.home())
    names: list[str] = list(CLAUDE_BUILTINS)
    for skills in (user_home / ".claude" / "skills", project / ".claude" / "skills"):
        names.extend("/" + name for name in _paths_with_skill_names(skills))
    names.extend("/" + name for name in _legacy_command_names(project / ".claude" / "commands"))
    return sorted({command for name in names if (command := _command(name))}, key=str.casefold)


def commands(guest: str, cwd: str | os.PathLike[str] | None = None,
             home: str | os.PathLike[str] | None = None) -> list[str]:
    """The static fallback catalog for one registered guest."""
    if guest == "claude":
        return claude_commands(cwd, home)
    if guest == "codex":
        return list(CODEX_BUILTINS)
    raise ValueError(f"unknown guest {guest!r}")


# The channel's one writer (26.3), beside the backend directory this package lives in.
# Three shims carry this constant (guest_hook, guest_codex, guest_slash) and they must not
# be folded into one: two of them are run by absolute path with no PYTHONPATH, so they may
# not import from `relay_core` at all. `tests/test_guest.py` (OneChannelInThreeLanguages) is what
# keeps the copies in step.
WRITER = Path(__file__).resolve().parents[2] / "shell" / "guest-event.py"


def helper_path(environment=None) -> str:
    """`shell/guest-event.py`, or "" outside a Relay pane.

    ``RELAY_GUEST_EVENT`` is the pane's spool *directory* since the single ``guest.json`` slot was
    replaced (26.3); it named this script in the first cut, so a value that still ends in ``.py``
    is taken at its word. Either way the variable is what says there is a pane at all.
    """
    environment = os.environ if environment is None else environment
    exported = environment.get("RELAY_GUEST_EVENT") or ""
    if not exported:
        return ""
    override = environment.get("RELAY_GUEST_WRITER") or ""
    if override:
        return override
    return exported if exported.endswith(".py") else str(WRITER)


def emit(guest: str, cwd: str | os.PathLike[str] | None = None) -> bool:
    """Publish the catalog onto the pane's guest event spool; absent Relay is a quiet no-op."""
    helper = helper_path()
    if not helper:
        return False
    payload = json.dumps({"commands": commands(guest, cwd)}, separators=(",", ":"))
    try:
        completed = subprocess.run([helper, "slash", guest], input=payload, text=True,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=5, check=False)
    except OSError:
        return False
    return completed.returncode == 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("guest", choices=("claude", "codex"))
    parser.add_argument("--cwd", default=os.getcwd())
    parser.add_argument("--emit", action="store_true")
    args = parser.parse_args(argv)
    if args.emit:
        emit(args.guest, args.cwd)
    else:
        print(json.dumps({"commands": commands(args.guest, args.cwd)}, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

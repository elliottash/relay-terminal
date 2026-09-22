# SPDX-License-Identifier: AGPL-3.0-or-later
"""Aliases: saved terminal commands and prompts, Warp-workflow style (issue G8DK).

One Markdown file per alias, in the Board format (`docs/BOARD-FORMAT.md`): YAML front
matter with `type: alias`, then the body.  Global aliases live in the global Board
(`$XDG_CONFIG_HOME/relay/switchboard/aliases/`), local ones in the repository Board
(`<repo>/.switchboard/aliases/`, `<repo>/switchboard/aliases/`, `<repo>/issues/aliases/`, or
`<repo>/.relay/aliases/`
in a project that has no board yet) --
the owner's decision in `docs/TASKS-AND-MEMORY-DESIGN.md` section 9.

    ---
    id: A7K2
    type: alias
    status: active
    name: squash
    kind: command
    rank: 0i
    created: '2026-09-17'
    links: {plans: [], commits: [], evidence: [], related: [], github: null}
    ---
    # Squash the last N commits together

    Squashes the last n commits together.

    ## Run

    ```sh
    git reset --soft HEAD~{{num_commits}} && git commit
    ```

    ## Parameters

    - `num_commits` = `2` — the number of commits to squash

The runnable text and the parameter defaults live in the **body**, not the front matter, because
front matter scalars are single-line (format section 2.1) and a default may hold any character --
including the commas and braces that would break a YAML flow sequence.

Security.  An alias is stored text that later gets run, and imported aliases come from files
Relay did not write.  Nothing in this module executes anything: it reads, parses and renders text.
`substitute()` is the one function that matters -- it is quote-aware, so a parameter value that
contains shell metacharacters becomes one shell word instead of new syntax.  The expanded text is
handed to the GUI, which stages it on the shell's prompt line where the user sees it and presses
Enter; Relay never runs an alias by itself.
"""
from __future__ import annotations

import os
import re
import shlex
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from . import board

KINDS = ("command", "prompt")
STATUSES = ("active", "retired")
SCOPES = ("local", "global")

MAX_NAME = 32
MAX_TEXT = 16384
MAX_VALUE = 4096
MAX_PARAMS = 32
MAX_ALIASES = 500

#: What you may type as an alias name.  Lower case so `/name` is unambiguous, no dots or slashes
#: so it can never be read as a path, and it must start with a letter or digit.
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,%d}$" % (MAX_NAME - 1))
#: Parameter names inside `{{...}}`.  Warp uses snake_case; so do we.
PARAM_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")
#: A placeholder occurrence in the runnable text.
PLACEHOLDER_RE = re.compile(r"\{\{\s*([A-Za-z_][A-Za-z0-9_]{0,63})\s*\}\}")

RUN_HEADING = "Run"
PARAMS_HEADING = "Parameters"

#: `- `name` = `default` — description`, with Markdown code-span fences of any width so a default
#: may itself contain a backtick.
_PARAM_LINE_RE = re.compile(
    r"^\s*[-*]\s+(?P<nf>`+)(?P<name>.+?)(?P=nf)"
    r"(?:\s*=\s*(?P<df>`+)(?P<default>.*?)(?P=df))?"
    r"(?:\s*[-–—:]\s*(?P<description>.*))?\s*$")

_FENCE_RE = re.compile(r"^(?P<fence>`{3,}|~{3,})[^\n]*\n(?P<body>.*?)(?:\n(?P=fence)[ \t]*$|\Z)",
                       re.S | re.M)


class AliasError(Exception):
    """A bad alias definition, an unknown name, or a missing parameter value."""


# ------------------------------------------------------------------ parameters and substitution

@dataclass
class Param:
    name: str
    default: str | None = None
    description: str = ""

    def to_dict(self) -> dict:
        return {"name": self.name, "default": self.default, "description": self.description}


@dataclass
class Alias:
    name: str
    kind: str = "command"
    title: str = ""
    description: str = ""
    text: str = ""
    params: list[Param] = field(default_factory=list)
    scope: str = "local"
    labels: list[str] = field(default_factory=list)
    shell: str | None = None
    source: str | None = None
    card_id: str | None = None
    path: str | None = None
    status: str = "active"
    #: set by `catalog()` on a global alias a local one of the same name hides
    shadowed: bool = False

    def param(self, name: str) -> Param | None:
        return next((p for p in self.params if p.name == name), None)

    def placeholders(self) -> list[str]:
        """Parameter names in the order they first appear in the runnable text."""
        out: list[str] = []
        for match in PLACEHOLDER_RE.finditer(self.text):
            if match.group(1) not in out:
                out.append(match.group(1))
        return out

    def required(self) -> list[str]:
        """Placeholders with no default: the composer must be filled in before this can run."""
        return [n for n in self.placeholders() if (self.param(n) is None or self.param(n).default is None)]

    def defaults(self) -> dict[str, str]:
        return {p.name: p.default for p in self.params if p.default is not None}

    def to_dict(self) -> dict:
        return {"name": self.name, "kind": self.kind, "title": self.title,
                "description": self.description, "text": self.text,
                "params": [p.to_dict() for p in self.params],
                "placeholders": self.placeholders(), "required": self.required(),
                "scope": self.scope, "labels": list(self.labels), "shell": self.shell,
                "source": self.source, "id": self.card_id, "path": self.path,
                "status": self.status, "shadowed": self.shadowed}


# Shell quoting states while scanning a command template.
_OUTSIDE, _SINGLE, _DOUBLE = 0, 1, 2


def _scan_quotes(text: str) -> list[int]:
    """The shell quoting state at every index of `text` (`_OUTSIDE`/`_SINGLE`/`_DOUBLE`).

    Good enough for the job it has: deciding how to escape a substituted value.  A template Relay
    cannot read this way still substitutes safely, because the fallback (`_OUTSIDE`) is the most
    conservative of the three -- it quotes the value outright.
    """
    states: list[int] = []
    state = _OUTSIDE
    escaped = False
    for ch in text:
        states.append(state)
        if escaped:
            escaped = False
            continue
        if state == _OUTSIDE:
            if ch == "\\":
                escaped = True
            elif ch == "'":
                state = _SINGLE
            elif ch == '"':
                state = _DOUBLE
        elif state == _SINGLE:
            if ch == "'":
                state = _OUTSIDE
        else:  # _DOUBLE
            if ch == "\\":
                escaped = True
            elif ch == '"':
                state = _OUTSIDE
    states.append(state)
    return states


def clean_value(value) -> str:
    """A parameter value as text: NULs dropped, length capped.  Never raises on odd input."""
    if value is None:
        return ""
    if not isinstance(value, str):
        value = str(value)
    return value.replace("\x00", "")[:MAX_VALUE]


def _escape_single(value: str) -> str:
    return value.replace("'", "'\\''")


def substitute(template: str, values: dict, *, shell: bool = True) -> str:
    """Fill `{{name}}` placeholders in `template`.

    With `shell=True` (a command) every value is escaped for the quoting context it lands in, so a
    value can only ever be *data*:

    * outside quotes  -> `shlex.quote`, e.g. `HEAD~{{n}}` with `n="2; rm -rf /"` becomes
      ``HEAD~'2; rm -rf /'`` -- one word, no second command;
    * inside `'...'`  -> `'` becomes `'\\''`, which closes, escapes and reopens the quote;
    * inside `"..."`  -> the quote is closed, the value goes in as its own quoted word, and the
      quote reopens (`""'value'""`), so `$`, backticks and `!` in the value are never expanded.
      The very common `"{{name}}"` and `'{{name}}'` spellings are recognised and replaced whole,
      which keeps the result readable.

    With `shell=False` (a prompt) the substitution is plain text: a prompt is prose handed to the
    model as untrusted data, not a command line.

    Raises `AliasError` naming every placeholder that has no value and no default.
    """
    missing = [name for name in PLACEHOLDER_RE.findall(template) if name not in values]
    if missing:
        raise AliasError("missing value for " + ", ".join(sorted(dict.fromkeys(missing))))
    if not shell:
        return PLACEHOLDER_RE.sub(lambda m: clean_value(values[m.group(1)]), template)

    states = _scan_quotes(template)
    out: list[str] = []
    cursor = 0
    for match in PLACEHOLDER_RE.finditer(template):
        start, end = match.span()
        value = clean_value(values[match.group(1)])
        state = states[start]
        if state == _DOUBLE and start > 0 and template[start - 1] == '"' and template[end:end + 1] == '"':
            # `"{{name}}"` -- swallow both quotes and emit one properly quoted word.
            out.append(template[cursor:start - 1])
            out.append(shlex.quote(value))
            cursor = end + 1
            continue
        if state == _SINGLE and start > 0 and template[start - 1] == "'" and template[end:end + 1] == "'":
            out.append(template[cursor:start - 1])
            out.append(shlex.quote(value))
            cursor = end + 1
            continue
        out.append(template[cursor:start])
        if state == _SINGLE:
            out.append(_escape_single(value))
        elif state == _DOUBLE:
            out.append('"' + shlex.quote(value) + '"')
        else:
            out.append(shlex.quote(value))
        cursor = end
    out.append(template[cursor:])
    return "".join(out)


def fill(alias: Alias, values: dict | None = None) -> str:
    """Expand an alias, taking declared defaults for anything the caller left out."""
    merged = dict(alias.defaults())
    for key, value in (values or {}).items():
        if isinstance(key, str) and PARAM_RE.match(key):
            merged[key] = clean_value(value)
    return substitute(alias.text, merged, shell=alias.kind == "command")


def positional(alias: Alias, args: str) -> dict:
    """Values from a terminal-mode line: `squash 3` fills the first placeholder with `3`.

    The words are split the way a shell would, so `deploy "my app"` fills one parameter.  A
    line Relay cannot split (an unbalanced quote) falls back to whitespace splitting rather than
    failing, because the user is still typing.
    """
    try:
        words = shlex.split(args)
    except ValueError:
        words = args.split()
    names = alias.placeholders()
    out: dict = {}
    for index, word in enumerate(words):
        if index >= len(names):
            break
        out[names[index]] = word
    return out


# ------------------------------------------------------------------------------ the file format

def _sections(body: str) -> dict[str, str]:
    """`## Heading` -> the text under it.  The lead paragraph is under the key `""`."""
    out: dict[str, str] = {}
    current = ""
    lines: list[str] = []
    for line in body.splitlines():
        if line.startswith("## "):
            out[current] = "\n".join(lines).strip()
            current = line[3:].strip()
            lines = []
        elif line.startswith("# "):
            out[current] = "\n".join(lines).strip()
            current = ""
            lines = []
        else:
            lines.append(line)
    out[current] = "\n".join(lines).strip()
    return out


def _section(sections: dict[str, str], heading: str) -> str:
    for key, value in sections.items():
        if key.lower() == heading.lower():
            return value
    return ""


def run_text(section: str) -> str:
    """The runnable text of a `## Run` section: the first fenced block, or the section itself."""
    match = _FENCE_RE.search(section.strip())
    if match:
        return match.group("body").strip("\n")
    return section.strip()


def parse_params(section: str) -> list[Param]:
    out: list[Param] = []
    for line in section.splitlines():
        if not line.strip():
            continue
        match = _PARAM_LINE_RE.match(line)
        if not match:
            continue
        name = match.group("name").strip()
        if not PARAM_RE.match(name) or any(p.name == name for p in out):
            continue
        out.append(Param(name, match.group("default"), (match.group("description") or "").strip()))
        if len(out) >= MAX_PARAMS:
            break
    return out


def _code_span(text: str) -> str:
    """Render `text` as a Markdown code span, widening the fence past any backtick run inside."""
    runs = [len(m) for m in re.findall(r"`+", text)]
    fence = "`" * (max(runs) + 1 if runs else 1)
    pad = " " if text.startswith("`") or text.endswith("`") else ""
    return f"{fence}{pad}{text}{pad}{fence}"


def render_body(alias: Alias) -> str:
    """The Markdown body of an alias card: title, description, `## Run`, `## Parameters`."""
    fence = "sh" if alias.kind == "command" else ""
    out = [f"# {alias.title or alias.name}", ""]
    if alias.description.strip():
        out += [alias.description.strip(), ""]
    out += [f"## {RUN_HEADING}", ""]
    if alias.kind == "command":
        ticks = "`" * max(3, max((len(m) for m in re.findall(r"`+", alias.text)), default=0) + 1)
        out += [f"{ticks}{fence}", alias.text.strip("\n"), ticks, ""]
    else:
        out += [alias.text.strip("\n"), ""]
    declared = [p for p in alias.params if p.name in alias.placeholders()]
    if declared:
        out += [f"## {PARAMS_HEADING}", ""]
        for param in declared:
            line = f"- {_code_span(param.name)}"
            if param.default is not None:
                line += f" = {_code_span(param.default)}"
            if param.description.strip():
                line += f" — {param.description.strip()}"
            out.append(line)
        out.append("")
    return "\n".join(out)


def from_card(card: board.Card, scope: str, path: Path | None = None) -> Alias:
    """Read an alias out of a parsed card.  Raises `AliasError` on anything unusable."""
    if str(card.front.get("type") or "") != "alias":
        raise AliasError("not an alias card")
    name = str(card.front.get("name") or "").strip().lower()
    if not NAME_RE.match(name):
        raise AliasError(f"bad alias name {name!r}")
    kind = str(card.front.get("kind") or "command").strip().lower()
    if kind not in KINDS:
        raise AliasError(f"alias {name}: kind must be command or prompt")
    sections = _sections(card.body)
    text = run_text(_section(sections, RUN_HEADING))
    if not text.strip():
        raise AliasError(f"alias {name}: the `## {RUN_HEADING}` section is empty")
    if len(text) > MAX_TEXT:
        raise AliasError(f"alias {name}: too long ({len(text)} bytes)")
    labels = card.front.get("labels")
    return Alias(
        name=name, kind=kind, title=card.title or name,
        description=sections.get("", "").strip(), text=text,
        params=parse_params(_section(sections, PARAMS_HEADING)), scope=scope,
        labels=[str(x) for x in labels] if isinstance(labels, list) else [],
        shell=str(card.front["shell"]) if card.front.get("shell") else None,
        source=str(card.front["source"]) if card.front.get("source") else None,
        card_id=card.id, path=str(path) if path else None,
        status=str(card.front.get("status") or "active"))


def to_card(alias: Alias, *, card_id: str | None = None, rank: str | None = None,
            created: str | None = None) -> board.Card:
    card = board.new_card("alias", alias.title or alias.name, alias.status or "active",
                          card_id=card_id, rank=rank, created=created,
                          name=alias.name, kind=alias.kind,
                          shell=alias.shell, source=alias.source,
                          labels=list(alias.labels) or None)
    card.body = render_body(alias)
    card.dirty = True
    return card


def validate(alias: Alias) -> Alias:
    """Check a definition before it is written.  Raises `AliasError` with a sentence to show."""
    alias.name = (alias.name or "").strip().lower()
    if not NAME_RE.match(alias.name):
        raise AliasError("An alias name is 1-32 characters of a-z, 0-9, - or _, starting with a letter or digit.")
    if alias.kind not in KINDS:
        raise AliasError('An alias kind is "command" or "prompt".')
    if alias.status not in STATUSES:
        raise AliasError('An alias status is "active" or "retired".')
    alias.text = (alias.text or "").replace("\x00", "")
    if not alias.text.strip():
        raise AliasError("An alias needs the text it runs.")
    if len(alias.text) > MAX_TEXT:
        raise AliasError(f"An alias may hold at most {MAX_TEXT} characters.")
    seen: set[str] = set()
    params: list[Param] = []
    for param in alias.params[:MAX_PARAMS]:
        if not PARAM_RE.match(param.name) or param.name in seen:
            continue
        seen.add(param.name)
        params.append(Param(param.name,
                            None if param.default is None else clean_value(param.default),
                            " ".join((param.description or "").split())[:200]))
    alias.params = params
    return alias


# ------------------------------------------------------------------------------------- the store

def global_root() -> Path:
    """The global Board: `$XDG_CONFIG_HOME/relay/switchboard` (owner decision, section 9)."""
    override = os.environ.get("RELAY_GLOBAL_SWITCHBOARD", "").strip()
    if override:
        return Path(override).expanduser()
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "relay" / "switchboard"


def local_root(workspace: str | os.PathLike | None) -> Path | None:
    """The project's board folder when it has one, else `<repo>/.relay`.

    The folder is the first of `board.BOARD_FOLDERS` this project has — `board/` on a board created
    from 2026-09-21 on, then `.switchboard/`, `switchboard/` and `issues/`.  `.relay/` is the same fallback
    plan mode's own plan files use in a project with no board (`<root>/.relay/plans`), so a
    project gets local aliases before it gets a board, and adopting a board later is a move, not
    a conversion.
    """
    if not workspace:
        return None
    root = Path(workspace).expanduser()
    for name in board.BOARD_FOLDERS:
        here = root / name
        if (here / board.BOARD_CONFIG).exists() or (here / board.ALIAS_FOLDER).is_dir():
            return here
    return root / ".relay"


def alias_dir(root: Path, status: str = "active") -> Path:
    sub = board.ALIAS_STATUS_FOLDER.get(status, "")
    return (root / board.ALIAS_FOLDER / sub) if sub else (root / board.ALIAS_FOLDER)


def _load_dir(directory: Path, scope: str) -> tuple[list[Alias], list[dict]]:
    out: list[Alias] = []
    problems: list[dict] = []
    if not directory.is_dir():
        return out, problems
    for path in sorted(directory.glob("*.md")):
        if len(out) >= MAX_ALIASES:
            problems.append({"path": str(directory), "message": f"stopped after {MAX_ALIASES} aliases"})
            break
        try:
            alias = from_card(board.Card.load(path), scope, path)
        except (AliasError, board.BoardError, OSError, UnicodeDecodeError) as exc:
            problems.append({"path": str(path), "message": str(exc)})
            continue
        out.append(alias)
    return out, problems


def load(workspace: str | os.PathLike | None = None,
         *, scopes: tuple[str, ...] = SCOPES) -> tuple[list[Alias], list[dict]]:
    """Every active alias, local first.  Never raises: unreadable files become problems."""
    aliases: list[Alias] = []
    problems: list[dict] = []
    if "local" in scopes:
        root = local_root(workspace)
        if root is not None:
            found, bad = _load_dir(alias_dir(root), "local")
            aliases += found
            problems += bad
    if "global" in scopes:
        found, bad = _load_dir(alias_dir(global_root()), "global")
        aliases += found
        problems += bad
    return aliases, problems


def catalog(workspace: str | os.PathLike | None = None) -> tuple[list[Alias], list[dict]]:
    """The resolved list: a local alias hides a global one of the same name, which is kept in the
    list and marked `shadowed` so the UI can say so rather than silently dropping it."""
    aliases, problems = load(workspace)
    local_names = {a.name for a in aliases if a.scope == "local"}
    for alias in aliases:
        alias.shadowed = alias.scope == "global" and alias.name in local_names
    aliases.sort(key=lambda a: (a.name, a.scope != "local"))
    return aliases, problems


def resolve(name: str, workspace: str | os.PathLike | None = None,
            scope: str | None = None) -> Alias:
    """The alias `name` would run: local wins over global unless a scope is named."""
    name = (name or "").strip().lower()
    aliases, _ = load(workspace)
    for want in ([scope] if scope in SCOPES else ["local", "global"]):
        for alias in aliases:
            if alias.name == name and alias.scope == want and not alias.shadowed:
                return alias
    raise AliasError(f"No alias named {name!r}.")


def card_path(root: Path, alias: Alias) -> Path:
    return alias_dir(root, alias.status) / f"{alias.name}.md"


def save(alias: Alias, workspace: str | os.PathLike | None = None,
         scope: str = "local") -> Alias:
    """Write one alias card, replacing any earlier file of the same name in the same scope.

    Writing is atomic (a temp file plus `os.replace`), as every Board write is.
    """
    if scope not in SCOPES:
        raise AliasError('An alias scope is "local" or "global".')
    validate(alias)
    alias.scope = scope
    root = global_root() if scope == "global" else local_root(workspace)
    if root is None:
        raise AliasError("A local alias needs an open project.")
    path = card_path(root, alias)
    # HQ and imported cards may have ID-based filenames. Find the actual stored definition
    # before updating so a second name.md cannot leave a stale command active (#P7SJ).
    stored = []
    for status in STATUSES:
        found, _ = _load_dir(alias_dir(root, status), scope)
        stored.extend(found)
    previous = next((a for a in stored if alias.card_id and a.card_id == alias.card_id), None)
    if previous is None:
        previous = next((a for a in stored if a.name == alias.name and a.status == alias.status), None)
    if previous is not None and previous.path:
        old_path = Path(previous.path)
        if not old_path.resolve().is_relative_to(root.resolve()):
            raise AliasError("Alias source is outside its Board.")
        if previous.name == alias.name and previous.status == alias.status:
            path = old_path
        elif path.exists() and path != old_path:
            raise AliasError("An alias already occupies the destination path.")
    card_id, rank, created = alias.card_id, None, None
    identity_path = Path(previous.path) if previous is not None and previous.path else path
    if identity_path.exists():  # keep the identity, rank and creation date on updates and renames
        try:
            old = board.Card.load(identity_path)
            card_id = card_id or old.id
            rank = old.rank or None
            created = str(old.front.get("created")) if old.front.get("created") else None
        except (board.BoardError, OSError, UnicodeDecodeError):
            pass
    card = to_card(alias, card_id=card_id, rank=rank, created=created)
    card.path = path
    board.atomic_write(path, card.to_text())
    if previous is not None and previous.path and Path(previous.path) != path:
        Path(previous.path).unlink()
    alias.card_id = card.id
    alias.path = str(path)
    return alias


def delete(name: str, workspace: str | os.PathLike | None = None, scope: str = "local") -> str:
    """Remove an alias card.  Returns the path it removed."""
    alias = resolve(name, workspace, scope)
    if not alias.path:
        raise AliasError(f"No alias named {name!r} in the {scope} Board.")
    Path(alias.path).unlink()
    return alias.path


# --------------------------------------------------------------------------- repeated commands

#: Commands never worth an alias: too short, or already one word.
_TRIVIAL = {"ls", "cd", "pwd", "clear", "exit", "vim", "git", "make", "top", "htop"}


def normalize_command(command: str) -> str:
    return " ".join((command or "").split())


def repeats(history, min_count: int = 3, limit: int = 5) -> list[dict]:
    """Commands typed `min_count` times or more, most repeated first.

    A pure rule: no model, no network.  The worker feeds the winner to a side call that proposes
    a name and the parameters; the user decides whether anything is written.
    """
    counts: dict[str, int] = {}
    for entry in history or []:
        command = normalize_command(entry if isinstance(entry, str) else (entry or {}).get("command", ""))
        if not command or len(command) < 6 or command.split()[0] in _TRIVIAL and " " not in command:
            continue
        if command.split()[0] in _TRIVIAL and len(command.split()) < 2:
            continue
        counts[command] = counts.get(command, 0) + 1
    ranked = [{"command": c, "count": n} for c, n in counts.items() if n >= min_count]
    ranked.sort(key=lambda r: (-r["count"], r["command"]))
    return ranked[:limit]


def slug(title: str, taken=()) -> str:
    """An alias name from a human title: `Kill the process on a port` -> `kill-the-process-on-a`."""
    text = re.sub(r"[^a-z0-9]+", "-", (title or "").lower()).strip("-")
    if len(text) > MAX_NAME:
        cut = text[:MAX_NAME]
        dash = cut.rfind("-")
        text = (cut[:dash] if dash > MAX_NAME // 2 else cut).strip("-")
    text = text or "alias"
    if not NAME_RE.match(text):
        text = "alias"
    candidate, n = text, 1
    taken = set(taken)
    while candidate in taken:
        n += 1
        suffix = f"-{n}"
        candidate = text[:MAX_NAME - len(suffix)].rstrip("-") + suffix
    return candidate


def today() -> str:
    return datetime.now().strftime("%Y-%m-%d")

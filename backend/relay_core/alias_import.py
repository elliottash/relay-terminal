# SPDX-License-Identifier: AGPL-3.0-or-later
"""Importing Warp workflows and shell aliases as Relay aliases (issue G8DK).

Everything here is **reading**.  A Warp workflow and a line of somebody's `.bashrc` are untrusted
input from disk: this module never sources a startup file, never runs a workflow, never opens a
subprocess and never writes anything.  It produces a *preview* -- for each candidate, the exact
text that would be stored, the parameters it found, whether it would replace an alias that already
exists, and any warning worth reading before saying yes.  `apply()` writes only the names the user
picked out of a preview it already saw.

Sources:

* **Warp workflows in the desktop database.**  Warp keeps saved workflows as JSON rows in
  `$XDG_STATE_HOME/warp-terminal/warp.sqlite` (table `workflows`, one JSON blob per row).  The
  file is copied to a temporary directory and opened read-only, so an import can neither block
  nor alter a running Warp.
* **Warp workflow YAML files**, the shareable form, under `~/.warp/workflows/` and
  `<repo>/.warp/workflows/`.  Read with a small parser for exactly the shape Warp writes; a file
  outside that shape is skipped with a reason rather than guessed at.
* **Shell aliases** from the user's startup files (`.bashrc`, `.bash_aliases`, `.zshrc`,
  `.profile`, fish's `config.fish`).  Only lines that literally begin with `alias ` are read, and
  they are unquoted textually.

A Warp `agent_mode` workflow carries a `query` instead of a `command`: that is a saved *prompt*,
and it imports as `kind: prompt`.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import sqlite3
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from . import aliases

WARP_SQLITE = "warp-terminal/warp.sqlite"
WARP_YAML_DIRS = ("~/.warp/workflows", "~/.config/warp-terminal/workflows")
SHELL_FILES = (".bashrc", ".bash_aliases", ".bash_profile", ".zshrc", ".zshenv", ".profile",
               ".config/fish/config.fish")
SOURCES = ("warp", "shell")

MAX_FILE_BYTES = 4 << 20        # a startup file or workflow this big is not one we should read
MAX_ITEMS = 300
MAX_COMMAND = aliases.MAX_TEXT

#: `alias name=value` / fish's `alias name value`.  Anything else on the line is not an alias.
_ALIAS_RE = re.compile(r"^\s*alias\s+(?P<name>[A-Za-z0-9_.\-]+)\s*=\s*(?P<value>.*)$")
_FISH_ALIAS_RE = re.compile(r"^\s*alias\s+(?P<name>[A-Za-z0-9_.\-]+)\s+(?P<value>\S.*)$")
#: Aliases that only re-spell a builtin are noise, not workflows.
_NOISE = {"ls", "ll", "la", "l", "grep", "fgrep", "egrep", "rm", "cp", "mv", "dir", "vdir"}


@dataclass
class Candidate:
    """One thing that could become an alias, with everything needed to decide."""
    source: str                  # "warp-sqlite" | "warp-yaml" | "shell"
    origin: str                  # the file (and row) it came from, for the preview
    name: str
    kind: str = "command"
    title: str = ""
    description: str = ""
    text: str = ""
    params: list[aliases.Param] = field(default_factory=list)
    labels: list[str] = field(default_factory=list)
    shell: str | None = None
    conflict: str | None = None  # "local" or "global": an alias of this name is already there
    warnings: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {"source": self.source, "origin": self.origin, "name": self.name, "kind": self.kind,
                "title": self.title, "description": self.description, "text": self.text,
                "params": [p.to_dict() for p in self.params], "labels": list(self.labels),
                "shell": self.shell, "conflict": self.conflict, "warnings": list(self.warnings)}

    def to_alias(self, scope: str) -> aliases.Alias:
        return aliases.Alias(name=self.name, kind=self.kind, title=self.title or self.name,
                             description=self.description, text=self.text, params=list(self.params),
                             scope=scope, labels=list(self.labels), shell=self.shell,
                             source=self.origin)


def _skip(origin: str, reason: str) -> dict:
    return {"origin": origin, "reason": reason}


def _read_text(path: Path) -> str:
    """Read a file we did not write: size-capped, never following a symlink to somewhere else."""
    if path.is_symlink():
        raise OSError("is a symbolic link")
    size = path.stat().st_size
    if size > MAX_FILE_BYTES:
        raise OSError(f"larger than {MAX_FILE_BYTES} bytes")
    return path.read_text(encoding="utf-8", errors="replace")


# --------------------------------------------------------------------------- Warp: the database

def warp_sqlite_path() -> Path:
    base = os.environ.get("XDG_STATE_HOME") or str(Path.home() / ".local" / "state")
    return Path(base) / WARP_SQLITE


def read_warp_sqlite(path: Path | None = None) -> tuple[list[dict], list[dict]]:
    """The `workflows` rows as `(id, data)` JSON objects.  Reads a copy, never the live file."""
    path = Path(path) if path is not None else warp_sqlite_path()
    if not path.exists():
        return [], []
    rows: list[dict] = []
    skipped: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="relay-warp-") as temp:
        copy = Path(temp) / "warp.sqlite"
        try:
            shutil.copyfile(path, copy)
            for suffix in ("-wal", "-shm"):  # so the copy sees committed-but-uncheckpointed rows
                side = path.with_name(path.name + suffix)
                if side.exists():
                    shutil.copyfile(side, copy.with_name(copy.name + suffix))
        except OSError as exc:
            return [], [_skip(str(path), f"could not read Warp's database ({exc})")]
        try:
            connection = sqlite3.connect(f"file:{copy}?mode=ro", uri=True)
        except sqlite3.Error as exc:
            return [], [_skip(str(path), f"not a readable database ({exc})")]
        try:
            found = connection.execute("select id, data from workflows").fetchall()
        except sqlite3.Error as exc:
            connection.close()
            return [], [_skip(str(path), f"no readable workflows table ({exc})")]
        for row_id, data in found:
            origin = f"{path} (workflows row {row_id})"
            try:
                value = json.loads(data)
            except (TypeError, ValueError) as exc:
                skipped.append(_skip(origin, f"not valid JSON ({exc})"))
                continue
            if not isinstance(value, dict):
                skipped.append(_skip(origin, "not a workflow object"))
                continue
            rows.append({"origin": origin, "workflow": value})
        connection.close()
    return rows, skipped


# ------------------------------------------------------------------------- Warp: the YAML files

def _unquote(text: str) -> str:
    """YAML's rules: a wholly quoted scalar, `''` being a literal quote inside single quotes."""
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        inner = text[1:-1]
        return inner.replace("''", "'") if text[0] == "'" else inner.replace('\\"', '"').replace("\\\\", "\\")
    return text


def unquote_word(text: str) -> str:
    """One shell word with its quoting removed, the way the shell removes it.

    Needed because `alias alert='… '\\''quoted'\\'' …'` is three concatenated pieces, not one
    single-quoted string with a doubled quote in it -- YAML's `''` rule gets that wrong.  Purely
    textual: `$`, backticks and `~` are left exactly as written, because the stored alias text is
    what the user will see on the prompt line, not something this module expands.

    Raises `ValueError` when the quoting never closes.
    """
    out: list[str] = []
    index, length = 0, len(text)
    while index < length:
        ch = text[index]
        if ch == "\\" and index + 1 < length:
            out.append(text[index + 1])
            index += 2
        elif ch == "'":
            end = text.find("'", index + 1)
            if end == -1:
                raise ValueError("unbalanced single quote")
            out.append(text[index + 1:end])
            index = end + 1
        elif ch == '"':
            index += 1
            while index < length and text[index] != '"':
                if text[index] == "\\" and index + 1 < length and text[index + 1] in '\\"$`\n':
                    out.append(text[index + 1])
                    index += 2
                    continue
                out.append(text[index])
                index += 1
            if index >= length:
                raise ValueError("unbalanced double quote")
            index += 1
        else:
            out.append(ch)
            index += 1
    return "".join(out)


def _flow_list(text: str) -> list[str]:
    inner = text.strip()[1:-1]
    return [_unquote(part) for part in inner.split(",") if part.strip()]


def parse_workflow_yaml(text: str) -> dict:
    """The subset a Warp workflow file uses: scalars, `|`/`>` blocks, flow lists, and a block list
    of one-level mappings (`arguments:`).  Anything outside that raises `ValueError`, so a file we
    do not really understand is skipped with a reason instead of half-read."""
    out: dict = {}
    lines = text.replace("\t", "    ").splitlines()
    index = 0
    if lines and lines[0].strip() == "---":
        index = 1
    while index < len(lines):
        raw = lines[index]
        if not raw.strip() or raw.lstrip().startswith("#") or raw.strip() == "---":
            index += 1
            continue
        if raw[:1] in (" ", "-"):
            raise ValueError(f"unexpected indentation at line {index + 1}")
        if ":" not in raw:
            raise ValueError(f"not a key at line {index + 1}")
        key, _, value = raw.partition(":")
        key, value = key.strip(), value.strip()
        index += 1
        if value in ("|", ">", "|-", ">-", "|+", ">+"):
            block: list[str] = []
            indent = None
            while index < len(lines) and (not lines[index].strip() or lines[index][:1] in (" ", "\t")):
                line = lines[index]
                if line.strip():
                    here = len(line) - len(line.lstrip())
                    indent = here if indent is None else min(indent, here)
                block.append(line)
                index += 1
            body = "\n".join(line[indent:] if len(line) >= (indent or 0) else line.strip() for line in block)
            out[key] = body.strip("\n") if value.startswith("|") else " ".join(body.split())
            continue
        if value.startswith("[") and value.endswith("]"):
            out[key] = _flow_list(value)
            continue
        if value:
            out[key] = _unquote(value)
            continue
        # a block: either `- item` entries or nested `key: value` lines
        items: list = []
        current: dict | None = None
        while index < len(lines) and (not lines[index].strip() or lines[index][:1] in (" ", "\t")):
            line = lines[index].rstrip()
            index += 1
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            stripped = line.strip()
            if stripped.startswith("- "):
                rest = stripped[2:].strip()
                if ":" in rest and not rest.startswith(("'", '"')):
                    k, _, v = rest.partition(":")
                    current = {k.strip(): _unquote(v)}
                    items.append(current)
                else:
                    current = None
                    items.append(_unquote(rest))
                continue
            if current is None:
                raise ValueError(f"unexpected line {index}: {line.strip()!r}")
            k, _, v = stripped.partition(":")
            if not _:
                raise ValueError(f"unexpected line {index}: {line.strip()!r}")
            current[k.strip()] = _unquote(v)
        out[key] = items
    return out


def warp_yaml_dirs(workspace: str | os.PathLike | None = None) -> list[Path]:
    dirs = [Path(d).expanduser() for d in WARP_YAML_DIRS]
    if workspace:
        dirs.append(Path(workspace).expanduser() / ".warp" / "workflows")
    return dirs


def read_warp_yaml(directories) -> tuple[list[dict], list[dict]]:
    rows: list[dict] = []
    skipped: list[dict] = []
    for directory in directories:
        directory = Path(directory)
        if not directory.is_dir():
            continue
        for path in sorted(list(directory.glob("*.yaml")) + list(directory.glob("*.yml"))):
            try:
                value = parse_workflow_yaml(_read_text(path))
            except (ValueError, OSError, UnicodeDecodeError) as exc:
                skipped.append(_skip(str(path), f"could not be read as a Warp workflow ({exc})"))
                continue
            rows.append({"origin": str(path), "workflow": value})
    return rows, skipped


# --------------------------------------------------------------------- Warp workflow -> candidate

def _warp_params(workflow: dict, text: str) -> list[aliases.Param]:
    declared = workflow.get("arguments")
    out: list[aliases.Param] = []
    if isinstance(declared, list):
        for entry in declared:
            if not isinstance(entry, dict):
                continue
            name = str(entry.get("name") or "").strip()
            if not aliases.PARAM_RE.match(name) or any(p.name == name for p in out):
                continue
            default = entry.get("default_value")
            if default is None:
                default = entry.get("default")
            out.append(aliases.Param(name, None if default is None else aliases.clean_value(default),
                                     " ".join(str(entry.get("description") or "").split())[:200]))
    for name in aliases.PLACEHOLDER_RE.findall(text):   # placeholders Warp did not declare
        if not any(p.name == name for p in out):
            out.append(aliases.Param(name))
    return out[:aliases.MAX_PARAMS]


def warp_candidate(origin: str, workflow: dict, taken) -> Candidate | dict:
    """One Warp workflow as a candidate, or a skip record explaining why it is not usable."""
    title = str(workflow.get("name") or "").strip()
    if not title:
        return _skip(origin, "the workflow has no name")
    kind = "prompt" if str(workflow.get("type") or "") == "agent_mode" else "command"
    text = workflow.get("command") if kind == "command" else workflow.get("query")
    if not isinstance(text, str) or not text.strip():
        return _skip(origin, f"{title!r} has no {'command' if kind == 'command' else 'prompt'} text")
    if len(text) > MAX_COMMAND:
        return _skip(origin, f"{title!r} is longer than {MAX_COMMAND} characters")
    tags = workflow.get("tags")
    shells = workflow.get("shells")
    return Candidate(
        source="warp-sqlite" if "workflows row" in origin else "warp-yaml",
        origin=origin, name=aliases.slug(title, taken), kind=kind, title=title,
        description=" ".join(str(workflow.get("description") or "").split()),
        text=text.strip("\n"), params=_warp_params(workflow, text),
        labels=[str(t) for t in tags][:8] if isinstance(tags, list) else [],
        shell=str(shells[0]) if isinstance(shells, list) and shells else None)


# ------------------------------------------------------------------------------- shell aliases

def shell_files(home: Path | None = None) -> list[Path]:
    home = home or Path.home()
    return [home / name for name in SHELL_FILES]


def parse_shell_aliases(text: str, *, fish: bool = False) -> tuple[list[tuple[str, str]], list[str]]:
    """`(name, value)` pairs from startup-file text, plus a reason for each line we would not read.

    Textual only.  A line is read when it starts with `alias ` and has a name and a value; a
    continued or unbalanced line is reported rather than guessed at.
    """
    found: list[tuple[str, str]] = []
    problems: list[str] = []
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped.startswith("alias ") and stripped != "alias":
            continue
        if stripped.endswith("\\"):
            problems.append(f"line {number}: continued over more than one line")
            continue
        match = _ALIAS_RE.match(line) or (_FISH_ALIAS_RE.match(line) if fish else None)
        if not match:
            problems.append(f"line {number}: not `alias name=value`")
            continue
        name, value = match.group("name"), match.group("value").strip()
        if not value:
            problems.append(f"line {number}: {name} has no value")
            continue
        try:
            unquoted = unquote_word(value)
        except ValueError as exc:
            problems.append(f"line {number}: {name} has an {exc}")
            continue
        if not unquoted.strip():
            problems.append(f"line {number}: {name} has no value")
            continue
        found.append((name, unquoted))
    return found, problems


def read_shell_aliases(paths) -> tuple[list[dict], list[dict]]:
    rows: list[dict] = []
    skipped: list[dict] = []
    for path in paths:
        path = Path(path)
        if not path.is_file():
            continue
        try:
            text = _read_text(path)
        except (OSError, UnicodeDecodeError) as exc:
            skipped.append(_skip(str(path), f"could not be read ({exc})"))
            continue
        found, problems = parse_shell_aliases(text, fish=path.name == "config.fish")
        for name, value in found:
            rows.append({"origin": str(path), "name": name, "value": value})
        for problem in problems:
            skipped.append(_skip(str(path), problem))
    return rows, skipped


def shell_candidate(origin: str, name: str, value: str, taken) -> Candidate | dict:
    if not value.strip():
        return _skip(origin, f"{name} has no value")
    if len(value) > MAX_COMMAND:
        return _skip(origin, f"{name} is longer than {MAX_COMMAND} characters")
    if name in _NOISE:
        return _skip(origin, f"{name} only re-spells a standard command")
    return Candidate(source="shell", origin=origin, name=aliases.slug(name, taken),
                     kind="command", title=name, description=f"Shell alias from {Path(origin).name}",
                     text=value.strip(), params=[], shell="bash")


# ------------------------------------------------------------------------------ preview / apply

def _warnings(candidate: Candidate) -> list[str]:
    out: list[str] = []
    if shutil.which(candidate.name):
        out.append(f"`{candidate.name}` is also a program on your PATH; typing it in terminal mode "
                   f"will run the alias instead.")
    # Worth a second look before saving, whether it will be run by the shell or asked of the
    # agent: a saved prompt is a standing instruction with tools behind it.
    lowered = candidate.text.lower()
    for needle, why in (("sudo", "runs as root"), ("rm -rf", "deletes recursively"),
                        ("curl", "downloads from the network"), ("wget", "downloads from the network"),
                        ("| sh", "pipes downloaded text into a shell"),
                        ("| bash", "pipes downloaded text into a shell")):
        if needle in lowered:
            out.append(f"contains `{needle}` — {why}")
    if len(candidate.name) >= aliases.MAX_NAME:
        out.append("the name was shortened to fit")
    return out


def preview(sources=SOURCES, *, workspace=None, home: Path | None = None,
            sqlite_path: Path | None = None, yaml_dirs=None, shell_paths=None) -> dict:
    """Everything that could be imported, with the exact text that would be stored.

    Nothing is written and nothing is run.  Every path argument is injectable so tests (and the
    QA lane) never touch the real home directory.
    """
    wanted = tuple(s for s in (sources or SOURCES) if s in SOURCES) or SOURCES
    items: list[Candidate] = []
    skipped: list[dict] = []
    taken: set[str] = set()

    if "warp" in wanted:
        rows, bad = read_warp_sqlite(sqlite_path)
        skipped += bad
        more, bad = read_warp_yaml(yaml_dirs if yaml_dirs is not None else warp_yaml_dirs(workspace))
        rows += more
        skipped += bad
        for row in rows:
            result = warp_candidate(row["origin"], row["workflow"], taken)
            if isinstance(result, Candidate):
                taken.add(result.name)
                items.append(result)
            else:
                skipped.append(result)

    if "shell" in wanted:
        rows, bad = read_shell_aliases(shell_paths if shell_paths is not None else shell_files(home))
        skipped += bad
        for row in rows:
            result = shell_candidate(row["origin"], row["name"], row["value"], taken)
            if isinstance(result, Candidate):
                taken.add(result.name)
                items.append(result)
            else:
                skipped.append(result)

    existing, problems = aliases.load(workspace)
    by_name = {a.name: a.scope for a in existing}
    for candidate in items:
        candidate.conflict = by_name.get(candidate.name)
        candidate.warnings = _warnings(candidate)
        if candidate.conflict:
            candidate.warnings.insert(0, f"replaces the {candidate.conflict} alias `{candidate.name}`")

    return {"event": "alias_import_preview", "sources": list(wanted),
            "items": [c.to_dict() for c in items[:MAX_ITEMS]],
            "skipped": skipped[:MAX_ITEMS], "problems": problems}


def apply(items, names, scope: str = "global", workspace=None, renames=None) -> dict:
    """Write the named candidates from a preview.

    `items` are the preview's *own* dictionaries, held by the worker -- the caller names which of
    them to write, and may pass `renames` to store one under a different name, but it cannot
    supply text of its own.  So an import can only ever write bytes this module read and showed.
    """
    if scope not in aliases.SCOPES:
        raise aliases.AliasError('An import scope is "local" or "global".')
    wanted = [str(n) for n in (names or [])]
    by_name = {str(item.get("name")): item for item in (items or []) if isinstance(item, dict)}
    unknown = [n for n in wanted if n not in by_name]
    if unknown:
        raise aliases.AliasError("Not in the preview: " + ", ".join(sorted(unknown)))
    renames = {str(k): str(v) for k, v in (renames or {}).items()}
    written: list[dict] = []
    failed: list[dict] = []
    for name in wanted:
        item = by_name[name]
        candidate = Candidate(
            source=str(item.get("source") or "shell"), origin=str(item.get("origin") or ""),
            name=renames.get(name, name), kind=str(item.get("kind") or "command"),
            title=str(item.get("title") or name),
            description=str(item.get("description") or ""), text=str(item.get("text") or ""),
            params=[aliases.Param(str(p.get("name")), p.get("default"), str(p.get("description") or ""))
                    for p in (item.get("params") or []) if isinstance(p, dict)],
            labels=[str(x) for x in (item.get("labels") or [])],
            shell=str(item["shell"]) if item.get("shell") else None)
        try:
            saved = aliases.save(candidate.to_alias(scope), workspace, scope)
        except (aliases.AliasError, OSError) as exc:
            failed.append({"name": candidate.name, "reason": str(exc)})
            continue
        written.append({"name": saved.name, "kind": saved.kind, "scope": scope,
                        "path": saved.path, "id": saved.card_id})
    return {"event": "alias_imported", "scope": scope, "written": written, "failed": failed}

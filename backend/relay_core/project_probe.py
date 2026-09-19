# SPDX-License-Identifier: GPL-3.0-or-later
"""What Relay can see in a project *before* it is allowed to do anything.

This module answers one question, offline: **if we initialized a Switchboard here, what
would we already have?**  Its result is what the one-time question

    Initialize a project and create a Switchboard here?

shows the user, with an import/sync checkbox per finding, unchecked.  Nothing here writes a
file, calls a model, opens a socket or runs a subprocess — the project has a no-telemetry
rule, and no network call may happen before the user has said yes.  `board_import` turns the
findings into proposed cards; only `board_import.apply` writes anything.

Bounded on purpose: every walk has a depth and an entry ceiling, every file a byte ceiling,
and a symlink that leaves the project is never followed (`docs/PROJECT-INIT-AND-IMPORT.md`).

## Where each format was read from

Every parser below is written against the project's own documentation.  The URL beside it is
the page the shape was taken from; if a shape could not be confirmed from a primary source it
says so in the parser's own docstring and the parser is deliberately forgiving.

* git config file format — https://git-scm.com/docs/git-config#_configuration_file
  (`[remote "x"] url =`), and `url.<base>.insteadOf`:
  https://git-scm.com/docs/git-config#Documentation/git-config.txt-urlltbasegtinsteadOf
* the `.git` *file* (`gitdir: …`) of a worktree or submodule, and the `commondir` file —
  https://git-scm.com/docs/gitrepository-layout
* GitHub CLI hosts file (`hosts.yml`, one top-level key per host) —
  https://cli.github.com/manual/gh_help_environment (`GH_CONFIG_DIR`, `XDG_CONFIG_HOME`)
* Backlog.md (MrLesk) — see `_read_backlog`
* Beads (steveyegge) — see `_read_beads`
* Task Master (eyaltoledano/claude-task-master) — see `_read_taskmaster`
* spec-kit (github/spec-kit) — see `_read_speckit`
* Kiro specs (kiro.dev) — see `_read_kiro`
* OpenSpec (Fission-AI/OpenSpec) — see `_read_openspec`
"""
from __future__ import annotations

import hashlib
import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

from . import board as B

#: Schema version of `probe()`'s result, so a GUI can refuse a shape it does not know.
PROBE_VERSION = 1

# --------------------------------------------------------------------------- ceilings
#: A probe runs while a dialog waits, so every step is bounded rather than merely fast.
MAX_FILE_BYTES = 512 * 1024        # any single text file we parse
MAX_JSON_BYTES = 4 * 1024 * 1024   # tasks.json / JSONL can legitimately be larger
MAX_ITEMS_PER_SOURCE = 500         # findings past this are counted, not carried
MAX_SOURCES_PER_KIND = 40          # e.g. specs/*/tasks.md in a huge repository
MAX_DIR_ENTRIES = 2000             # entries read from any one directory
MAX_DEPTH = 3                      # how far below a tracker's own root we descend

#: Checklist files we read at the project root and in `docs/` (matched case-insensitively).
CHECKLIST_NAMES = ("TODO.md", "NOTES.md", "IDEAS.md", "ROADMAP.md", "PLAN.md")
CHECKLIST_DIRS = ("", "docs")

TRACKER_KINDS = ("checklist", "backlog-md", "beads", "taskmaster", "spec-kit", "kiro", "openspec")


class ProbeError(Exception):
    """A probe could not read something it was pointed at.  Never raised out of `probe()`."""


# --------------------------------------------------------------------------- findings

@dataclass
class TrackerItem:
    """One importable thing found in a tracker, normalised.

    `source_key` is the identity that makes a re-import a no-op: it survives the file being
    edited around the item, and it is what `board_import` records once a card exists.
    """
    kind: str
    path: str                       # relative to the project, POSIX separators
    source_id: str                  # id inside that file (task id, bead id, text hash)
    title: str
    body: str = ""
    status: str = "open"            # open|in-progress|blocked|review|deferred|done|dropped
    priority: str | None = None     # high|medium|low, or None
    labels: list[str] = field(default_factory=list)
    tasks: list[tuple[str, str]] = field(default_factory=list)   # (text, item status)
    #: Dependencies *between* the entries of `tasks`, keyed by their 1-based position in it.
    #: A value is either another 1-based position in `tasks`, or the `source_id` of another
    #: item in the same `group` — which `board_import` turns into that item's card.  Only
    #: Task Master has these; every other tracker's items are a flat checklist.
    task_depends: dict[int, list] = field(default_factory=dict)
    depends_on: list[str] = field(default_factory=list)          # source_ids in the same group
    parent: str | None = None       # a source_id in the same group
    #: The set a `depends_on`/`parent` id is resolved inside — a tracker's own root, because
    #: Backlog.md and Beads number across many files while a `tasks.md` numbers within one.
    group: str = ""
    order: int = 0                  # position in the source, for a stable rank
    line: int | None = None         # 1-based line, when the item is a line
    #: Free-form extra front matter the board accepts for a work card (`milestone`, `assignee`),
    #: applied after the card exists.  Nothing here may be a key `board.check` does not know.
    fields: dict = field(default_factory=dict)
    #: Discussion carried across as thread entries: `(author, text)`.
    comments: list[tuple[str, str]] = field(default_factory=list)

    @property
    def source_key(self) -> str:
        return f"{self.kind}:{self.path}#{self.source_id}"

    def to_dict(self) -> dict:
        out = {
            "kind": self.kind, "path": self.path, "source_id": self.source_id,
            "source_key": self.source_key, "title": self.title, "status": self.status,
            "order": self.order,
        }
        if self.body:
            out["body"] = self.body
        if self.priority:
            out["priority"] = self.priority
        if self.labels:
            out["labels"] = sorted(self.labels)
        if self.tasks:
            out["tasks"] = [{"text": t, "status": s} for t, s in self.tasks]
            for position, refs in sorted(self.task_depends.items()):
                if 1 <= position <= len(self.tasks) and refs:
                    out["tasks"][position - 1]["blocked_by"] = list(refs)
        if self.depends_on:
            out["depends_on"] = sorted(self.depends_on)
        if self.parent:
            out["parent"] = self.parent
        if self.group:
            out["group"] = self.group
        if self.fields:
            out["fields"] = {k: self.fields[k] for k in sorted(self.fields)}
        if self.comments:
            out["comments"] = [{"author": a, "text": t} for a, t in self.comments]
        if self.line is not None:
            out["line"] = self.line
        return out


@dataclass
class Finding:
    """One tracker source: `{kind, path, count, summary}` in the result, items on the side."""
    kind: str
    path: str
    count: int
    summary: str
    truncated: bool = False
    items: list[TrackerItem] = field(default_factory=list)

    def to_dict(self) -> dict:
        out = {"kind": self.kind, "path": self.path, "count": self.count, "summary": self.summary}
        if self.truncated:
            out["truncated"] = True
        return out


# --------------------------------------------------------------------------- safe IO

def _inside(project: Path, path: Path) -> bool:
    """True when `path` really lives inside `project` — symlinks resolved.

    The whole tree walk goes through this, so a `docs -> /etc` symlink is a finding of
    nothing rather than a way to make Relay read the filesystem.
    """
    try:
        real = path.resolve()
        root = project.resolve()
    except OSError:                                      # pragma: no cover - unreadable path
        return False
    return real == root or root in real.parents


def _read_text(project: Path, path: Path, limit: int = MAX_FILE_BYTES) -> str | None:
    """The file's text, or None when it is absent, too big, not a file, or outside `project`.

    Decoding is UTF-8 with replacement (a tracker file may be anything), a UTF-8 BOM is
    dropped, and CRLF and lone CR are normalised to LF so every parser below sees `\\n`.
    """
    if not _inside(project, path):
        return None
    try:
        if not path.is_file():
            return None
        size = path.stat().st_size
        if size > limit:
            return None
        data = path.read_bytes()
    except OSError:
        return None
    if data.startswith(b"\xef\xbb\xbf"):
        data = data[3:]
    return data.decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "\n")


def _listdir(project: Path, directory: Path) -> list[Path]:
    """Sorted children of `directory`, capped, never leaving the project."""
    if not _inside(project, directory):
        return []
    try:
        if not directory.is_dir():
            return []
        entries = sorted(directory.iterdir())[:MAX_DIR_ENTRIES]
    except OSError:
        return []
    return [e for e in entries if _inside(project, e)]


def _find_files(project: Path, directory: Path, names: Sequence[str] | None = None,
                suffix: str | None = None, depth: int = MAX_DEPTH) -> list[Path]:
    """Files under `directory`, breadth-first, bounded by depth, entry count and symlinks."""
    out: list[Path] = []
    frontier = [(directory, 0)]
    while frontier and len(out) < MAX_DIR_ENTRIES:
        here, level = frontier.pop(0)
        for entry in _listdir(project, here):
            try:
                is_dir = entry.is_dir()
            except OSError:                              # pragma: no cover - vanished mid-walk
                continue
            if is_dir:
                if level < depth and not entry.is_symlink():
                    frontier.append((entry, level + 1))
                continue
            if names is not None and entry.name.lower() not in {n.lower() for n in names}:
                continue
            if suffix is not None and not entry.name.endswith(suffix):
                continue
            out.append(entry)
    return sorted(out)


def _rel(project: Path, path: Path) -> str:
    try:
        return path.relative_to(project).as_posix()
    except ValueError:                                   # pragma: no cover - guarded by _inside
        return path.as_posix()


def _text_key(text: str, seen: dict[str, int]) -> str:
    """A stable id for a line of prose: the first 8 hex of its digest.

    Line numbers move whenever anything above the item is edited, so an import keyed on them
    would re-propose the whole file after one insertion.  Repeats inside one file are
    disambiguated by their order of appearance, which is stable too.
    """
    digest = hashlib.sha256(" ".join(text.split()).encode("utf-8")).hexdigest()[:8]
    seen[digest] = seen.get(digest, 0) + 1
    return digest if seen[digest] == 1 else f"{digest}-{seen[digest]}"


# ------------------------------------------------------------------------------ git
#
# The git parts read files and nothing else: `git` is never run.  A probe happens while a
# modal question is open, in a directory the user has not yet consented to anything about,
# and a subprocess there could hit a hook, a credential helper or a submodule fetch.

_SECTION_RE = re.compile(r'^\[([A-Za-z0-9.\-]+)(?:\s+"((?:[^"\\]|\\.)*)")?\]\s*(.*)$')
_SUBSECTION_DOT_RE = re.compile(r'^([A-Za-z0-9\-]+)\.(.+)$')
_KEY_RE = re.compile(r"^([A-Za-z][A-Za-z0-9\-]*)\s*(?:=\s*(.*))?$")


def _unescape_subsection(text: str) -> str:
    out, i = [], 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            out.append(text[i + 1])
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _config_value(raw: str) -> str:
    """git-config value semantics: quotes, `\\` escapes, `#`/`;` comments, trailing space."""
    out: list[str] = []
    quoted = False
    i = 0
    while i < len(raw):
        char = raw[i]
        if char == '"':
            quoted = not quoted
            i += 1
            continue
        if char == "\\" and i + 1 < len(raw):
            nxt = raw[i + 1]
            out.append({"n": "\n", "t": "\t", "b": "\b", '"': '"', "\\": "\\"}.get(nxt, nxt))
            i += 2
            continue
        if char in "#;" and not quoted:
            break
        out.append(char)
        i += 1
    return "".join(out).strip() if not quoted else "".join(out).rstrip("\n")


def parse_git_config(text: str) -> list[tuple[str, str, str, str]]:
    """`(section, subsection, key, value)` for every variable, in file order.

    Sections and keys are lower-cased (git compares them case-insensitively); a subsection
    name keeps its case, because git does not fold it.  Line continuations (`\\` at end of
    line) are joined.  https://git-scm.com/docs/git-config#_configuration_file
    """
    out: list[tuple[str, str, str, str]] = []
    section = subsection = ""
    lines = text.split("\n")
    index = 0
    while index < len(lines):
        line = lines[index]
        index += 1
        while line.endswith("\\") and index < len(lines):
            line = line[:-1] + lines[index]
            index += 1
        stripped = line.strip()
        if not stripped or stripped[0] in "#;":
            continue
        if stripped.startswith("["):
            match = _SECTION_RE.match(stripped)
            if not match:
                continue
            name, quoted_sub, rest = match.group(1), match.group(2), match.group(3)
            if quoted_sub is None:
                # `[remote.origin]` — the legacy spelling; the first dot splits it.
                dotted = _SUBSECTION_DOT_RE.match(name)
                section, subsection = (dotted.group(1).lower(), dotted.group(2)) if dotted \
                    else (name.lower(), "")
            else:
                section, subsection = name.lower(), _unescape_subsection(quoted_sub)
            stripped = rest.strip()
            if not stripped or stripped[0] in "#;":
                continue
        match = _KEY_RE.match(stripped)
        if not match or not section:
            continue
        value = match.group(2)
        out.append((section, subsection, match.group(1).lower(),
                    "true" if value is None else _config_value(value)))
    return out


def _insteadof(entries: Sequence[tuple[str, str, str, str]]) -> list[tuple[str, str]]:
    """`(prefix, replacement)` pairs from `url.<base>.insteadOf`, longest prefix first.

    Only the repository's own config is consulted: reading `~/.gitconfig` would report a URL
    the user never wrote into this project, and the probe stays inside the project.
    """
    pairs = [(value, base) for section, base, key, value in entries
             if section == "url" and key == "insteadof" and value and base]
    return sorted(pairs, key=lambda p: (-len(p[0]), p[0]))


def apply_insteadof(url: str, rules: Sequence[tuple[str, str]]) -> str:
    """Rewrite `url` by the first (longest) matching `insteadOf` prefix, as git does."""
    for prefix, base in rules:
        if url.startswith(prefix):
            return base + url[len(prefix):]
    return url


_SCP_RE = re.compile(r"^(?:(?P<user>[^@/]+)@)?(?P<host>[^:/@]+):(?P<path>.+)$")
_URL_RE = re.compile(r"^(?P<scheme>[A-Za-z][A-Za-z0-9+.\-]*)://"
                     r"(?:(?P<user>[^@/]*)@)?(?P<host>[^/:]*)(?::(?P<port>\d+))?(?P<path>/.*)?$")


def split_remote_url(url: str) -> dict:
    """`{host, owner, repo}` for a remote URL, with None where it does not apply.

    Handles `https://host/owner/repo.git`, `ssh://git@host:22/owner/repo`, the scp-like
    `git@host:owner/repo.git`, `git://host/owner/repo`, Azure DevOps'
    `https://dev.azure.com/<org>/<project>/_git/<repo>`, and a plain local path (host None).
    """
    url = (url or "").strip()
    host = path = None
    if url.startswith("file://"):
        return {"host": None, "owner": None, "repo": None}
    match = _URL_RE.match(url)
    if match:
        if match.group("scheme").lower() == "file":      # pragma: no cover - caught above
            return {"host": None, "owner": None, "repo": None}
        host, path = (match.group("host") or "").lower(), match.group("path") or ""
    else:
        scp = _SCP_RE.match(url)
        if scp and not url.startswith("/") and not url.startswith("."):
            host, path = scp.group("host").lower(), "/" + scp.group("path")
    if host is None:
        return {"host": None, "owner": None, "repo": None}
    parts = [p for p in (path or "").split("/") if p]
    if parts and parts[-1].endswith(".git"):
        parts[-1] = parts[-1][:-4]
    if "_git" in parts:                                  # Azure DevOps: org/project/_git/repo
        cut = parts.index("_git")
        owner = "/".join(parts[:cut]) or None
        repo = parts[cut + 1] if len(parts) > cut + 1 else None
        return {"host": host, "owner": owner, "repo": repo}
    if len(parts) >= 2:
        return {"host": host, "owner": "/".join(parts[:-1]), "repo": parts[-1]}
    return {"host": host, "owner": None, "repo": parts[0] if parts else None}


#: Hosts whose forge is known without any configuration.
_KNOWN_HOSTS = {
    "github.com": "github", "www.github.com": "github", "ssh.github.com": "github",
    "gitlab.com": "gitlab", "salsa.debian.org": "gitlab", "invent.kde.org": "gitlab",
    "bitbucket.org": "bitbucket", "altssh.bitbucket.org": "bitbucket",
    "dev.azure.com": "azure-devops", "ssh.dev.azure.com": "azure-devops",
    "vs-ssh.visualstudio.com": "azure-devops",
    "codeberg.org": "gitea-like", "gitea.com": "gitea-like", "git.disroot.org": "gitea-like",
}


def forge_for(host: str | None, gh_hosts: Iterable[str] = ()) -> str:
    """The forge behind `host`: one of the seven names, `unknown` when nothing says.

    A GitHub Enterprise host has no giveaway in its name, so the only offline evidence is
    that the user's own `gh` is logged in to it — hence `gh_hosts`.
    """
    if not host:
        return "unknown"
    host = host.lower()
    if host in _KNOWN_HOSTS:
        return _KNOWN_HOSTS[host]
    if host in {h.lower() for h in gh_hosts}:
        return "github-enterprise"
    first = host.split(".")[0]
    if first in ("gitlab", "git-lab") or host.endswith(".gitlab.io"):
        return "gitlab"
    if first in ("gitea", "forgejo"):
        return "gitea-like"
    if first == "bitbucket":
        return "bitbucket"
    if host.endswith(".visualstudio.com"):
        return "azure-devops"
    if first == "github":
        # `github.<company>.com` is the usual Enterprise spelling, but a self-hosted anything
        # can be called that, so it is only a guess unless `gh` confirmed it above.
        return "github-enterprise"
    return "unknown"


def gh_config_dir(env: dict | None = None) -> Path:
    """Where `gh` keeps `hosts.yml`: `$GH_CONFIG_DIR`, else `$XDG_CONFIG_HOME/gh`, else `~/.config/gh`.

    https://cli.github.com/manual/gh_help_environment
    """
    env = os.environ if env is None else env
    explicit = (env.get("GH_CONFIG_DIR") or "").strip()
    if explicit:
        return Path(explicit)
    xdg = (env.get("XDG_CONFIG_HOME") or "").strip()
    base = Path(xdg) if xdg else Path(env.get("HOME", "~")).expanduser() / ".config"
    return base / "gh"


_HOSTS_KEY_RE = re.compile(r'^(?!\s)["\']?([A-Za-z0-9][A-Za-z0-9.\-]*)["\']?\s*:\s*$')


def gh_hosts(path: str | os.PathLike | None = None, env: dict | None = None) -> list[str]:
    """The hosts `gh` is configured for, read from `hosts.yml`'s top-level keys only.

    `hosts.yml` is a map of hostname to that host's settings, so the un-indented keys are the
    hosts and nothing below them is looked at — in particular no `oauth_token` is ever read
    into memory.  `path` is injectable so tests never touch the real one.
    """
    file = Path(path) if path is not None else gh_config_dir(env) / "hosts.yml"
    try:
        if not file.is_file() or file.stat().st_size > MAX_FILE_BYTES:
            return []
        text = file.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    out = []
    for line in text.replace("\r\n", "\n").split("\n"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = _HOSTS_KEY_RE.match(line.rstrip())
        if match and "." in match.group(1):
            out.append(match.group(1).lower())
    return sorted(set(out))


def resolve_git_paths(project: Path) -> tuple[Path, Path] | None:
    """`(git_dir, common_dir)` for this checkout, or None when there is no repository.

    `.git` is usually a directory, and then the two are the same.  In a linked worktree and
    in a submodule it is a *file* holding `gitdir: <path>`; that path may sit outside the
    project (a submodule's real repository lives in the superproject's `.git/modules/<name>`),
    which is git's own design and not a symlink escape, so it is followed — once, with no
    further indirection.  The two parts then differ and both are needed: a linked worktree
    keeps its own `HEAD` in `git_dir`, while `config` lives in the `common_dir` its
    `commondir` file names, so reading the branch from the common directory would report the
    *main* worktree's branch.  https://git-scm.com/docs/gitrepository-layout
    """
    dot = project / ".git"
    try:
        if dot.is_dir():
            git_dir = dot
        elif dot.is_file():
            if dot.stat().st_size > 64 * 1024:
                return None
            pointer = dot.read_text(encoding="utf-8", errors="replace").strip()
            if not pointer.startswith("gitdir:"):
                return None
            target = Path(pointer.split(":", 1)[1].strip())
            git_dir = target if target.is_absolute() else (project / target)
            if not git_dir.is_dir():
                return None
        else:
            return None
        common_dir = git_dir
        common = git_dir / "commondir"
        if common.is_file() and common.stat().st_size < 64 * 1024:
            shared = Path(common.read_text(encoding="utf-8", errors="replace").strip())
            candidate = shared if shared.is_absolute() else (git_dir / shared)
            if candidate.is_dir():
                common_dir = candidate
    except OSError:
        return None
    try:
        return git_dir.resolve(), common_dir.resolve()
    except OSError:                                      # pragma: no cover - unreadable path
        return None


#: Jira and Linear issue keys look the same on a branch (`PROJ-123`, `ENG-42`), so the probe
#: reports the shape it matched rather than claiming which tracker it belongs to.
TICKET_KEY_RE = re.compile(r"(?<![A-Za-z0-9])([A-Z][A-Z0-9]{1,9})-([0-9]{1,6})(?![A-Za-z0-9])")
TICKET_KEY_PATTERN = "[A-Z][A-Z0-9]{1,9}-[0-9]{1,6}"


def _head_branch(git_dir: Path) -> str | None:
    try:
        head = git_dir / "HEAD"
        if not head.is_file() or head.stat().st_size > 64 * 1024:
            return None
        text = head.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return None
    if text.startswith("ref:"):
        ref = text.split(":", 1)[1].strip()
        return ref[len("refs/heads/"):] if ref.startswith("refs/heads/") else ref
    return None                                          # detached HEAD: a sha names no ticket


def probe_git(project: Path, *, hosts: Sequence[str] = ()) -> dict:
    """The `git` block of `probe()`.  Reads `.git/config` and `.git/HEAD` as files."""
    resolved = resolve_git_paths(project)
    if resolved is None:
        return {"is_repo": False, "remotes": [], "primary": None,
                "primary_reason": "no git repository here", "branch": None, "ticket_keys": []}
    git_dir, common_dir = resolved
    config_text = ""
    config = common_dir / "config"
    try:
        if config.is_file() and config.stat().st_size <= MAX_FILE_BYTES:
            config_text = config.read_text(encoding="utf-8", errors="replace")
    except OSError:                                      # pragma: no cover - unreadable config
        config_text = ""
    entries = parse_git_config(config_text)
    rules = _insteadof(entries)
    remotes: dict[str, str] = {}
    for section, sub, key, value in entries:
        if section == "remote" and key == "url" and sub and sub not in remotes:
            remotes[sub] = value
    rows = []
    for name in sorted(remotes):
        url = apply_insteadof(remotes[name], rules)
        parts = split_remote_url(url)
        rows.append({"name": name, "url": url, "host": parts["host"], "owner": parts["owner"],
                     "repo": parts["repo"], "forge": forge_for(parts["host"], hosts)})
    primary, reason = _primary_remote(rows)
    branch = _head_branch(git_dir)
    keys = sorted({f"{m.group(1)}-{m.group(2)}" for m in TICKET_KEY_RE.finditer(branch or "")})
    return {
        "is_repo": True,
        "remotes": rows,
        "primary": primary,
        "primary_reason": reason,
        "branch": branch,
        "ticket_keys": [{"key": k, "pattern": TICKET_KEY_PATTERN, "looks_like": "jira-or-linear"}
                        for k in keys],
    }


def _primary_remote(rows: Sequence[dict]) -> tuple[str | None, str]:
    """Which remote an issue would belong to, and why — the reason is shown to the user.

    `upstream` beats `origin` because on a fork `origin` is the user's own copy and the
    issues live on the repository it was forked from.
    """
    names = [r["name"] for r in rows]
    if not names:
        return None, "this repository has no remotes"
    if "upstream" in names:
        if "origin" in names:
            return "upstream", ("upstream over origin: origin looks like a fork, and issues "
                                "are filed on the repository it was forked from")
        return "upstream", "upstream is the only conventional remote here"
    if "origin" in names:
        return "origin", "origin is this repository's own remote"
    return names[0], f"no upstream or origin; {names[0]} is the first remote by name"


# -------------------------------------------------------------------- markdown helpers

#: `- [ ] text`, `* [x] text`, `+ [-] text`, with the indent kept so nesting can be read.
CHECKBOX_RE = re.compile(r"^(?P<indent>[ \t]*)(?:[-*+]|\d+[.)])[ \t]+\[(?P<mark>.)\][ \t]*(?P<text>.*)$")
HEADING_RE = re.compile(r"^(?P<hashes>#{1,6})[ \t]+(?P<text>.*?)[ \t]*#*[ \t]*$")
#: A plain bullet with no checkbox, so a mixed list is not silently half-read.
BULLET_RE = re.compile(r"^(?P<indent>[ \t]*)(?:[-*+]|\d+[.)])[ \t]+(?P<text>.*)$")

#: How a checkbox mark reads.  `[-]`, `[/]` and `[~]` are the common "in progress" spellings
#: (Kiro writes `[-]`); anything else non-blank counts as done, which is what GitHub renders.
BOX_STATUS = {" ": "open", "": "open", "x": "done", "X": "done",
              "-": "in-progress", "/": "in-progress", "~": "in-progress",
              ">": "deferred", "!": "blocked"}


def _box_status(mark: str) -> str:
    return BOX_STATUS.get(mark, "done")


def _indent_width(text: str) -> int:
    return len(text.replace("\t", "    "))


def _strip_inline(text: str) -> str:
    """A checklist line's own text, without a trailing HTML comment or `<!-- t:xx -->` marker."""
    return re.sub(r"<!--.*?-->", "", text).strip()


def _read_checklist(project: Path, path: Path) -> list[TrackerItem]:
    """Unchecked `- [ ]` items and headed sections of a TODO/NOTES/IDEAS/ROADMAP/PLAN file.

    * A **top-level unchecked box** becomes one item; the boxes nested under it become its
      `## Tasks`, whatever their own marks, because they describe that one piece of work.
    * A **checked** top-level box is ignored — it is already done and importing it would fill
      the board with history nobody asked for (`docs/SWITCHBOARD-DESIGN.md` §7).
    * A **heading whose section has no boxes at all** becomes one item carrying that prose,
      which is how a hand-kept `IDEAS.md` of paragraphs comes across.

    The item's id is a digest of its own text, so editing the file elsewhere — or adding a
    line above it — does not make the next probe propose it again.
    """
    text = _read_text(project, path)
    if text is None:
        return []
    rel = _rel(project, path)
    lines = text.split("\n")
    seen: dict[str, int] = {}
    out: list[TrackerItem] = []
    heading = ""
    section_start = 0
    section_has_box = False
    prose: list[str] = []
    current: TrackerItem | None = None
    current_indent = 0

    def flush_section() -> None:
        """A section with no checkbox at all is one item made of its prose."""
        nonlocal prose
        body = "\n".join(prose).strip()
        if heading and not section_has_box and body:
            item = TrackerItem(kind="checklist", path=rel, source_id="", title=heading,
                               body=body[:4000], status="open", order=len(out), group=rel,
                               line=section_start, labels=["imported", "todo"])
            item.source_id = _text_key(f"section:{heading}", seen)
            out.append(item)
        prose = []

    for number, raw in enumerate(lines, 1):
        head = HEADING_RE.match(raw)
        if head:
            flush_section()
            current = None
            # The `# ` title names the document, not a section of it: the paragraph under it
            # is the file's preamble and is nobody's card.
            heading = head.group("text").strip() if len(head.group("hashes")) > 1 else ""
            section_start = number
            section_has_box = False
            continue
        box = CHECKBOX_RE.match(raw)
        if box:
            section_has_box = True
            indent = _indent_width(box.group("indent"))
            body_text = _strip_inline(box.group("text"))
            status = _box_status(box.group("mark"))
            if not body_text:
                continue
            if current is not None and indent > current_indent:
                if len(current.tasks) < MAX_ITEMS_PER_SOURCE:
                    current.tasks.append((body_text, status))
                continue
            current = None
            if status in ("done", "dropped"):
                continue                                  # checked items are ignored
            item = TrackerItem(kind="checklist", path=rel, source_id="", title=body_text[:200],
                               status=status, order=len(out), line=number, group=rel,
                               labels=["imported", "todo"])
            item.source_id = _text_key(body_text, seen)
            if heading:
                item.body = f"From `{'#' * 2} {heading}` in `{rel}`."
            out.append(item)
            current, current_indent = item, indent
            if len(out) >= MAX_ITEMS_PER_SOURCE:
                break
            continue
        if not raw.strip():
            current = None
        prose.append(raw)
    flush_section()
    return out


def _checklist_sources(project: Path) -> list[Path]:
    out = []
    for folder in CHECKLIST_DIRS:
        base = project / folder if folder else project
        for entry in _listdir(project, base):
            if entry.name.lower() in {n.lower() for n in CHECKLIST_NAMES} and entry.is_file():
                out.append(entry)
    return sorted(out)[:MAX_SOURCES_PER_KIND]


# ---------------------------------------------------------------------- existing board

def probe_board(project: Path) -> dict:
    """Whether this project already has a Switchboard, or something `migrate` could convert.

    Three answers, in the order they are looked for:

    * `board` — a `board.yaml` in one of `board.BOARD_FOLDERS` (`.switchboard/`, `switchboard/`,
      `issues/`, in that order) exists.  There is nothing to initialize; the init question is not
      asked at all.  The hidden spelling is looked for explicitly: a project initialized by Relay
      since 2026-09-19 keeps its board in `.switchboard/`, and a probe that only knew the visible
      names would offer to initialize a project that already has a board.
    * `pre-board` — one of those folders holding a tree of Markdown whose files carry the
      old `- **Field**: value` header block.  Detection is `board.migrate(apply=False)`, the
      very code that would convert it, so the count shown is the count that would convert.
    * `none`.
    """
    root = B.board_folder(project)
    if root is not None:
        board = B.Board(root, project)
        try:
            cards = len(board.card_paths())
        except OSError:                                  # pragma: no cover - unreadable tree
            cards = 0
        return {"present": True, "kind": "board", "folder": root.name,
                "path": _rel(project, root), "cards": cards, "convertible": 0}
    for name in B.BOARD_FOLDERS:
        directory = project / name
        if not directory.is_dir() or not _inside(project, directory):
            continue
        try:
            report = B.migrate(directory, apply=False, repo=project)
        except (B.BoardError, OSError, ValueError):
            continue
        convertible = len(report.converted)
        if not convertible and not report.skipped:
            continue
        return {"present": False, "kind": "pre-board", "folder": name,
                "path": _rel(project, directory), "cards": 0, "convertible": convertible,
                "unparsed": len(report.skipped),
                "summary": f"{convertible} pre-board issue(s) in {name}/ that `migrate` converts"}
    return {"present": False, "kind": "none", "folder": None, "path": None,
            "cards": 0, "convertible": 0}


# ------------------------------------------------------------------------------ hints
#
# A hint is something the user should know about and Relay will not import: it names a
# system Relay cannot read offline, or one with no repository-level data at all.

def probe_hints(project: Path, git: dict) -> list[dict]:
    hints: list[dict] = []
    templates = project / ".github" / "ISSUE_TEMPLATE"
    if templates.is_dir() and _inside(project, templates):
        files = [e for e in _listdir(project, templates)
                 if e.is_file() and e.suffix.lower() in (".md", ".yml", ".yaml")]
        if files:
            hints.append({"kind": "github-issue-templates", "path": _rel(project, templates),
                          "count": len(files),
                          "detail": "this project files issues on GitHub with "
                                    f"{len(files)} issue template(s)"})
    for name in ("issue_template.md", "ISSUE_TEMPLATE.md"):
        single = project / ".github" / name
        if single.is_file() and _inside(project, single):
            hints.append({"kind": "github-issue-templates", "path": _rel(project, single),
                          "count": 1, "detail": "this project files issues on GitHub "
                                                "(a single issue template)"})
            break
    for entry in git.get("ticket_keys") or []:
        hints.append({"kind": "ticket-key-in-branch", "path": ".git/HEAD", "count": 1,
                      "key": entry["key"], "pattern": entry["pattern"],
                      "detail": f"the branch name carries {entry['key']}, which looks like a "
                                "Jira or Linear key; Relay does not contact either"})
    return sorted(hints, key=lambda h: (h["kind"], h["path"], h.get("key", "")))


# ------------------------------------------------------------------- front matter, sections

_FRONT_RE = re.compile(r"\A---[ \t]*\n(.*?)\n---[ \t]*\n?", re.S)


def split_front_matter(text: str) -> tuple[dict, str]:
    """`(front, body)` for a Markdown file with `---` front matter.

    `board.parse_yaml` is the reader — the same restricted subset the Switchboard writes, and
    it already covers what these trackers emit (flat scalars, quoted scalars, flow and block
    sequences).  A file it refuses falls back to a forgiving `key: value` scan rather than
    being dropped: a tracker this probe cannot fully understand should still be *counted*.
    """
    match = _FRONT_RE.match(text)
    if not match:
        return {}, text
    raw, body = match.group(1), text[match.end():]
    try:
        return B.parse_yaml(raw), body
    except B.BoardError:
        return _loose_yaml(raw), body


_LOOSE_KEY_RE = re.compile(r"^(?P<key>[A-Za-z_][A-Za-z0-9_.\-]*)[ \t]*:[ \t]*(?P<value>.*)$")
_LOOSE_ITEM_RE = re.compile(r"^[ \t]+-[ \t]+(?P<value>.*)$")


def _loose_yaml(raw: str) -> dict:
    """Top-level `key: value` and `key:` + `  - item`, quotes stripped.  Nothing nested."""
    out: dict = {}
    key = None
    for line in raw.split("\n"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        item = _LOOSE_ITEM_RE.match(line)
        if item is not None and key is not None:
            out.setdefault(key, [])
            if isinstance(out[key], list):
                out[key].append(_unquote(item.group("value")))
            continue
        match = _LOOSE_KEY_RE.match(line)
        if match is None:
            continue
        key = match.group("key")
        value = match.group("value").strip()
        if value.startswith("[") and value.endswith("]"):
            inner = value[1:-1].strip()
            out[key] = [_unquote(v) for v in inner.split(",") if v.strip()] if inner else []
        elif value:
            out[key] = _unquote(value)
        else:
            out[key] = []
    return out


def _unquote(text: str) -> str:
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        return text[1:-1]
    return text


def _as_list(value) -> list[str]:
    if value is None or value == "":
        return []
    if isinstance(value, (list, tuple)):
        return [str(v).strip() for v in value if str(v).strip()]
    return [str(value).strip()] if str(value).strip() else []


def _sections(body: str) -> dict[str, str]:
    """`{heading: text}` for every `## `/`###` heading, first spelling wins, comments dropped.

    HTML-comment sentinels (Backlog.md wraps each section in `<!-- AC:BEGIN -->` and friends)
    are stripped, so the text is what a person wrote.
    """
    out: dict[str, str] = {}
    heading = ""
    buffer: list[str] = []
    for line in body.split("\n"):
        match = HEADING_RE.match(line)
        if match and len(match.group("hashes")) <= 3:
            if heading and heading not in out:
                out[heading] = "\n".join(buffer).strip()
            heading, buffer = match.group("text").strip(), []
            continue
        if re.match(r"^\s*<!--\s*[A-Z_:]+\s*-->\s*$", line):
            continue
        buffer.append(line)
    if heading and heading not in out:
        out[heading] = "\n".join(buffer).strip()
    return out


def _section(sections: dict[str, str], *names: str) -> str:
    """The first of `names` present, matched case-insensitively and ignoring a `(Optional)`."""
    folded = {re.sub(r"\s*\(optional\)\s*$", "", k.strip().lower()): v for k, v in sections.items()}
    for name in names:
        value = folded.get(name.strip().lower())
        if value:
            return value
    return ""


def _checkbox_items(text: str) -> list[tuple[str, str]]:
    """`(text, status)` for the checkboxes in a block, `#1 ` numbering stripped."""
    out = []
    for line in text.split("\n"):
        match = CHECKBOX_RE.match(line)
        if not match:
            continue
        body = _strip_inline(match.group("text"))
        body = re.sub(r"^#\d+[ \t]+", "", body)
        if body:
            out.append((body[:300], _box_status(match.group("mark"))))
    return out


# --------------------------------------------------------------------------- Backlog.md
#
# MrLesk/Backlog.md.  Shapes taken from the project's own source and its own `backlog/`:
#   layout + config names   https://github.com/MrLesk/Backlog.md/blob/main/src/constants/index.ts
#   discovery order         https://github.com/MrLesk/Backlog.md/blob/main/src/utils/backlog-directory.ts
#   front matter            https://github.com/MrLesk/Backlog.md/blob/main/src/markdown/serializer.ts
#                           https://github.com/MrLesk/Backlog.md/blob/main/src/markdown/parser.ts
#   section headings        https://github.com/MrLesk/Backlog.md/blob/main/src/markdown/section-titles.ts
#   `- [ ] #1 text` items   https://github.com/MrLesk/Backlog.md/blob/main/src/markdown/structured-sections.ts
#   a real task file        https://github.com/MrLesk/Backlog.md/blob/main/backlog/tasks/
#
# Notes that matter for an importer: the id in the *front matter* is upper-cased
# (`TASK-1`, `BACK-681`) while the *filename* is `task-1 - Title.md`, lower-cased, with
# " - " between id and title; sub-task ids are dotted (`BACK-535.2`); `assignee` is a list;
# statuses are free-form strings configured in `config.yml` (default To Do / In Progress /
# Done); acceptance-criteria checkboxes are written `- [ ] #1 text` but a legacy writer
# emits them unnumbered, so both are accepted.

BACKLOG_DIRS = ("backlog", ".backlog")
BACKLOG_CONFIGS = ("config.yml", "config.yaml")
BACKLOG_ROOT_CONFIG = "backlog.config.yml"
#: Where task files live.  `completed/` and `archive/` hold history: they are counted in the
#: summary but not imported, because a board filling up with finished work helps nobody.
BACKLOG_TASK_DIRS = ("tasks", "drafts")
BACKLOG_HISTORY_DIRS = ("completed", "archive/tasks", "archive/drafts")


def backlog_root(project: Path) -> Path | None:
    """The Backlog.md directory of `project`, honouring a root `backlog.config.yml`."""
    root_config = project / BACKLOG_ROOT_CONFIG
    text = _read_text(project, root_config)
    if text is not None:
        named = _loose_yaml(text).get("backlog_directory") or _loose_yaml(text).get("backlogDirectory")
        if named:
            candidate = project / str(named)
            if _inside(project, candidate) and candidate.is_dir():
                return candidate
    for name in BACKLOG_DIRS:
        directory = project / name
        if not (_inside(project, directory) and directory.is_dir()):
            continue
        if any((directory / c).is_file() for c in BACKLOG_CONFIGS):
            return directory
        if any((directory / d).is_dir() for d in BACKLOG_TASK_DIRS):
            return directory
    return None


def backlog_config(project: Path, root: Path) -> dict:
    for name in BACKLOG_CONFIGS:
        text = _read_text(project, root / name)
        if text is not None:
            return _loose_yaml(text)
    return {}


_BACKLOG_ID_RE = re.compile(r"^(?P<prefix>[A-Za-z]+)-(?P<body>\d+(?:\.\d+)*)$")


def _read_backlog(project: Path, root: Path) -> list[TrackerItem]:
    """Every task and draft in a Backlog.md directory, as items."""
    group = _rel(project, root)
    out: list[TrackerItem] = []
    for folder in BACKLOG_TASK_DIRS:
        base = root / folder
        for path in _find_files(project, base, suffix=".md", depth=1):
            if path.name.lower() == "readme.md":
                continue
            text = _read_text(project, path)
            if text is None:
                continue
            front, body = split_front_matter(text)
            task_id = str(front.get("id") or "").strip()
            if not task_id:
                continue
            title = str(front.get("title") or "").strip() or path.stem
            sections = _sections(body)
            description = _section(sections, "Description")
            notes = _section(sections, "Implementation Notes", "Notes", "Notes & Comments")
            summary = _section(sections, "Final Summary")
            request = description
            for heading, extra in (("Implementation Notes", notes), ("Final Summary", summary)):
                if extra:
                    request += f"\n\n### {heading}\n{extra}"
            tasks = _checkbox_items(_section(sections, "Acceptance Criteria"))
            tasks += _checkbox_items(_section(sections, "Implementation Plan"))
            status = str(front.get("status") or "").strip() or "open"
            if folder == "drafts":
                status = "open"
            labels = _as_list(front.get("labels"))
            if front.get("type"):
                labels.append(str(front["type"]))
            if folder == "drafts":
                labels.append("draft")
            fields: dict = {}
            assignees = _as_list(front.get("assignee"))
            if assignees:
                fields["assignee"] = assignees[0][:80]
            if front.get("milestone"):
                fields["milestone"] = str(front["milestone"])[:120]
            item = TrackerItem(
                kind="backlog-md", path=_rel(project, path), source_id=task_id, title=title[:200],
                body=request.strip()[:8000], status=status,
                priority=str(front.get("priority") or "").strip().lower() or None,
                labels=["imported", "backlog-md"] + labels, tasks=tasks[:100],
                depends_on=_as_list(front.get("dependencies")),
                parent=str(front.get("parent_task_id") or "").strip() or None,
                group=group, order=_backlog_order(front, task_id), fields=fields)
            out.append(item)
            if len(out) >= MAX_ITEMS_PER_SOURCE:
                break
    out.sort(key=lambda i: (i.order, i.source_id))
    for index, item in enumerate(out):
        item.order = index
    return out


def _backlog_order(front: dict, task_id: str) -> int:
    """Backlog.md's own ordering: `ordinal` when it is set, else the numeric part of the id."""
    ordinal = front.get("ordinal")
    try:
        return int(str(ordinal))
    except (TypeError, ValueError):
        pass
    match = _BACKLOG_ID_RE.match(task_id)
    if match:
        parts = match.group("body").split(".")
        try:
            return int(parts[0]) * 1000 + (int(parts[1]) if len(parts) > 1 else 0)
        except ValueError:                               # pragma: no cover - guarded by the regex
            return 0
    return 0


def _backlog_summary(project: Path, root: Path, items: Sequence[TrackerItem]) -> str:
    history = 0
    for folder in BACKLOG_HISTORY_DIRS:
        history += len(_find_files(project, root / folder, suffix=".md", depth=1))
    open_count = sum(1 for i in items if i.status.strip().lower() not in ("done", "closed", "complete"))
    text = (f"Backlog.md in {_rel(project, root)}/: {len(items)} task(s), {open_count} not done")
    if history:
        text += f"; {history} in completed/ and archive/ are left alone"
    return text


# -------------------------------------------------------------------------------- Beads
#
# steveyegge/beads (the repository now answers as gastownhall/beads).  Shapes from:
#   storage modes and `.beads/` contents   https://github.com/gastownhall/beads#storage-modes
#   the JSONL export                       https://github.com/gastownhall/beads/blob/main/docs/cli-reference/export.md
#   the record, field by field             https://github.com/gastownhall/beads/blob/main/internal/types/types.go
#   the export record wrapper (`_type`)    https://github.com/gastownhall/beads/blob/main/cmd/bd/export.go
#   what the importer skips                https://github.com/gastownhall/beads/blob/main/cmd/bd/import_shared.go
#
# **The JSONL is an export, not the source of truth** — beads keeps its issues in a Dolt
# database under `.beads/embeddeddolt/` (or `.beads/dolt/`), and `issues.jsonl` is only
# written when `export.auto` is on.  Relay reads the JSONL because it is the only documented
# plain-file interchange shape; the probe's summary says so, so nobody mistakes a stale
# export for the tracker.  Reading Dolt would mean a dependency and a subprocess, and this
# module has neither.
#
# Dependencies are nested on the issue (`dependencies: [{depends_on_id, type}]`) and the edge
# points *from* the blocked issue, so `issue_id` is blocked by `depends_on_id`.  Priority is
# an integer, 0 = critical … 4 = backlog, and 0 is meaningful rather than missing.

BEADS_DIR = ".beads"
BEADS_JSONL = ("issues.jsonl", "global-issues.jsonl")
#: The edge types that actually block work; the rest are cross-references beads does not
#: treat as blockers (`docs/core-concepts/issues.md`, "Dependency Types").
BEADS_BLOCKING = ("blocks", "conditional-blocks", "waits-for")
BEADS_STATUS = {"open": "open", "in_progress": "in-progress", "blocked": "blocked",
                "deferred": "deferred", "closed": "done", "pinned": "open", "hooked": "open"}
BEADS_PRIORITY = {0: "critical", 1: "high", 2: "medium", 3: "low", 4: "lowest"}


def beads_sources(project: Path) -> list[Path]:
    base = project / BEADS_DIR
    if not (_inside(project, base) and base.is_dir()):
        return []
    out = [base / name for name in BEADS_JSONL if (base / name).is_file()]
    # A repository may commit the export at its root instead (beads' own does).
    root = project / "issues.jsonl"
    if not out and root.is_file() and _inside(project, root):
        out.append(root)
    return sorted(out)


def _read_beads(project: Path, path: Path) -> list[TrackerItem]:
    """One item per issue record of a beads JSONL export."""
    text = _read_text(project, path, MAX_JSON_BYTES)
    if text is None:
        return []
    rel = _rel(project, path)
    out: list[TrackerItem] = []
    for order, line in enumerate(text.split("\n")):
        line = line.strip()
        if not line or line[0] != "{":
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if not isinstance(record, dict):
            continue
        if "_schema" in record:                          # provenance header, skipped on import too
            continue
        record_type = record.get("_type")
        if record_type is not None and record_type != "issue":
            continue                                    # `memory` rows and other planes
        if record.get("status") == "tombstone":          # a legacy deletion marker
            continue
        issue_id = str(record.get("id") or "").strip()
        title = str(record.get("title") or "").strip()
        if not issue_id or not title:
            continue
        body_parts = []
        for key, heading in (("description", None), ("design", "Design"),
                             ("acceptance_criteria", "Acceptance criteria"), ("notes", "Notes")):
            value = str(record.get(key) or "").strip()
            if value:
                body_parts.append(value if heading is None else f"### {heading}\n{value}")
        labels = [str(v) for v in (record.get("labels") or []) if str(v).strip()]
        # The struct tag is `issue_type`; some of beads' own docs and tests write `type`, which
        # its importer ignores.  Both are read here: a file that says `type` still means it.
        issue_type = str(record.get("issue_type") or record.get("type") or "").strip()
        if issue_type:
            labels.append(issue_type)
        depends_on, parent = [], None
        for edge in record.get("dependencies") or []:
            if not isinstance(edge, dict):
                continue
            target = str(edge.get("depends_on_id") or "").strip()
            kind = str(edge.get("type") or "").strip()
            if not target:
                continue
            if kind in BEADS_BLOCKING:
                depends_on.append(target)
            elif kind == "parent-child" and parent is None:
                parent = target
        fields = {}
        who = str(record.get("assignee") or record.get("owner") or "").strip()
        if who:
            fields["assignee"] = who[:80]
        comments = []
        for comment in (record.get("comments") or [])[:20]:
            if isinstance(comment, dict) and str(comment.get("text") or "").strip():
                comments.append((str(comment.get("author") or "beads")[:60],
                                 str(comment["text"]).strip()[:4000]))
        priority = record.get("priority")
        out.append(TrackerItem(
            kind="beads", path=rel, source_id=issue_id, title=title[:200],
            body="\n\n".join(body_parts)[:8000],
            status=BEADS_STATUS.get(str(record.get("status") or "").strip(), "open"),
            priority=BEADS_PRIORITY.get(priority) if isinstance(priority, int) else None,
            labels=["imported", "beads"] + labels, depends_on=depends_on, parent=parent,
            group=rel, order=order, fields=fields, comments=comments))
        if len(out) >= MAX_ITEMS_PER_SOURCE:
            break
    return out


def _beads_summary(project: Path, path: Path, items: Sequence[TrackerItem]) -> str:
    dolt = any((project / BEADS_DIR / name).is_dir() for name in ("embeddeddolt", "dolt"))
    open_count = sum(1 for i in items if i.status not in ("done", "dropped"))
    text = f"Beads: {len(items)} issue(s) in {_rel(project, path)}, {open_count} not closed"
    if dolt:
        text += " (an export of the Dolt database beside it, which Relay does not read)"
    return text


# --------------------------------------------------------------------------- Task Master
#
# eyaltoledano/claude-task-master.  Shapes from:
#   the paths            https://github.com/eyaltoledano/claude-task-master/blob/main/src/constants/paths.js
#   the task object      https://github.com/eyaltoledano/claude-task-master/blob/main/docs/task-structure.md
#   the statuses         https://github.com/eyaltoledano/claude-task-master/blob/main/src/constants/task-status.js
#   tagged vs legacy     https://github.com/eyaltoledano/claude-task-master/blob/main/scripts/modules/utils.js
#                        (`hasTaggedStructure`: any top-level value with an array `tasks`)
#
# Two top-level shapes, both current: `{"<tag>": {"tasks": [...]}}` (tagged lists, v0.17+)
# and the legacy `{"tasks": [...]}`.  A task's `id` is documented as a number but strings
# occur in real files, and so do string `dependencies` — both are parsed defensively.
# Subtask ids are integers relative to their parent on disk; the dotted `21.4` spelling
# appears inside subtask `dependencies` and means "subtask 4 of task 21".

TASKMASTER_FILES = (".taskmaster/tasks/tasks.json", "tasks/tasks.json", "tasks.json")
TASKMASTER_STATUS = {"pending": "open", "in-progress": "in-progress", "review": "review",
                     "done": "done", "completed": "done", "deferred": "deferred",
                     "cancelled": "dropped", "canceled": "dropped", "blocked": "blocked"}


def taskmaster_sources(project: Path) -> list[Path]:
    for name in TASKMASTER_FILES:
        path = project / name
        if path.is_file() and _inside(project, path):
            return [path]
    return []


def _tm_task_lists(data) -> list[tuple[str, list]]:
    """`(tag, tasks)` pairs for either top-level shape, in `hasTaggedStructure`'s terms."""
    if not isinstance(data, dict):
        return []
    tagged = [(str(key), value["tasks"]) for key, value in data.items()
              if isinstance(value, dict) and isinstance(value.get("tasks"), list)]
    if tagged:
        return sorted(tagged)
    if isinstance(data.get("tasks"), list):
        return [("master", data["tasks"])]
    return []


def _tm_id(value) -> str:
    if isinstance(value, bool) or value is None:
        return ""
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return str(value).strip()


def _tm_subtask_depends(subtasks: Sequence[tuple[int, dict]], positions: dict[str, int],
                        tag: str, task_id: str, task_ids: set[str]) -> dict[int, list]:
    """A Task Master subtask's `dependencies`, as `TrackerItem.task_depends`.

    Task Master numbers a subtask inside its parent (`1`, `2`) and writes a dependency either
    as that bare sibling id or fully qualified (`"4.2"`, `"4"`).  So:

    * a bare id that is a sibling here  -> that sibling's position, a `blocked_by=` marker
      between two items of the same card;
    * `"<this task>.<sibling>"`         -> the same thing;
    * anything else that names another task -> that task's `source_id`, which `board_import`
      turns into `blocked_by=#CARD` when that task is in the same import, and drops otherwise.

    A dependency on another task's *subtask* becomes a dependency on that task's card: the
    board has no way to name an item of a card it is not on, and a card is the honest
    approximation — nearer than dropping it, which is what happened before 2026-09-18.
    """
    out: dict[int, list] = {}
    for position, sub in subtasks:
        refs: list = []
        for dep in sub.get("dependencies") or []:
            value = _tm_id(dep)
            if not value:
                continue
            head, _, tail = value.partition(".")
            if value in positions:
                ref = positions[value]
            elif tail and head == task_id and tail in positions:
                ref = positions[tail]
            elif head and head != task_id and head in task_ids:
                ref = f"{tag}/{head}"
            else:
                # A sibling that is not in this card after all, or a task that is not in this
                # file: dropped rather than written as a marker naming nothing, which is what
                # `relay-board.py check` would call an error.
                continue
            if ref != position and ref not in refs:
                refs.append(ref)
        if refs:
            out[position] = refs
    return out


def _read_taskmaster(project: Path, path: Path) -> list[TrackerItem]:
    text = _read_text(project, path, MAX_JSON_BYTES)
    if text is None:
        return []
    try:
        data = json.loads(text)
    except ValueError:
        return []
    rel = _rel(project, path)
    out: list[TrackerItem] = []
    order = 0
    for tag, tasks in _tm_task_lists(data):
        #: Every task id in this tag, so a subtask's dependency on another *task* can be told
        #: from one on a sibling that is not there.
        tag_ids = {_tm_id(t.get("id")) for t in tasks if isinstance(t, dict)} - {""}
        for task in tasks:
            if not isinstance(task, dict):
                continue
            task_id = _tm_id(task.get("id"))
            title = str(task.get("title") or "").strip()
            if not task_id or not title:
                continue
            body_parts = []
            for key, heading in (("description", None), ("details", "Details"),
                                 ("testStrategy", "Test strategy"),
                                 ("acceptanceCriteria", "Acceptance criteria")):
                value = str(task.get(key) or "").strip()
                if value:
                    body_parts.append(value if heading is None else f"### {heading}\n{value}")
            items: list[tuple[str, str]] = []
            #: Sub-id -> its 1-based position in `items`, so a subtask's `dependencies` can name
            #: a sibling.  Filled before the dependencies are read, because Task Master lets a
            #: subtask depend on a later one.
            positions: dict[str, int] = {}
            raw_subtasks: list[tuple[int, dict]] = []
            for sub in task.get("subtasks") or []:
                if not isinstance(sub, dict):
                    continue
                sub_title = str(sub.get("title") or "").strip()
                if not sub_title:
                    continue
                sub_status = TASKMASTER_STATUS.get(str(sub.get("status") or "").strip().lower(), "open")
                items.append((sub_title[:300], "open" if sub_status == "review" else sub_status))
                sub_id = _tm_id(sub.get("id"))
                if sub_id and sub_id not in positions:
                    positions[sub_id] = len(items)
                raw_subtasks.append((len(items), sub))
            task_depends = _tm_subtask_depends(raw_subtasks, positions, tag, task_id, tag_ids)
            labels = ["imported", "taskmaster"]
            if tag and tag != "master":
                labels.append(tag)
            out.append(TrackerItem(
                kind="taskmaster", path=rel, source_id=f"{tag}/{task_id}", title=title[:200],
                body="\n\n".join(body_parts)[:8000],
                status=TASKMASTER_STATUS.get(str(task.get("status") or "").strip().lower(), "open"),
                priority=str(task.get("priority") or "").strip().lower() or None,
                labels=labels, tasks=items[:100],
                task_depends={k: v for k, v in task_depends.items() if k <= 100},
                depends_on=[f"{tag}/{_tm_id(d)}" for d in (task.get("dependencies") or [])
                            if _tm_id(d)],
                group=rel, order=order))
            order += 1
            if len(out) >= MAX_ITEMS_PER_SOURCE:
                return out
    return out


def _taskmaster_summary(project: Path, path: Path, items: Sequence[TrackerItem]) -> str:
    tags = sorted({i.source_id.split("/", 1)[0] for i in items})
    subtasks = sum(len(i.tasks) for i in items)
    text = f"Task Master: {len(items)} task(s) and {subtasks} subtask(s) in {_rel(project, path)}"
    if len(tags) > 1:
        text += f"; tags: {', '.join(tags)}"
    return text


# ------------------------------------------------------- spec systems: spec-kit, Kiro, OpenSpec
#
# All three keep one directory per unit of work with a `tasks.md` inside it, so all three
# import as **one card per directory** with that file's checkboxes as the card's `## Tasks`.
# A card per checkbox line would put fifty cards on the board for one feature and lose the
# thing that made them a set.  The three differ in the task-line grammar and in what a box
# mark means, which is why each has its own line reader below.

_SPECKIT_LINE_RE = re.compile(
    r"^(?P<indent>[ \t]*)[-*+][ \t]+\[(?P<mark>.)\][ \t]*"
    r"(?P<id>T[0-9X]{3,})\b[ \t]*(?P<rest>.*)$")
_SPECKIT_TAG_RE = re.compile(r"^\[(?:P|US\d+)\][ \t]*")

_KIRO_LINE_RE = re.compile(
    r"^(?P<indent>[ \t]*)[-*+][ \t]+\[(?P<mark>.)\](?P<optional>\*?)[ \t]*"
    r"(?P<id>\d+(?:\.\d+)*)\.?[ \t]+(?P<rest>\S.*)$")

_OPENSPEC_LINE_RE = re.compile(
    r"^(?P<indent>[ \t]*)[-*+][ \t]+\[(?P<mark>[^\]]*)\][ \t]*"
    r"(?P<id>\d+(?:\.\d+)*)?[ \t]*(?P<rest>.*)$")


def _speckit_tasks(text: str) -> list[tuple[str, str]]:
    """`- [ ] T001 [P] [US1] Description` — https://github.com/github/spec-kit/blob/main/templates/tasks-template.md

    `[P]` (parallelizable) and `[US<n>]` (user story) are stripped from the front of the
    description; `TXXX` is the template's own placeholder and is skipped.  Both eras of the
    template are read: the older one has no `[US<n>]` tag and groups under `## Phase 3.1`.
    """
    out = []
    for line in text.split("\n"):
        match = _SPECKIT_LINE_RE.match(line)
        if not match:
            continue
        task_id = match.group("id")
        if "X" in task_id:
            continue
        rest = _strip_inline(match.group("rest"))
        while True:
            stripped = _SPECKIT_TAG_RE.sub("", rest)
            if stripped == rest:
                break
            rest = stripped
        if rest:
            out.append((f"{task_id} {rest}"[:300], _box_status(match.group("mark"))))
    return out


def _kiro_tasks(text: str) -> list[tuple[str, str]]:
    """`- [x] 1. Title` and `  - [ ]* 1.2 Title` — Kiro's spec task list.

    **Not documented by kiro.dev.**  The grammar here is taken from real Kiro-generated
    `.kiro/specs/*/tasks.md` files, including the ones in AWS's own `kirodotdev/Kiro`
    repository, so it is observed rather than contractual and the reader is lenient: a line
    it does not recognise is skipped, never guessed at.  `- [-]` is Kiro's in-progress mark
    (it appears exactly once per file, on the task in flight); `*` right after the bracket
    marks an optional task, which is kept in the text because dropping it loses the meaning.
    Unnumbered detail bullets and the trailing `_Requirements: 1.1, 2.3_` back-reference are
    not tasks and are left in the file.
    """
    out = []
    for line in text.split("\n"):
        match = _KIRO_LINE_RE.match(line)
        if not match:
            continue
        rest = _strip_inline(match.group("rest"))
        if not rest or rest.startswith("_Requirements:"):
            continue
        prefix = "(optional) " if match.group("optional") else ""
        out.append((f"{prefix}{match.group('id')} {rest}"[:300], _box_status(match.group("mark"))))
    return out


def _openspec_tasks(text: str) -> list[tuple[str, str]]:
    """`- [ ] 1.1 Task description`, grouped under `## 1. Section`.

    https://github.com/Fission-AI/OpenSpec/blob/main/schemas/spec-driven/schema.yaml says it
    exactly: a box holding only `x` is done, in either case and with any spacing, and **every
    other marker — `- [~]`, `- [-]`, an empty `- []` — reads as unfinished**.  That is the
    opposite of Kiro's `- [-]`, which is why this reader does not share `BOX_STATUS`.
    """
    out = []
    for line in text.split("\n"):
        match = _OPENSPEC_LINE_RE.match(line)
        if not match:
            continue
        rest = _strip_inline(match.group("rest"))
        if not rest or rest.startswith("<!--"):
            continue
        number = match.group("id")
        status = "done" if match.group("mark").strip().lower() == "x" else "open"
        out.append((f"{number} {rest}".strip()[:300], status))
    return out


def _title_from(project: Path, path: Path, fallback: str) -> str:
    """The `# ` heading of a Markdown file, or `fallback` (a directory name, humanised)."""
    text = _read_text(project, path)
    if text:
        match = re.search(r"^#[ \t]+(.+?)[ \t]*$", text, re.M)
        if match:
            title = re.sub(r"^(?:Implementation Plan|Tasks?|Spec(?:ification)?)[:\s-]+", "",
                           match.group(1).strip())
            if title.strip():
                return title.strip()[:200]
    return fallback


def _humanise(name: str) -> str:
    """`001-user-auth` -> `User auth`; `add-dark-mode` -> `Add dark mode`."""
    text = re.sub(r"^\d{3,}[-_]", "", name)
    text = re.sub(r"^\d{8}-\d{6}-", "", text)
    text = " ".join(part for part in re.split(r"[-_\s]+", text) if part)
    return (text[:1].upper() + text[1:]) if text else name


def _status_from_tasks(tasks: Sequence[tuple[str, str]]) -> str:
    """A directory's status read off its own checklist, since none of the three records one."""
    if not tasks:
        return "open"
    done = sum(1 for _, status in tasks if status in ("done", "dropped"))
    if done == len(tasks):
        return "done"
    if done or any(status == "in-progress" for _, status in tasks):
        return "in-progress"
    return "open"


def _spec_item(project: Path, kind: str, directory: Path, tasks_file: Path,
               tasks: Sequence[tuple[str, str]], *, group: str, order: int,
               title_from: Sequence[str], labels: Sequence[str] = (),
               note: str = "") -> TrackerItem:
    title = _humanise(directory.name)
    for name in title_from:
        candidate = directory / name
        if candidate.is_file():
            title = _title_from(project, candidate, title)
            break
    body = note
    for name in title_from:
        text = _read_text(project, directory / name)
        if text is None:
            continue
        # The prose under the `# ` title, or — when a template puts the summary in a section
        # instead, which all three of these systems do — that section's text.
        intro = _intro_of(text) or _section(_sections(text), "Overview", "Why", "Summary",
                                            "What Changes", "Problem")
        if intro:
            body = (body + "\n\n" if body else "") + intro[:2000]
        break
    extra = ""
    if len(tasks) > 100:
        extra = (f"\n\n{len(tasks)} checklist items were found; the first 100 are on this card "
                 f"and the rest stay in `{_rel(project, tasks_file)}`.")
    return TrackerItem(
        kind=kind, path=_rel(project, tasks_file), source_id=directory.name,
        title=title, body=(body + extra).strip()[:8000],
        status=_status_from_tasks(tasks), labels=["imported", kind] + list(labels),
        tasks=list(tasks)[:100], group=group, order=order)


def _intro_of(text: str, limit: int = 2000) -> str:
    """The prose between a Markdown file's `# ` title and its first `##` heading."""
    lines = text.split("\n")
    start = 0
    for index, line in enumerate(lines):
        if re.match(r"^#[ \t]+\S", line):
            start = index + 1
            break
    out = []
    for line in lines[start:]:
        if line.startswith("## "):
            break
        if CHECKBOX_RE.match(line):
            break
        out.append(line)
    return "\n".join(out).strip()[:limit]


def speckit_sources(project: Path) -> list[Path]:
    """`specs/<NNN-slug>/tasks.md` — https://github.com/github/spec-kit/blob/main/scripts/bash/create-new-feature.sh

    The directory is `<3-or-more digits>-<slug>`, or `YYYYMMDD-HHMMSS-<slug>` in the
    timestamp mode; anything else under `specs/` is left alone, so a repository that merely
    keeps written specs there is not mistaken for a spec-kit project.
    """
    base = project / "specs"
    out = []
    for entry in _listdir(project, base):
        if not entry.is_dir() or entry.is_symlink():
            continue
        if not (re.match(r"^\d{3,}-", entry.name) or re.match(r"^\d{8}-\d{6}-", entry.name)):
            continue
        tasks = entry / "tasks.md"
        if tasks.is_file() and _inside(project, tasks):
            out.append(tasks)
    return sorted(out)[:MAX_SOURCES_PER_KIND]


def kiro_sources(project: Path) -> list[Path]:
    """`.kiro/specs/<name>/tasks.md`, and the nested `.kiro/specs/<area>/<name>/tasks.md`
    that occurs in real projects — https://kiro.dev/docs/specs/best-practices.md"""
    return _find_files(project, project / ".kiro" / "specs", names=("tasks.md",),
                       depth=2)[:MAX_SOURCES_PER_KIND]


def openspec_sources(project: Path) -> list[Path]:
    """`openspec/changes/<change-id>/tasks.md`, excluding `changes/archive/<date>-<id>/`.

    https://github.com/Fission-AI/OpenSpec — an archived change is finished work, kept for
    the record; importing it would fill a new board with somebody else's history.
    """
    base = project / "openspec" / "changes"
    out = []
    for entry in _listdir(project, base):
        if not entry.is_dir() or entry.is_symlink() or entry.name == "archive":
            continue
        tasks = entry / "tasks.md"
        if tasks.is_file() and _inside(project, tasks):
            out.append(tasks)
    return sorted(out)[:MAX_SOURCES_PER_KIND]


def _read_speckit(project: Path, tasks_file: Path, order: int = 0) -> list[TrackerItem]:
    text = _read_text(project, tasks_file)
    if text is None:
        return []
    directory = tasks_file.parent
    return [_spec_item(project, "spec-kit", directory, tasks_file, _speckit_tasks(text),
                       group="specs", order=order, title_from=("spec.md", "plan.md"))]


def _read_kiro(project: Path, tasks_file: Path, order: int = 0) -> list[TrackerItem]:
    text = _read_text(project, tasks_file)
    if text is None:
        return []
    directory = tasks_file.parent
    return [_spec_item(project, "kiro", directory, tasks_file, _kiro_tasks(text),
                       group=".kiro/specs", order=order,
                       title_from=("requirements.md", "bugfix.md", "design.md"))]


def _read_openspec(project: Path, tasks_file: Path, order: int = 0) -> list[TrackerItem]:
    text = _read_text(project, tasks_file)
    if text is None:
        return []
    directory = tasks_file.parent
    return [_spec_item(project, "openspec", directory, tasks_file, _openspec_tasks(text),
                       group="openspec/changes", order=order,
                       title_from=("proposal.md", "design.md"))]


# --------------------------------------------------------------------------- the probe

def _checklist_findings(project: Path) -> list[Finding]:
    out = []
    for path in _checklist_sources(project):
        items = _read_checklist(project, path)
        if not items:
            continue
        boxes = sum(1 for i in items if i.line is not None)
        sections = len(items) - boxes
        parts = []
        if boxes:
            parts.append(f"{boxes} unchecked item(s)")
        if sections:
            parts.append(f"{sections} headed section(s)")
        out.append(Finding("checklist", _rel(project, path), len(items),
                           f"{_rel(project, path)}: {', '.join(parts)}", items=items))
    return out


def _backlog_findings(project: Path) -> list[Finding]:
    root = backlog_root(project)
    if root is None:
        return []
    items = _read_backlog(project, root)
    if not items:
        return []
    return [Finding("backlog-md", _rel(project, root), len(items),
                    _backlog_summary(project, root, items), items=items)]


def _beads_findings(project: Path) -> list[Finding]:
    out = []
    for path in beads_sources(project):
        items = _read_beads(project, path)
        if not items:
            continue
        out.append(Finding("beads", _rel(project, path), len(items),
                           _beads_summary(project, path, items), items=items))
    return out


def _taskmaster_findings(project: Path) -> list[Finding]:
    out = []
    for path in taskmaster_sources(project):
        items = _read_taskmaster(project, path)
        if not items:
            continue
        out.append(Finding("taskmaster", _rel(project, path), len(items),
                           _taskmaster_summary(project, path, items), items=items))
    return out


def _spec_findings(project: Path, kind: str, root: str, sources, reader, what: str) -> list[Finding]:
    """One finding for a spec system: its root, one card per spec directory under it."""
    items: list[TrackerItem] = []
    for order, tasks_file in enumerate(sources(project)):
        items.extend(reader(project, tasks_file, order))
    if not items:
        return []
    boxes = sum(len(i.tasks) for i in items)
    open_boxes = sum(1 for i in items for _, status in i.tasks if status not in ("done", "dropped"))
    return [Finding(kind, root, len(items),
                    f"{len(items)} {what} in {root}/ with {boxes} checklist item(s), "
                    f"{open_boxes} not done", items=items)]


def findings(project: str | os.PathLike, kinds: Sequence[str] | None = None) -> list[Finding]:
    """Every tracker finding in `project`, with its items, sorted by kind then path."""
    project = Path(project).expanduser()
    wanted = set(kinds) if kinds else set(TRACKER_KINDS)
    unknown = wanted - set(TRACKER_KINDS)
    if unknown:
        raise ProbeError(f"unknown tracker kind(s): {', '.join(sorted(unknown))}; "
                         f"known: {', '.join(TRACKER_KINDS)}")
    out: list[Finding] = []
    if not project.is_dir():
        return out
    if "checklist" in wanted:
        out.extend(_checklist_findings(project))
    if "backlog-md" in wanted:
        out.extend(_backlog_findings(project))
    if "beads" in wanted:
        out.extend(_beads_findings(project))
    if "taskmaster" in wanted:
        out.extend(_taskmaster_findings(project))
    if "spec-kit" in wanted:
        out.extend(_spec_findings(project, "spec-kit", "specs", speckit_sources,
                                  _read_speckit, "feature spec(s)"))
    if "kiro" in wanted:
        out.extend(_spec_findings(project, "kiro", ".kiro/specs", kiro_sources,
                                  _read_kiro, "Kiro spec(s)"))
    if "openspec" in wanted:
        out.extend(_spec_findings(project, "openspec", "openspec/changes", openspec_sources,
                                  _read_openspec, "OpenSpec change(s)"))
    for finding in out:
        finding.truncated = len(finding.items) >= MAX_ITEMS_PER_SOURCE
    return sorted(out, key=lambda f: (f.kind, f.path))


def items_for(project: str | os.PathLike, kinds: Sequence[str] | None = None) -> list[TrackerItem]:
    """Every importable item in `project` — what `board_import.propose` maps into cards."""
    out: list[TrackerItem] = []
    for finding in findings(project, kinds):
        out.extend(finding.items)
    return out


def probe(project: str | os.PathLike, *, gh_hosts_path: str | os.PathLike | None = None,
          env: dict | None = None, kinds: Sequence[str] | None = None) -> dict:
    """What Relay found in `project`, offline: git, an existing board, trackers, hints.

    Small, sorted and JSON-serialisable — it goes straight out as `project_probe_result` and
    straight into the init question's checkboxes.  It never raises for a project it cannot
    read: an unreadable corner is an absent finding, because this runs before the user has
    agreed to anything and a traceback in that dialog helps nobody.

    `gh_hosts_path` and `env` are injectable so tests never read the real `~/.config/gh`.
    """
    project = Path(os.path.abspath(Path(project).expanduser()))
    hosts = gh_hosts(gh_hosts_path, env)
    git = probe_git(project, hosts=hosts)
    found = findings(project, kinds)
    return {
        "version": PROBE_VERSION,
        "project": str(project),
        "git": git,
        "board": probe_board(project),
        "trackers": [f.to_dict() for f in found],
        "hints": probe_hints(project, git),
        "counts": {"trackers": len(found), "items": sum(f.count for f in found)},
        "gh_hosts": hosts,
    }

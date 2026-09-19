# SPDX-License-Identifier: AGPL-3.0-or-later
"""Two-way sync between Switchboard work cards and a forge's issues (card `#GDQN`).

The engine is provider-neutral: it knows cards, threads and the three-way merge, and talks to
GitHub (or GitLab, or Gitea) only through `ForgeProvider`.  It runs headless — no model call, no
GUI — and the only network traffic is whatever the provider makes while `plan()` or `run()` is
executing.

What never leaves the machine
-----------------------------
Only **shared work cards** sync.  Private cards (anything under the board's `.private/` root),
plan cards and memory cards never do: `shared_cards()` is the only thing ever rendered into a
request, so their words are never in one to begin with.  What could still slip through is a
*mention* — a private card's title or id quoted in a shared card, a plan's title as a link — and
that is caught in one place: every provider call the engine makes goes through `GuardedProvider`,
which scans the outgoing strings against `PrivacyGuard` and raises `ForgePrivacyError` before a
byte is sent.  The guard checks the text, not the call site, so a field added later cannot get
around it by forgetting to filter.

The three-way decision
----------------------
For every linked card the engine holds three versions: the last synced snapshot (`base`, in the
state file), the card as it is now, and the issue as it is now.  Per field:

* only the card changed  → push
* only the issue changed → pull
* both changed to the same value → nothing to do, the baseline moves on
* both changed, differently → a **conflict**: neither side is written, both versions are recorded
  on the card's thread and in the result, and the baseline stays where it was.

Fields are merged independently (title, prose, task checkboxes, labels, status, tab, assignee), so
a title edited here and a checkbox ticked on GitHub both land.

State
-----
`<board>/.private/forge-sync.json` (see `STATE_FILE`): per card the issue number, the last synced
card hash, the issue's `updated_at`/ETag, the baseline field snapshot and the comment id maps.  It
is inside the board's gitignored private root, holds no card text and no token, and is rewritten
atomically after every card, so a run that is killed half way resumes where it stopped.  Losing it
is not fatal: `links.github` in the card's front matter is the durable link, and the engine falls
back to a conservative merge (differences become conflicts, nothing is overwritten).

Docs: `docs/GITHUB-SYNC.md`.  Format: `docs/SWITCHBOARD-FORMAT.md`.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Sequence

from . import board as B
from . import logs

_log = logs.get("forge")

# --------------------------------------------------------------------------- markers

#: The card id, hidden in the issue body.  An HTML comment: GitHub drops it from the rendered
#: page but keeps it in the raw Markdown the web editor shows, so it survives a human edit unless
#: the human deletes the line.  If they do, `links.github` still names the issue and the next push
#: writes the marker back.
ID_MARKER_RE = re.compile(r"<!--\s*relay-id:\s*(?P<id>[0-9A-Za-z]{4})\s*-->", re.I)

#: The thread entry a Relay-authored comment came from.  A comment carrying one is never imported
#: back as a thread entry (that is the echo the sync must not make).
ENTRY_MARKER_RE = re.compile(
    r"<!--\s*relay-entry:\s*(?P<entry>[0-9A-Za-z:\-]+)(?:\s+card=(?P<card>[0-9A-Za-z]{4}))?\s*-->", re.I)

#: Everything from this line to the end of an issue body is written by Relay and is stripped
#: before the body is compared with, or copied into, a card.
FOOTER_MARKER = "<!-- relay-sync -->"

#: Label prefixes the sync owns.  Anything else on the issue is a plain card label.
TAB_LABEL = "tab:"
STATUS_LABEL = "status:"

#: Thread entry kinds that *could* become issue comments.  `event` (card moved, card created) and
#: the machine kinds stay local whatever the configuration says: the issue already shows the state
#: change as labels.
COMMENT_KINDS = ("comment", "question", "decision", "evidence", "progress", "note")

#: Owner, 2026-09-18: which of those are public by default.  A thread carries two sorts of entry —
#: what somebody said about the work (`comment`, `question`, `decision`, `note`) and what an agent
#: did while doing it (`progress`, `evidence`: run logs, screenshot paths, QA folders).  The first
#: sort belongs on the issue; the second is Relay's working record and would be noise on a public
#: tracker, so it stays here unless `board.yaml` asks for it:
#:
#:     github: {comment_kinds: [comment, question, decision, note, progress]}
DEFAULT_COMMENT_KINDS = ("comment", "question", "decision", "note")

#: How many issues a first sync may create before it needs `confirm_bulk`.  Creating 186 public
#: issues by accident is the failure this number exists to prevent.
DEFAULT_CREATE_CAP = 20

#: Where the state lives, relative to the board root.
STATE_FILE = f"{B.PRIVATE_FOLDER}/forge-sync.json"

#: The person -> GitHub login map, which is **per user** and never committed (owner, 2026-09-18):
#: two people syncing one board do not agree about who `Elliott` is on their forge, and a login is
#: somebody's account name rather than a property of the project.  So it lives in the same
#: gitignored private root as the state file, and `board.yaml` holds only what the project shares.
#: Either spelling is read, the first that exists winning; `.json` matches the state file beside
#: it, `.yaml` matches `board.yaml` for a person editing it by hand.  Neither is a card, so
#: `relay-board.py check` never looks at it.
LOGINS_FILES = (f"{B.PRIVATE_FOLDER}/forge-logins.yaml", f"{B.PRIVATE_FOLDER}/forge-logins.yml",
                f"{B.PRIVATE_FOLDER}/forge-logins.json")

STATE_VERSION = 1

#: Only work cards sync, and only the statuses a work card can have.
CLOSING_STATUSES = {"done": "completed", "dropped": "not_planned"}

#: What a reopened issue puts the card back to when its labels say nothing better.
REOPEN_STATUS = "ready"

#: Where an imported issue that nobody has triaged lands.
IMPORT_STATUS = "inbox"

#: The author the sync writes its own thread entries under.  They are never pushed back as
#: comments: a conflict note quotes the remote version, and posting it there would be an echo.
SYNC_AUTHOR = "sync"

MAX_COMMENT_BODY = 60000
MAX_ISSUE_BODY = 60000


# ---------------------------------------------------------------------------- errors

class ForgeError(Exception):
    """Anything the sync could not do.  Never carries a token."""


class ForgeAuthError(ForgeError):
    """No usable credential, or the forge refused the one we have."""


class ForgeUnavailable(ForgeError):
    """The forge could not be reached, or answered something unusable."""


class ForgeRateLimited(ForgeError):
    """A primary or secondary rate limit.  `retry_at` is a unix time, when the forge said one."""

    def __init__(self, message: str, retry_at: float | None = None):
        super().__init__(message)
        self.retry_at = float(retry_at) if retry_at else None

    @property
    def retry_at_text(self) -> str:
        return time.strftime("%H:%M", time.localtime(self.retry_at)) if self.retry_at else ""


class ForgePrivacyError(ForgeError):
    """The privacy guard refused an outgoing request.  Nothing was sent."""

    def __init__(self, message: str, card_id: str = "", where: str = ""):
        super().__init__(message)
        self.card_id = card_id
        self.where = where


# ------------------------------------------------------------------- provider types

@dataclass
class Issue:
    """One issue, in the vocabulary every provider translates into."""
    number: int
    title: str = ""
    body: str = ""
    state: str = "open"                 # open | closed
    state_reason: str = ""              # completed | not_planned | reopened | ""
    labels: list[str] = field(default_factory=list)
    assignees: list[str] = field(default_factory=list)
    updated_at: str = ""
    etag: str = ""
    url: str = ""
    is_pull_request: bool = False


@dataclass
class Comment:
    comment_id: int
    body: str = ""
    author: str = ""
    created_at: str = ""
    updated_at: str = ""
    url: str = ""


@dataclass
class RepoInfo:
    full_name: str
    has_issues: bool = True
    parent: str | None = None           # "owner/repo" of the fork's upstream, when it is a fork
    private: bool = False
    url: str = ""


class ForgeProvider:
    """What the engine needs from a forge.  Implemented by `forge_github.GitHubProvider`.

    Every method may raise `ForgeRateLimited`, `ForgeAuthError` or `ForgeUnavailable`.  The
    conditional ones return `None` to mean "not modified" (the ETag matched), which is how an idle
    sync costs no rate limit.
    """

    #: "owner/repo", for the result and for `links.github`.
    repo: str = ""
    #: The ETag of the last conditional listing, so the caller can store it and get a 304 next
    #: time.  Set by `list_issues` and `list_comments`.
    list_etag: str = ""
    comments_etag: str = ""

    def repo_info(self) -> RepoInfo:
        raise NotImplementedError

    def list_issues(self, since: str | None = None, etag: str | None = None) -> list[Issue] | None:
        """Every issue (never a pull request), newest change first.  None when not modified."""
        raise NotImplementedError

    def get_issue(self, number: int, etag: str | None = None) -> Issue | None:
        raise NotImplementedError

    def create_issue(self, title: str, body: str, labels: Sequence[str] = (),
                     assignees: Sequence[str] = ()) -> Issue:
        raise NotImplementedError

    def update_issue(self, number: int, *, title: str | None = None, body: str | None = None,
                     labels: Sequence[str] | None = None, assignees: Sequence[str] | None = None,
                     state: str | None = None, state_reason: str | None = None) -> Issue:
        raise NotImplementedError

    def list_comments(self, number: int, since: str | None = None,
                      etag: str | None = None) -> list[Comment] | None:
        raise NotImplementedError

    def create_comment(self, number: int, body: str) -> Comment:
        raise NotImplementedError

    def update_comment(self, comment_id: int, body: str) -> Comment:
        raise NotImplementedError

    def ensure_labels(self, names: Sequence[str]) -> list[str]:
        """Create any of `names` the repository does not have yet; returns the ones created."""
        raise NotImplementedError


# ------------------------------------------------------------------- privacy guard

#: A name shorter than this is not distinctive enough to guard on: it would refuse honest
#: syncs on a coincidence far more often than it would catch a leak, and the type filter in
#: `ForgeSync.shared_cards` already keeps the card itself off the wire.
NAME_MIN = 5


class PrivacyGuard:
    """The one choke point: nothing that identifies a private, plan or memory card goes out.

    Built from the board once per run.  `scan()` is called on every string of every outgoing
    request by `GuardedProvider`, so a leak has to get past one function, not past every call
    site.  It refuses rather than redacts: a refusal is visible in the result, a redaction would
    quietly ship a half-written issue.

    It guards **identities** — a private card's id, and the title (and `name`/`description`/
    `goal`) of every card that must not sync — matched on word boundaries, case-insensitively.
    That is what "including as a link title" means, and it is what a body, a comment or a label
    can carry by accident.  Guarding every sentence of a private body instead would refuse a
    perfectly ordinary card that happens to repeat a line, so the *content* of a card that does
    not sync is kept off the wire by never reading it into a request in the first place: only
    `shared_cards()` is ever rendered.
    """

    def __init__(self, board: B.Board):
        self.board = board
        self.ids: set[str] = set()
        #: (needle in lower case, what it is) — matched on word boundaries.
        self.names: list[tuple[str, str]] = []
        self._patterns: list[tuple, ] = []
        self._build()

    def _build(self) -> None:
        seen: set[str] = set()

        def add(text, source: str) -> None:
            needle = " ".join(str(text or "").split()).lower()
            if len(needle) < NAME_MIN or needle in seen:
                return
            seen.add(needle)
            self.names.append((needle, source))

        for card in self.board.cards():
            if card.id is None:
                continue
            private = card.private or _under_private(self.board, card)
            if private:
                self.ids.add(card.id.upper())
                add(card.title, f"private card #{card.id}")
            elif card.type not in ("work",):
                add(card.title, f"{card.type} card #{card.id}")
            else:
                continue
            for key in ("name", "description", "goal"):
                add(card.front.get(key), f"{card.type} card #{card.id}")
        self._patterns = [(re.compile(rf"(?<![0-9a-z]){re.escape(n)}(?![0-9a-z])"), source)
                          for n, source in self.names]
        self._id_patterns = [(re.compile(rf"(?<![0-9a-z]){re.escape(i.lower())}(?![0-9a-z])"), i)
                             for i in sorted(self.ids)]

    def scan(self, value, where: str = "") -> None:
        """Raise `ForgePrivacyError` if `value` carries anything that must stay here."""
        for text in _strings(value):
            low = " ".join(text.split()).lower()
            if not low:
                continue
            for pattern, card_id in self._id_patterns:
                if pattern.search(low):
                    raise ForgePrivacyError(
                        f"refused to send {where or 'a request'}: it names the private card "
                        f"#{card_id}, and private cards never leave this machine.",
                        card_id=card_id, where=where)
            for pattern, source in self._patterns:
                if pattern.search(low):
                    raise ForgePrivacyError(
                        f"refused to send {where or 'a request'}: it names a {source}, "
                        "which never leaves this machine.", where=where)


def _strings(value) -> Iterable[str]:
    if value is None:
        return ()
    if isinstance(value, str):
        return (value,)
    if isinstance(value, (list, tuple, set)):
        out: list[str] = []
        for item in value:
            out.extend(_strings(item))
        return out
    if isinstance(value, dict):
        out = []
        for key, item in value.items():
            out.extend(_strings(key))
            out.extend(_strings(item))
        return out
    return ()


def _under_private(board: B.Board, card: B.Card) -> bool:
    if card.path is None:
        return bool(card.private)
    try:
        Path(card.path).relative_to(board.private_root())
    except ValueError:
        return bool(card.private)
    return True


class GuardedProvider(ForgeProvider):
    """Every outgoing byte of the engine passes through here, and nowhere else.

    Reads pass straight through; anything that writes is scanned first.  `calls` records what was
    sent (without bodies), so a test can assert that a dry run touched nothing.
    """

    def __init__(self, inner: ForgeProvider, guard: PrivacyGuard):
        self.inner = inner
        self.guard = guard
        self.repo = getattr(inner, "repo", "")
        self.writes: list[tuple[str, tuple]] = []

    @property
    def list_etag(self) -> str:
        return getattr(self.inner, "list_etag", "") or ""

    @property
    def comments_etag(self) -> str:
        return getattr(self.inner, "comments_etag", "") or ""

    # ---- reads
    def repo_info(self) -> RepoInfo:
        return self.inner.repo_info()

    def list_issues(self, since=None, etag=None):
        return self.inner.list_issues(since, etag)

    def get_issue(self, number, etag=None):
        return self.inner.get_issue(number, etag)

    def list_comments(self, number, since=None, etag=None):
        return self.inner.list_comments(number, since, etag)

    # ---- writes
    def create_issue(self, title, body, labels=(), assignees=()):
        self.guard.scan([title, body, list(labels), list(assignees)], "a new issue")
        self.writes.append(("create_issue", (title,)))
        return self.inner.create_issue(title, body, labels, assignees)

    def update_issue(self, number, **fields):
        self.guard.scan(fields, f"an update to issue #{number}")
        self.writes.append(("update_issue", (number,)))
        return self.inner.update_issue(number, **fields)

    def create_comment(self, number, body):
        self.guard.scan(body, f"a comment on issue #{number}")
        self.writes.append(("create_comment", (number,)))
        return self.inner.create_comment(number, body)

    def update_comment(self, comment_id, body):
        self.guard.scan(body, f"an edit of comment {comment_id}")
        self.writes.append(("update_comment", (comment_id,)))
        return self.inner.update_comment(comment_id, body)

    def ensure_labels(self, names):
        self.guard.scan(list(names), "a label")
        self.writes.append(("ensure_labels", tuple(names)))
        return self.inner.ensure_labels(names)


# ------------------------------------------------------------------ body and fields

def normalize(text: str) -> str:
    """Line endings and trailing space, so a round trip through GitHub compares equal."""
    lines = [line.rstrip() for line in str(text or "").replace("\r\n", "\n").replace("\r", "\n").split("\n")]
    return "\n".join(lines).strip("\n")


def strip_h1(body: str) -> str:
    """A card body without its `# ` title line (the title is its own field)."""
    lines = str(body or "").splitlines()
    for index, line in enumerate(lines):
        if B._H1_RE.match(line):
            rest = lines[:index] + lines[index + 1:]
            return "\n".join(rest).lstrip("\n")
    return str(body or "")


def strip_footer(body: str) -> str:
    """An issue body without the block Relay appends (plan links and the id marker)."""
    text = str(body or "").replace("\r\n", "\n").replace("\r", "\n")
    cut = text.find(FOOTER_MARKER)
    if cut >= 0:
        text = text[:cut]
    # A marker a human moved out of the footer (or an older issue without one) still goes.
    text = ID_MARKER_RE.sub("", text)
    return text.rstrip("\n")


#: What a task line becomes while the prose around it is compared, so ticking a box is not
#: mistaken for a prose edit and vice versa.
TASK_SLOT = "\x00task\x00"


def split_body(body: str) -> tuple[str, tuple[tuple[str, bool, str], ...]]:
    """(prose with a slot per task line, the task items) for an issue-shaped body."""
    prose: list[str] = []
    tasks: list[tuple[str, bool, str]] = []
    for line in normalize(body).split("\n"):
        try:
            item = B.parse_task_line(line)
        except B.BoardError:
            item = None
        if item is None:
            prose.append(line)
            continue
        tasks.append((item.item_id or "", item.done, item.text.strip()))
        prose.append(TASK_SLOT)
    return "\n".join(prose).strip("\n"), tuple(tasks)


def render_body(title: str, prose: str, tasks: Sequence[tuple[str, bool, str]],
                taken: Iterable[str] = ()) -> str:
    """A card body from the merged fields: `# title`, the prose, the task lines in their slots."""
    # An item keeps the id it arrived with — that marker is how the two sides recognise each
    # other — so only the unmarked ones (a checkbox typed in the web UI) get a fresh one.
    used = {str(t).lower() for t in taken} | {i.lower() for i, _, _ in tasks if i}
    items: list[B.TaskItem] = []
    seen: set[str] = set()
    for item_id, done, text in tasks:
        identifier = (item_id or "").lower()
        if not identifier or identifier in seen:
            identifier = B.new_item_id(used)
        used.add(identifier)
        seen.add(identifier)
        items.append(B.TaskItem(item_id=identifier, text=text, status="done" if done else "open"))
    lines = prose.split("\n") if prose else []
    out: list[str] = []
    pending = list(items)
    last_slot = None
    for line in lines:
        if line == TASK_SLOT:
            if pending:
                out.append(pending.pop(0).render())
                last_slot = len(out)
            continue
        out.append(line)
    if pending:
        if last_slot is None:
            if out and out[-1].strip():
                out.append("")
            out.append("## Tasks")
            out.append("")
            last_slot = len(out)
        for item in pending:
            out.insert(last_slot, item.render())
            last_slot += 1
    body = "\n".join(out).strip("\n")
    return f"# {title}\n\n{body}\n" if body else f"# {title}\n"


def footer_for(card_id: str, plan_ids: Sequence[str] = ()) -> str:
    """The block Relay owns at the foot of an issue body: plan links and the hidden card id."""
    lines = [FOOTER_MARKER]
    if plan_ids:
        # A link, never a title: plan cards do not sync and their words stay here.
        lines.append("Relay plan cards (not synced): " + ", ".join(f"`#{p}`" for p in plan_ids))
    lines.append(f"<!-- relay-id: {card_id} -->")
    return "\n".join(lines)


def issue_body_with_footer(prose: str, card_id: str, plan_ids: Sequence[str] = ()) -> str:
    block = footer_for(card_id, plan_ids)
    body = f"{prose}\n\n{block}\n" if prose else f"{block}\n"
    return body[:MAX_ISSUE_BODY]


def issue_body_for(card: B.Card, plan_ids: Sequence[str] = ()) -> str:
    """The issue body a card pushes: its own body without the title, plus the hidden footer."""
    return issue_body_with_footer(normalize(strip_h1(card.body)), card.id or "", plan_ids)


@dataclass(frozen=True)
class Fields:
    """The mapped fields of a card, in the one vocabulary both sides are compared in."""
    title: str = ""
    prose: str = ""
    tasks: tuple = ()
    labels: tuple = ()
    status: str = ""
    tab: str = ""
    assignee: str | None = None

    def to_json(self) -> dict:
        return {"title": self.title, "prose": self.prose,
                "tasks": [list(t) for t in self.tasks], "labels": list(self.labels),
                "status": self.status, "tab": self.tab, "assignee": self.assignee}

    @classmethod
    def from_json(cls, data) -> "Fields | None":
        if not isinstance(data, dict):
            return None
        return cls(title=str(data.get("title") or ""), prose=str(data.get("prose") or ""),
                   tasks=tuple(tuple(t) for t in (data.get("tasks") or [])),
                   labels=tuple(data.get("labels") or []),
                   status=str(data.get("status") or ""), tab=str(data.get("tab") or ""),
                   assignee=data.get("assignee"))


FIELD_NAMES = ("title", "prose", "tasks", "labels", "status", "tab", "assignee")


# ----------------------------------------------------------------------- tab helpers
#
# One copy, in `board`, shared with `board_tools.BoardTools`: the sync writes card files and has
# to put them where the pane's own writes would (`board.tab_of` and friends).  These names stay
# because the engine and its tests read like this.

tab_folders = B.tab_folders
tab_of = B.tab_of
category_for_tab = B.category_for_tab


# --------------------------------------------------------------- the board's configuration

def read_logins(board: B.Board) -> dict[str, str]:
    """The person -> GitHub login map for *this* user, or `{}` (see `LOGINS_FILES`).

    The file is either a bare map, or one under a `logins:` key.  Anything unreadable, or not a
    map of scalars, is no map at all: an unsyncable assignee is a field the engine leaves alone,
    which is a great deal better than failing the run over a private file somebody mistyped.
    """
    for name in LOGINS_FILES:
        path = board.root / name
        try:
            raw = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        try:
            data = json.loads(raw) if path.suffix == ".json" else B.parse_yaml(raw)
        except (ValueError, B.BoardError):
            return {}
        if not isinstance(data, dict):
            return {}
        if isinstance(data.get("logins"), dict):
            data = data["logins"]
        return {str(k): str(v) for k, v in data.items()
                if isinstance(v, (str, int)) and not isinstance(v, bool) and str(v).strip()}
    return {}


def board_config(board: B.Board) -> dict:
    """`board.yaml`'s `github:` block plus this user's login map, as `ForgeSync` kwargs.

    What is in `board.yaml` is what the project agrees on and commits — the repository, an
    Enterprise `base_url`, the bulk `create_cap`, the `default_tab` an imported issue lands in,
    and which thread kinds may be posted publicly.  The login map is not one of those; it is
    read from the private root (`read_logins`).
    """
    block = board.config().get("github") if board.config_path.is_file() else None
    block = block if isinstance(block, dict) else {}
    out: dict = {"repo": "", "base_url": None, "create_cap": DEFAULT_CREATE_CAP,
                 "default_tab": None, "comment_kinds": list(DEFAULT_COMMENT_KINDS),
                 "login_map": read_logins(board)}
    for key in ("repo", "base_url", "default_tab"):
        value = block.get(key)
        if isinstance(value, str) and value.strip():
            out[key] = value.strip()
    cap = block.get("create_cap")
    if isinstance(cap, int) and not isinstance(cap, bool) and cap >= 0:
        out["create_cap"] = cap
    kinds = block.get("comment_kinds")
    if isinstance(kinds, (list, tuple)):
        wanted = [str(k).strip().lower() for k in kinds]
        unknown = [k for k in wanted if k not in COMMENT_KINDS]
        if unknown:
            raise ForgeError(f"board.yaml github.comment_kinds: unknown entry "
                             f"{', '.join(sorted(set(unknown)))}; the kinds a thread has are "
                             f"{', '.join(COMMENT_KINDS)}.")
        out["comment_kinds"] = [k for k in COMMENT_KINDS if k in wanted]
    # A `login_map:` in board.yaml is deliberately **not** read: it would commit other people's
    # account names to the project, and two people syncing one board do not agree about them.
    # Without the private file there is no map, and an assignee is a field the sync leaves alone.
    return out


# ------------------------------------------------------------------------- the state

class SyncState:
    """`<board>/.private/forge-sync.json`, rewritten atomically after every card."""

    def __init__(self, path: Path, repo: str = ""):
        self.path = Path(path)
        self.data: dict = {"version": STATE_VERSION, "repo": repo, "cards": {},
                           "issues_etag": "", "last_seen": ""}
        self.load()
        self.data.setdefault("cards", {})
        #: The repository this state was last written for, when it is not the one asked for.
        #: `ForgeSync._refuse_second_repo` turns it into a refusal; nothing is thrown away here.
        self.other_repo = ""
        self.other_cards = 0
        if repo and self.data.get("repo") and self.data.get("repo") != repo:
            self.other_repo = str(self.data["repo"])
            self.other_cards = sum(1 for e in (self.data.get("cards") or {}).values()
                                   if isinstance(e, dict) and e.get("number"))
        if repo and self.data.get("repo") != repo:
            # A board pointed at a different repository starts a fresh mapping rather than
            # pushing one project's cards onto another's issues.
            self.data = {"version": STATE_VERSION, "repo": repo, "cards": {},
                         "issues_etag": "", "last_seen": ""}

    def load(self) -> None:
        try:
            raw = self.path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            return
        try:
            data = json.loads(raw)
        except ValueError:
            return
        if isinstance(data, dict) and int(data.get("version") or 0) == STATE_VERSION:
            self.data = data

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        B.atomic_write(self.path, json.dumps(self.data, indent=1, sort_keys=True) + "\n", mode=0o600)

    # ---- per card
    def card(self, card_id: str) -> dict:
        return self.data["cards"].setdefault(card_id.upper(), {})

    def get(self, card_id: str) -> dict | None:
        return self.data["cards"].get(card_id.upper())

    def by_number(self, number: int) -> str | None:
        for card_id, entry in self.data["cards"].items():
            if entry.get("number") == number:
                return card_id
        return None


# ------------------------------------------------------------------------- planning

@dataclass
class CardPlan:
    """What the sync would do, or did, for one card."""
    card_id: str
    title: str = ""
    number: int | None = None
    action: str = "none"                 # create | update | pull | both | none | conflict | error | import
    push: list = field(default_factory=list)
    pull: list = field(default_factory=list)
    conflicts: list = field(default_factory=list)   # [{field, card, issue}]
    comments_out: int = 0
    comments_in: int = 0
    close: str = ""                      # "" | completed | not_planned | reopen
    note: str = ""

    def to_dict(self) -> dict:
        return {"card": self.card_id, "title": self.title, "number": self.number,
                "action": self.action, "push": list(self.push), "pull": list(self.pull),
                "conflicts": [dict(c) for c in self.conflicts],
                "comments_out": self.comments_out, "comments_in": self.comments_in,
                "close": self.close, "note": self.note}


@dataclass
class SyncResult:
    repo: str = ""
    dry_run: bool = False
    cards: list[CardPlan] = field(default_factory=list)
    creates: int = 0
    pushed: int = 0
    pulled: int = 0
    imported: int = 0
    comments_out: int = 0
    comments_in: int = 0
    needs_confirm: bool = False
    cap: int = DEFAULT_CREATE_CAP
    idle: bool = False
    retry_at: float | None = None
    errors: list = field(default_factory=list)

    @property
    def conflicts(self) -> list[dict]:
        out = []
        for plan in self.cards:
            for conflict in plan.conflicts:
                out.append({"card": plan.card_id, "number": plan.number, **conflict})
        return out

    def to_dict(self) -> dict:
        out = {"repo": self.repo, "dry_run": self.dry_run, "creates": self.creates,
               "pushed": self.pushed, "pulled": self.pulled, "imported": self.imported,
               "comments_out": self.comments_out, "comments_in": self.comments_in,
               "conflicts": self.conflicts, "needs_confirm": self.needs_confirm,
               "cap": self.cap, "idle": self.idle, "errors": list(self.errors),
               "cards": [c.to_dict() for c in self.cards]}
        if self.retry_at:
            out["retry_at"] = int(self.retry_at)
            out["retry_at_text"] = time.strftime("%H:%M", time.localtime(self.retry_at))
        return out


# --------------------------------------------------------------------------- merging

CONFLICT_HEADING = "Sync conflict"


class _Missing:
    """"No baseline", which is not the same as a baseline of None (an unassigned card)."""

    def __repr__(self) -> str:                    # pragma: no cover - debugging only
        return "<no baseline>"


MISSING = _Missing()


def merge_scalar(base, card, issue) -> tuple[str, object]:
    """('same'|'push'|'pull'|'conflict', value) for one independently-mergeable field.

    `base` is `MISSING` when there is no baseline at all; `None` is an ordinary value (a card
    with no assignee), so the two must not be spelled the same way.
    """
    if card == issue:
        return "same", card
    if base is MISSING:
        return "conflict", card
    if card == base:
        return "pull", issue
    if issue == base:
        return "push", card
    return "conflict", card


def merge_labels(base: Sequence[str], card: Sequence[str], issue: Sequence[str]) -> tuple[set, set, set]:
    """(merged, the card must change, the issue must change) — a set merge, so both sides'
    additions survive and a removal on either side reaches the other."""
    base_set, card_set, issue_set = set(base or ()), set(card or ()), set(issue or ())
    added = (card_set - base_set) | (issue_set - base_set)
    removed = (base_set - card_set) | (base_set - issue_set)
    merged = (base_set | added) - removed
    # A label one side removed and the other re-added is kept: never lose information silently.
    merged |= (card_set & issue_set)
    # "changed", not "gained": a label removed on one side has to reach the other too.
    return merged, merged != card_set, merged != issue_set


def merge_tasks(base, card, issue) -> tuple[str, tuple]:
    """Three-way merge of the task list, per item id.

    A box ticked on GitHub and a task renamed here merge; the same box changed differently on
    both sides is a conflict.  Items the other side has never seen are added.
    """
    def by_id(items):
        out = {}
        extra = []
        for item_id, done, text in items or ():
            if item_id:
                out[item_id.lower()] = (done, text)
            else:
                extra.append((done, text))
        return out, extra

    base_map, _ = by_id(base)
    card_map, card_extra = by_id(card)
    issue_map, issue_extra = by_id(issue)
    conflict = False
    merged: list[tuple[str, bool, str]] = []
    # The card's order is the one the file keeps; anything only the issue has is appended.
    order: list[str] = []
    placed: set[str] = set()
    for item_id, _, _ in list(card or ()) + list(issue or ()):
        if item_id and item_id.lower() not in placed:
            placed.add(item_id.lower())
            order.append(item_id)
    for item_id in order:
        key = item_id.lower()
        in_card, in_issue, in_base = key in card_map, key in issue_map, key in base_map
        if in_card and not in_issue:
            if in_base:
                continue                    # removed on the issue side
            merged.append((key, card_map[key][0], card_map[key][1]))
            continue
        if in_issue and not in_card:
            if in_base:
                continue                    # removed here
            merged.append((key, issue_map[key][0], issue_map[key][1]))
            continue
        base_item = base_map.get(key)
        done_state, done = merge_scalar(base_item[0] if base_item else MISSING,
                                        card_map[key][0], issue_map[key][0])
        text_state, text = merge_scalar(base_item[1] if base_item else MISSING,
                                        card_map[key][1], issue_map[key][1])
        if "conflict" in (done_state, text_state):
            conflict = True
        merged.append((key, bool(done), str(text)))
    # Unmarked items (a checkbox someone typed in the GitHub web UI) always come along.
    for done, text in issue_extra:
        if not any(text == t for _, _, t in merged):
            merged.append(("", bool(done), text))
    for done, text in card_extra:
        if not any(text == t for _, _, t in merged):
            merged.append(("", bool(done), text))
    return ("conflict" if conflict else "merged"), tuple(merged)


# ------------------------------------------------------------------------ the engine

class ForgeSync:
    """One board, one repository, one run.

    `plan()` reads both sides and returns exactly what `run()` would do; `run()` does it.  Neither
    ever deletes anything: a closed card closes its issue, a card that vanished locally is left
    alone, and a conflict writes to neither side.
    """

    def __init__(self, board: B.Board, provider: ForgeProvider, *, state_path: str | os.PathLike | None = None,
                 login_map: dict | None = None, create_cap: int = DEFAULT_CREATE_CAP,
                 default_tab: str | None = None, clock: Callable[[], float] = time.time,
                 import_issues: bool = True, comment_kinds: Sequence[str] | None = None,
                 on_progress: Callable[[dict], None] | None = None):
        self.board = board
        self.guard = PrivacyGuard(board)
        self.provider = GuardedProvider(provider, self.guard)
        self.repo = getattr(provider, "repo", "") or ""
        self.state = SyncState(Path(state_path) if state_path else board.root / STATE_FILE, self.repo)
        #: The issue listing's `ETag` and high-water mark, held back until the run is complete
        #: (`_issue_index`, `_commit_listing`).
        self._listing: dict = {}
        #: card id -> the file's hash when this run read it, so a write can tell that somebody
        #: else has edited the card since (`_base_hash`).  A sync runs on its own thread while
        #: the pane and the agent write through `BoardTools`, and the hash is the only thing
        #: standing between the two.
        self._read_hash: dict[str, str] = {}
        self._refuse_second_repo()        # reads the cards, so the two maps above exist first
        self.login_map = {str(k): str(v) for k, v in (login_map or {}).items()}
        self.reverse_logins = {v.lower(): k for k, v in self.login_map.items()}
        self.create_cap = int(create_cap)
        self.default_tab = default_tab
        #: Which thread kinds go out as public comments (`DEFAULT_COMMENT_KINDS`, or the
        #: `github: {comment_kinds: [...]}` override read by `board_config`).
        wanted = set(comment_kinds if comment_kinds is not None else DEFAULT_COMMENT_KINDS)
        self.comment_kinds = tuple(k for k in COMMENT_KINDS if k in wanted)
        self.clock = clock
        self.import_issues = import_issues
        #: Called after every card a run applies, with `{card, title, action, number, done,
        #: total}`, so the pane can show a progress line.  Never called by `plan()`, which
        #: changes nothing.  An exception from it never fails the sync.
        self.on_progress = on_progress
        #: The comment listing each linked card's plan already paid for, so `run()` does not ask
        #: twice.  `None` means the listing answered 304 and there is nothing to do.
        self._comment_cache: dict[str, list | None] = {}

    # ---- one board, one repository -------------------------------------------
    def _refuse_second_repo(self) -> None:
        """A board whose cards are already linked to another repository is not re-pointed here.

        Owner, 2026-09-18: one board syncs to one repository.  `links.github` holds a single
        `owner/repo#n`, so a second repository would either overwrite those links or file every
        card again as a new issue over there — and the first sync against the new repository is
        exactly when nobody is watching.  So it is refused, by name, before any request is made.
        Moving a board deliberately means clearing the links (and the state file) first.
        """
        linked = self.state.other_cards
        other = self.state.other_repo
        for card in self.shared_cards():
            value = (card.front.get("links") or {}).get("github") if isinstance(
                card.front.get("links"), dict) else None
            match = re.match(r"^(?P<repo>[^#\s]+)#\d+$", str(value or "").strip())
            if match and match.group("repo") != self.repo:
                other = other or match.group("repo")
                linked += 1
        if other and linked:
            raise ForgeError(
                f"This board is already synced with {other} ({linked} card(s) linked). One board "
                f"syncs to one repository, so it will not be pointed at {self.repo}: nothing has "
                f"been sent. To move it, clear `links.github` on those cards and delete "
                f"{STATE_FILE} first.")

    # ---- what syncs ----------------------------------------------------------
    def shared_cards(self) -> list[B.Card]:
        """Shared work cards, and nothing else.  The first half of the privacy rule; the guard
        is the second, and the one that a future field cannot get around."""
        out = []
        for card in self.board.cards(include_private=False):
            if card.id is None or card.type != "work":
                continue
            if card.private or _under_private(self.board, card):
                continue
            if card.path is not None:
                self._read_hash[card.id] = B.file_hash(card.path)
            out.append(card)
        return out

    def _base_hash(self, card: B.Card) -> str:
        """The card file's hash as this run read it — what a write must still find there.

        Taking it immediately before the write, which is what this code did, makes
        `Board.save`'s check compare a hash with itself: it always passes, so a card the user
        edited in the Switchboard pane (or the agent edited through `BoardTools`) while the sync
        thread was talking to GitHub was silently overwritten with the version read minutes
        earlier.  Reading it from here instead turns that race into a `BoardConflict`, which
        `_sync` records as an error on the card and nothing is lost.
        """
        known = self._read_hash.get(card.id or "")
        return known if known is not None else B.file_hash(card.path) if card.path else ""

    def _note_written(self, card: B.Card, written: str) -> None:
        """Remember what a successful save left on disk, so the next write in this run agrees."""
        if card.id:
            self._read_hash[card.id] = written

    # ---- field extraction ----------------------------------------------------
    def card_fields(self, card: B.Card) -> Fields:
        prose, tasks = split_body(strip_h1(card.body))
        labels = tuple(sorted(str(l) for l in (card.front.get("labels") or [])))
        assignee = card.front.get("assignee")
        assignee = str(assignee) if assignee else None
        if assignee is not None and assignee not in self.login_map:
            assignee = None                      # no mapping, no opinion about the field
        return Fields(title=card.title, prose=prose, tasks=tasks, labels=labels,
                      status=card.status, tab=tab_of(self.board, card), assignee=assignee)

    def issue_fields(self, issue: Issue, card_status: str = "") -> Fields:
        prose, tasks = split_body(strip_footer(issue.body))
        labels, tab, status_label = [], "", ""
        for label in issue.labels:
            if label.startswith(TAB_LABEL):
                tab = label[len(TAB_LABEL):]
            elif label.startswith(STATUS_LABEL):
                status_label = label[len(STATUS_LABEL):]
            else:
                labels.append(label)
        status = self.status_from_issue(issue, status_label, card_status)
        assignee = None
        for login in issue.assignees:
            if login.lower() in self.reverse_logins:
                assignee = self.reverse_logins[login.lower()]
                break
        return Fields(title=issue.title, prose=prose, tasks=tasks,
                      labels=tuple(sorted(labels)), status=status, tab=tab, assignee=assignee)

    def status_from_issue(self, issue: Issue, status_label: str, card_status: str) -> str:
        if issue.state == "closed":
            if issue.state_reason == "not_planned":
                return "dropped"
            if status_label in CLOSING_STATUSES:
                return status_label
            return "done"
        if status_label and status_label in B.WORK_STATUS_FOLDER and status_label not in CLOSING_STATUSES:
            return status_label
        # Reopened: the label still says done, so the card comes back to the lane it can be
        # picked up from rather than pretending it is still closed.
        if card_status in CLOSING_STATUSES or status_label in CLOSING_STATUSES:
            return REOPEN_STATUS
        return card_status or REOPEN_STATUS

    def issue_labels_for(self, fields: Fields) -> list[str]:
        labels = list(fields.labels)
        if fields.tab:
            labels.append(f"{TAB_LABEL}{fields.tab}")
        if fields.status:
            labels.append(f"{STATUS_LABEL}{fields.status}")
        return sorted(set(labels))

    # ---- reading the forge ---------------------------------------------------
    def _issue_index(self, cards: Sequence[B.Card]) -> tuple[dict[int, Issue], bool]:
        """Every issue that changed since the last run, by number, and whether it was a 304.

        The listing's `ETag` and high-water mark are **not** written into the state here; they go
        into `self._listing`, and `_commit_listing` moves them across only when a run has planned
        and applied every card.  Writing them at listing time was a way to lose a remote edit: the
        first per-card `state.save()` persists them, so a run that then stops — a rate limit is the
        ordinary case, and `_sync` returns right there — would leave the next run answered `304`,
        or filtered past the issues it never reached.  Those cards' `base` is intact, and
        `_plan_card` reads "no issue" as "the issue equals the baseline", so the next run would
        push the local version straight over the remote edit without seeing a conflict.
        """
        since = self.state.data.get("last_seen") or None
        etag = self.state.data.get("issues_etag") or None
        issues = self.provider.list_issues(since, etag)
        if issues is None:
            return {}, True
        index = {}
        newest = since or ""
        for issue in issues:
            if issue.is_pull_request:
                continue                      # a pull request is not an issue
            index[issue.number] = issue
            if issue.updated_at and issue.updated_at > newest:
                newest = issue.updated_at
        self._listing = {"issues_etag": self.provider.list_etag, "last_seen": newest}
        return index, False

    def _commit_listing(self) -> None:
        """Move the listing marks into the state, once everything this run planned has been done."""
        if self._listing:
            self.state.data.update(self._listing)
            self._listing = {}

    # ---- planning and running ------------------------------------------------
    def plan(self) -> SyncResult:
        return self._sync(dry_run=True, confirm_bulk=True)

    def run(self, *, confirm_bulk: bool = False) -> SyncResult:
        return self._sync(dry_run=False, confirm_bulk=confirm_bulk)

    def _sync(self, *, dry_run: bool, confirm_bulk: bool) -> SyncResult:
        result = SyncResult(repo=self.repo, dry_run=dry_run, cap=self.create_cap)
        try:
            info = self.provider.repo_info()
        except ForgeError as exc:
            result.retry_at = getattr(exc, "retry_at", None)
            result.errors.append(_clean(exc))
            return result
        if not info.has_issues:
            result.errors.append(
                f"{info.full_name} has issues disabled"
                + (f"; its upstream is {info.parent}" if info.parent else "") + ".")
            return result
        cards = self.shared_cards()
        try:
            index, not_modified = self._issue_index(cards)
        except ForgeError as exc:
            result.retry_at = getattr(exc, "retry_at", None)
            result.errors.append(_clean(exc))
            return result

        planned: list[tuple[B.Card | None, Issue | None, CardPlan]] = []
        creates = 0
        #: False as soon as a card is left unplanned or unapplied, which is what holds the
        #: listing's ETag and `last_seen` back (`_issue_index`).
        complete = True
        linked_numbers: set[int] = set()
        for card in cards:
            entry = self.state.get(card.id) or {}
            number = entry.get("number") or _linked_number(card, self.repo)
            issue = index.get(number) if number else None
            if number and issue is None and (not entry.get("base") or not entry.get("current")):
                # We know the number, and may not equate the issue with the baseline: either
                # there is none (state lost, or the card was linked by hand), or a conflict or
                # an error deliberately left it behind. A 304 or `since`-filtered listing only
                # says "nothing changed since the last listing", which for such a card is not
                # "the issue equals the baseline" — reading that way would push the local
                # version over the very remote edit the conflict was about. So the issue is
                # read (no etag: a real read, never a 304 that would say nothing about the
                # baseline) and the merge sees its true fields.
                try:
                    issue = self.provider.get_issue(int(number))
                except ForgeRateLimited as exc:
                    result.retry_at = exc.retry_at
                    result.errors.append(str(exc))
                    complete = False
                    break
                except ForgeError as exc:
                    plan = CardPlan(card.id, card.title, number, action="error", note=str(exc))
                    result.cards.append(plan)
                    result.errors.append(str(exc))
                    if not dry_run:
                        self._mark_lagging(card.id)
                        self.state.save()
                    continue
            if number:
                linked_numbers.add(int(number))
            try:
                plan = self._plan_card(card, number, issue, entry)
            except ForgeRateLimited as exc:
                result.retry_at = exc.retry_at
                result.errors.append(str(exc))
                complete = False
                break
            except (ForgeError, B.BoardError, OSError) as exc:
                result.cards.append(CardPlan(card.id, card.title, number, action="error",
                                             note=_clean(exc)))
                result.errors.append(_clean(exc))
                if not dry_run:
                    self._mark_lagging(card.id)
                    self.state.save()
                continue
            if plan.action == "create":
                creates += 1
            planned.append((card, issue, plan))
            result.cards.append(plan)

        # Issues nobody here knows about become cards (the other direction of a first sync).
        imports: list[Issue] = []
        if self.import_issues and not not_modified:
            for number, issue in sorted(index.items()):
                if number in linked_numbers:
                    continue
                marker = ID_MARKER_RE.search(issue.body or "")
                if marker and self.board.card_by_id(marker.group("id")) is not None:
                    continue
                if self.state.by_number(number):
                    continue
                imports.append(issue)
                result.cards.append(CardPlan("", issue.title, number, action="import"))

        result.creates = creates
        result.needs_confirm = bool(creates > self.create_cap and not confirm_bulk)
        result.idle = bool(not_modified and not imports and all(
            p.action == "none" for _, _, p in planned))
        result.comments_out = sum(p.comments_out for p in result.cards)
        result.comments_in = sum(p.comments_in for p in result.cards)
        if dry_run or result.needs_confirm:
            result.pushed = sum(1 for p in result.cards if p.push or p.action == "create")
            result.pulled = sum(1 for p in result.cards if p.pull)
            return result

        # ---- apply: the counters above are what *would* happen; from here they are what did.
        result.comments_out = result.comments_in = 0
        self._ensure_state_is_ignored()
        total = len(planned) + len(imports)
        done = 0
        for card, issue, plan in planned:
            try:
                self._apply_card(card, issue, plan, result)
            except ForgeRateLimited as exc:
                result.retry_at = exc.retry_at
                result.errors.append(str(exc))
                # The card may have stopped between a write and the baseline that records it,
                # so the next run must read its issue again rather than trust the baseline.
                self._mark_lagging(card.id)
                self.state.save()               # the listing marks stay behind: this run stopped
                return result
            except ForgePrivacyError as exc:
                plan.action, plan.note = "error", str(exc)
                result.errors.append(str(exc))
                self._mark_lagging(card.id)
            except (ForgeError, B.BoardError, OSError) as exc:
                plan.action, plan.note = "error", _clean(exc)
                result.errors.append(_clean(exc))
                self._mark_lagging(card.id)
            self.state.save()                 # per card, atomically: a crash resumes here
            done += 1
            self._progress(plan, done, total)
        for issue in imports:
            try:
                plan = next(p for p in result.cards if p.action == "import" and p.number == issue.number)
                self._import_issue(issue, plan, result)
            except ForgeRateLimited as exc:
                result.retry_at = exc.retry_at
                result.errors.append(str(exc))
                self.state.save()               # the listing marks stay behind: this run stopped
                return result
            except (ForgeError, B.BoardError, OSError) as exc:
                result.errors.append(_clean(exc))
            self.state.save()
            done += 1
            self._progress(plan, done, total)
        self.state.data["repo"] = self.repo
        if complete:
            # Every card this run planned has been applied, so the next run may trust the
            # listing marks: `since`/`If-None-Match` will not hide an issue nobody looked at.
            self._commit_listing()
        self.state.save()
        logs.event(_log, "forge_sync_done", repo=self.repo, pushed=result.pushed,
                   pulled=result.pulled, created=result.creates, conflicts=len(result.conflicts))
        return result

    def _progress(self, plan: CardPlan, done: int, total: int) -> None:
        if self.on_progress is None:
            return
        try:
            self.on_progress({"card": plan.card_id or None, "title": plan.title,
                              "action": plan.action, "number": plan.number,
                              "done": done, "total": total})
        except Exception:                        # a watcher must never break a sync
            logs.event(_log, "forge_progress_failed", level_name="debug", repo=self.repo)

    def _ensure_state_is_ignored(self) -> None:
        """The state file lives in the board's private root, which must stay out of git.

        A board scaffolded by Relay already has the rule; one made by hand before there was a
        `.gitignore` would otherwise get its first `.private/` from this sync and show it as
        untracked.  Writing the same line `board.scaffold_files` writes is the whole fix.
        """
        try:
            self.state.path.relative_to(self.board.private_root())
        except ValueError:
            return                              # the caller put the state somewhere else
        gitignore = self.board.root / ".gitignore"
        try:
            existing = gitignore.read_text(encoding="utf-8") if gitignore.exists() else ""
            if B.PRIVATE_FOLDER + "/" in existing:
                return
            B.atomic_write(gitignore, (existing.rstrip("\n") + "\n" if existing.strip() else "")
                           + B.GITIGNORE_TEXT)
        except OSError:                          # pragma: no cover - read-only checkout
            pass

    # ---- one card ------------------------------------------------------------
    def _plan_card(self, card: B.Card, number, issue: Issue | None, entry: dict) -> CardPlan:
        plan = CardPlan(card.id, card.title, int(number) if number else None)
        card_now = self.card_fields(card)
        if not number:
            plan.action = "create"
            plan.push = [f for f in FIELD_NAMES if getattr(card_now, f)]
            plan.comments_out = len(self._entries_to_push(card, {}))
            if card_now.status in CLOSING_STATUSES:
                plan.close = CLOSING_STATUSES[card_now.status]
            return plan
        base = Fields.from_json(entry.get("base"))
        issue_now = self.issue_fields(issue, card_now.status) if issue is not None else base
        if issue_now is None:
            issue_now = card_now                      # nothing known either way
        merged, push, pull, conflicts = self._merge(base, card_now, issue_now)
        plan.push, plan.pull, plan.conflicts = push, pull, conflicts
        if "status" in push and merged.status in CLOSING_STATUSES:
            plan.close = CLOSING_STATUSES[merged.status]
        elif "status" in push and issue is not None and issue.state == "closed":
            plan.close = "reopen"
        plan.comments_out = len(self._entries_to_push(card, self._already_posted(card, entry)))
        if issue is not None:
            plan.comments_in = self._count_imports(card, issue, entry)
        if conflicts:
            plan.action = "conflict"
        elif push and pull:
            plan.action = "both"
        elif push or plan.comments_out:
            plan.action = "update"
        elif pull or plan.comments_in:
            plan.action = "pull"
        else:
            plan.action = "none"
        if plan.action == "none" and plan.comments_in:
            plan.action = "pull"
        return plan

    def _merge(self, base: Fields | None, card: Fields, issue: Fields):
        push, pull, conflicts = [], [], []
        values = {}
        for name in ("title", "prose", "status", "tab", "assignee"):
            base_value = getattr(base, name) if base is not None else MISSING
            card_value, issue_value = getattr(card, name), getattr(issue, name)
            if name == "assignee" and not self.login_map:
                values[name] = card_value
                continue
            state, value = merge_scalar(base_value, card_value, issue_value)
            values[name] = value
            if state == "push":
                push.append(name)
            elif state == "pull":
                pull.append(name)
            elif state == "conflict":
                conflicts.append({"field": name, "card": _short(card_value), "issue": _short(issue_value)})
                values[name] = card_value          # nothing is overwritten on either side
        merged_labels, card_differs, issue_differs = merge_labels(
            base.labels if base is not None else card.labels, card.labels, issue.labels)
        values["labels"] = tuple(sorted(merged_labels))
        if issue_differs:
            push.append("labels")
        if card_differs:
            pull.append("labels")
        task_state, tasks = merge_tasks(base.tasks if base is not None else None, card.tasks, issue.tasks)
        values["tasks"] = tasks
        if task_state == "conflict":
            conflicts.append({"field": "tasks", "card": _short(_tasks_text(card.tasks)),
                              "issue": _short(_tasks_text(issue.tasks))})
            values["tasks"] = card.tasks
        else:
            if tasks != card.tasks:
                pull.append("tasks")
            if tasks != issue.tasks:
                push.append("tasks")
        return Fields(**values), push, pull, conflicts

    # ---- applying ------------------------------------------------------------
    def _mark_lagging(self, card_id: str) -> None:
        """A card whose run errored: its baseline was not confirmed against its issue.

        Nothing here decides whether the baseline *does* lag — a stop between two writes may
        have left it accurate — only that the next run may not *assume* it, so the issue is
        read again (the planning loop's `not entry.get("current")`) before anything is pushed.
        Only linked cards are marked: an unlinked one has no baseline to doubt.
        """
        entry = self.state.get(card_id)
        if entry and entry.get("number"):
            entry["current"] = False

    def _apply_card(self, card: B.Card, issue: Issue | None, plan: CardPlan, result: SyncResult) -> None:
        if plan.action == "error":
            return
        entry = self.state.card(card.id)
        card_now = self.card_fields(card)
        if plan.action == "create":
            issue = self._create_issue(card, card_now)
            plan.number = issue.number
            entry.update({"number": issue.number, "updated_at": issue.updated_at,
                          "etag": issue.etag, "base": card_now.to_json(),
                          "card_hash": B.file_hash(card.path), "current": True})
            self._link_card(card, issue.number)
            result.pushed += 1
            self._push_comments(card, issue.number, entry, plan, result)
            return
        if issue is None and plan.action in ("none",) and not plan.comments_out:
            return
        number = int(plan.number or entry.get("number") or 0)
        if not number:
            return
        base = Fields.from_json(entry.get("base"))
        issue_now = self.issue_fields(issue, card_now.status) if issue is not None else (base or card_now)
        merged, push, pull, conflicts = self._merge(base, card_now, issue_now)

        if pull:
            self._write_card(card, merged, pull)
            result.pulled += 1
        if push:
            self._push_issue(number, card, merged, push, issue, plan)
            result.pushed += 1
        elif issue is not None and not ID_MARKER_RE.search(issue.body or ""):
            self._repair_issue_marker(number, card, issue)
        if conflicts:
            self._record_conflict(card, number, conflicts, entry)
        # The baseline moves to what both sides now hold; a conflicted field keeps its old
        # baseline, so the conflict is surfaced again rather than silently accepted.
        new_base = merged.to_json()
        if conflicts and base is not None:
            for item in conflicts:
                new_base[item["field"]] = base.to_json()[item["field"]]
        entry["base"] = new_base
        entry["number"] = number
        entry["card_hash"] = B.file_hash(card.path)
        # `current` is what lets a later run equate an issue that is absent from the
        # changed-issues listing with this baseline (see `_sync`): it is true only when the
        # baseline records everything the run saw of the issue. A conflict keeps a field's
        # baseline behind on purpose, so for that card an unchanged listing is not an
        # unchanged issue, and the next run reads the issue again.
        entry["current"] = not conflicts
        if issue is not None:
            entry["updated_at"] = issue.updated_at
            entry["etag"] = issue.etag
        self._link_card(card, number)
        self._push_comments(card, number, entry, plan, result)
        if issue is not None:
            self._pull_comments(card, issue, entry, plan, result)

    def _create_issue(self, card: B.Card, fields: Fields) -> Issue:
        labels = self.issue_labels_for(fields)
        self.provider.ensure_labels(labels)
        assignees = [self.login_map[fields.assignee]] if fields.assignee in self.login_map else []
        issue = self.provider.create_issue(fields.title, issue_body_for(card, _plan_links(self.board, card)),
                                           labels, assignees)
        if fields.status in CLOSING_STATUSES:
            issue = self.provider.update_issue(issue.number, state="closed",
                                               state_reason=CLOSING_STATUSES[fields.status])
        return issue

    def _push_issue(self, number: int, card: B.Card, merged: Fields, push: list,
                    issue: Issue | None, plan: CardPlan) -> None:
        changes: dict = {}
        if "title" in push:
            changes["title"] = merged.title
        if {"prose", "tasks"} & set(push):
            pushed = B.Card(front=dict(card.front), body=render_body(merged.title, merged.prose, merged.tasks))
            changes["body"] = issue_body_for(pushed, _plan_links(self.board, card))
        if {"labels", "status", "tab"} & set(push):
            labels = self.issue_labels_for(merged)
            self.provider.ensure_labels(labels)
            changes["labels"] = labels
        if "assignee" in push and self.login_map:
            login = self.login_map.get(merged.assignee or "")
            changes["assignees"] = [login] if login else []
        if "status" in push:
            if merged.status in CLOSING_STATUSES:
                changes["state"] = "closed"
                changes["state_reason"] = CLOSING_STATUSES[merged.status]
            elif issue is not None and issue.state == "closed":
                changes["state"] = "open"
                changes["state_reason"] = "reopened"
        if not changes:
            return
        updated = self.provider.update_issue(number, **changes)
        plan.number = number
        if updated is not None:
            entry = self.state.card(card.id)
            entry["updated_at"] = updated.updated_at
            entry["etag"] = updated.etag

    def _repair_issue_marker(self, number: int, card: B.Card, issue: Issue) -> None:
        """Put the hidden `relay-id` back after a human deleted it, without touching the prose.

        The issue is still found through `links.github`, but an issue without the marker is one
        a fresh clone would import as a second card, so it is repaired the moment it is noticed.
        """
        body = issue_body_with_footer(strip_footer(issue.body), card.id or "",
                                      _plan_links(self.board, card))
        updated = self.provider.update_issue(number, body=body)
        if updated is not None:
            entry = self.state.card(card.id)
            entry["updated_at"] = updated.updated_at
            entry["etag"] = updated.etag

    def _write_card(self, card: B.Card, merged: Fields, pull: list) -> None:
        """Apply the pulled fields to the card file.  Bytes nothing pulled touches stay put."""
        base_hash = self._base_hash(card)
        if {"title", "prose", "tasks"} & set(pull):
            taken = {t.item_id for t in card.tasks() if t.item_id}
            card.body = render_body(merged.title, merged.prose, merged.tasks, taken)
        if "labels" in pull:
            card.set("labels", list(merged.labels))
        if "assignee" in pull and self.login_map:
            if merged.assignee:
                card.set("assignee", merged.assignee)
            else:
                card.drop("assignee")
        moved_to = None
        if {"status", "tab"} & set(pull):
            status = merged.status if "status" in pull else card.status
            if status in B.WORK_STATUS_FOLDER:
                card.set("status", status)
                tab = merged.tab if "tab" in pull and merged.tab else tab_of(self.board, card)
                category = category_for_tab(self.board, tab) or self.board.category_of(card.path)
                # `board.card_target_path` is what `BoardTools._move` asks too, so a card pulled
                # into `done` here lands in the same folder the pane would have moved it to.
                target = B.card_target_path(self.board, card, category)
                if target is not None and not target.exists():
                    moved_to = target
        self._note_written(card, self.board.save(card, base_hash=base_hash))
        if moved_to is not None:
            B.move_card_file(card, moved_to)
        self.board.append_thread(
            card.id, f"- ✦ pulled from {self.repo}: {', '.join(sorted(pull))}",
            author=SYNC_AUTHOR, kind="event", private=card.private, via="github", repo=self.repo)

    def _link_card(self, card: B.Card, number: int) -> None:
        link = f"{self.repo}#{number}"
        links = dict(card.front.get("links") or {})
        if links.get("github") == link:
            return
        links["github"] = link
        base_hash = self._base_hash(card)
        card.set("links", links)
        self._note_written(card, self.board.save(card, base_hash=base_hash))

    def _record_conflict(self, card: B.Card, number: int, conflicts: list, entry: dict) -> None:
        """Surface a conflict on the card's thread, once per distinct pair of versions."""
        fingerprint = hashlib.sha256(
            json.dumps(conflicts, sort_keys=True).encode("utf-8")).hexdigest()[:16]
        if entry.get("conflict") == fingerprint:
            return
        entry["conflict"] = fingerprint
        lines = [f"- ⚠ sync conflict with {self.repo}#{number}: "
                 f"{', '.join(c['field'] for c in conflicts)} changed on both sides. "
                 "Nothing was overwritten; resolve by editing one side and syncing again."]
        for item in conflicts:
            lines.append(f"  - **{item['field']}** here: {item['card']}")
            lines.append(f"  - **{item['field']}** on {self.repo}: {item['issue']}")
        self.board.append_thread(card.id, "\n".join(lines), author=SYNC_AUTHOR, kind="note",
                                 private=card.private, via="github", repo=self.repo,
                                 issue=str(number))

    # ---- comments ------------------------------------------------------------
    def _entries_to_push(self, card: B.Card, known) -> list[B.ThreadEntry]:
        out = []
        for entry in sorted(self.board.thread(card.id, card.private), key=lambda e: e.entry_id):
            if entry.kind not in self.comment_kinds:
                continue
            if entry.attrs.get("via") == "github" or entry.attrs.get("github_comment"):
                continue                       # it came from there; pushing it back is the echo
            if entry.author == SYNC_AUTHOR:
                continue                       # the sync's own notes (a conflict) stay here
            if entry.entry_id in known:
                continue
            out.append(entry)
        return out

    def _already_posted(self, card: B.Card, entry: dict) -> set[str]:
        """The thread entry ids the issue already carries as comments.

        Read from the comment listing this run already paid for, not only from the state file, so
        losing the state does not post every entry a second time.
        """
        out = set(entry.get("remote_entries") or [])
        out.update(str(k) for k in (entry.get("comments") or {}))
        for comment in self._comment_cache.get(card.id) or ():
            marker = ENTRY_MARKER_RE.search(comment.body or "")
            if marker:
                out.add(marker.group("entry"))
        return out

    def comment_body(self, card_id: str, entry: B.ThreadEntry) -> str:
        who = entry.attrs.get("author") or "relay"
        head = f"**{who}**" + (f" · {entry.kind}" if entry.kind != "comment" else "")
        text = entry.text.strip()
        marker = f"<!-- relay-entry: {entry.entry_id} card={card_id} -->"
        return f"{head}\n\n{text}\n\n{marker}\n"[:MAX_COMMENT_BODY]

    def _push_comments(self, card: B.Card, number: int, entry: dict, plan: CardPlan,
                       result: SyncResult) -> None:
        known = dict(entry.get("comments") or {})
        pushed = 0
        for item in self._entries_to_push(card, self._already_posted(card, entry)):
            comment = self.provider.create_comment(number, self.comment_body(card.id, item))
            known[item.entry_id] = comment.comment_id
            entry["comments"] = known
            self.state.save()                    # one comment at a time, so a crash never repeats
            pushed += 1
        plan.comments_out = pushed
        result.comments_out += pushed

    def _count_imports(self, card: B.Card, issue: Issue, entry: dict) -> int:
        try:
            comments = self.provider.list_comments(issue.number,
                                                   etag=entry.get("comments_etag") or None)
        except ForgeRateLimited:
            raise
        except ForgeError:
            return 0
        self._comment_cache[card.id] = comments
        if comments is None:
            return 0
        entry["comments_etag"] = self.provider.comments_etag
        return len(self._new_comments(card, comments, entry))

    def _new_comments(self, card: B.Card, comments: Sequence[Comment], entry: dict) -> list[Comment]:
        pulled = dict(entry.get("pulled") or {})
        seen = {e.attrs.get("github_comment") for e in self.board.thread(card.id, card.private)}
        # Comments Relay posted, even if a human edited the hidden marker out of one.
        ours = {str(v) for v in (entry.get("comments") or {}).values()}
        out = []
        for comment in comments:
            if ENTRY_MARKER_RE.search(comment.body or ""):
                continue                        # Relay wrote it; importing it would echo
            key = str(comment.comment_id)
            if key in seen or key in ours:
                continue
            known = pulled.get(key)
            if isinstance(known, dict) and known.get("updated_at") == comment.updated_at:
                continue
            if isinstance(known, str):
                continue
            out.append(comment)
        return out

    def _pull_comments(self, card: B.Card, issue: Issue, entry: dict, plan: CardPlan,
                       result: SyncResult) -> None:
        if card.id in self._comment_cache:
            comments = self._comment_cache[card.id]
        else:
            comments = self.provider.list_comments(issue.number, etag=entry.get("comments_etag") or None)
            if comments is not None:
                entry["comments_etag"] = self.provider.comments_etag
        if comments is None:
            return                            # 304: nothing on the issue moved
        pulled = dict(entry.get("pulled") or {})
        remote = set(entry.get("remote_entries") or [])
        for comment in comments:
            marker = ENTRY_MARKER_RE.search(comment.body or "")
            if marker:
                remote.add(marker.group("entry"))
                continue
        count = 0
        for comment in self._new_comments(card, comments, entry):
            key = str(comment.comment_id)
            previous = pulled.get(key)
            attrs = {"via": "github", "github_comment": key,
                     "github_author": comment.author or "github", "repo": self.repo}
            if isinstance(previous, dict) and previous.get("entry"):
                attrs["edit_of"] = previous["entry"]
            thread_entry = self.board.append_thread(
                card.id, _comment_text(comment), author=comment.author or "github",
                kind="comment", private=card.private, **attrs)
            pulled[key] = {"entry": thread_entry.entry_id, "updated_at": comment.updated_at}
            entry["pulled"] = pulled
            self.state.save()
            count += 1
        entry["pulled"] = pulled
        entry["remote_entries"] = sorted(remote)
        self._repair_markers(card, comments, entry)
        plan.comments_in = count
        result.comments_in += count

    def _repair_markers(self, card: B.Card, comments: Sequence[Comment], entry: dict) -> None:
        """A human editing a Relay comment in the web UI may drop the hidden marker; put it back,
        or the next sync would import our own words as if a stranger had written them."""
        known = {str(v): k for k, v in (entry.get("comments") or {}).items()}
        for comment in comments:
            key = str(comment.comment_id)
            if key not in known or ENTRY_MARKER_RE.search(comment.body or ""):
                continue
            marker = f"<!-- relay-entry: {known[key]} card={card.id} -->"
            self.provider.update_comment(comment.comment_id,
                                         (comment.body or "").rstrip("\n") + "\n\n" + marker + "\n")

    # ---- importing an issue nobody here knows --------------------------------
    def _import_issue(self, issue: Issue, plan: CardPlan, result: SyncResult) -> None:
        fields = self.issue_fields(issue)
        tab = fields.tab or self.default_tab or next(iter(tab_folders(self.board)), "features")
        category = category_for_tab(self.board, tab)
        # An issue nobody has labelled with a lane is untriaged work, whatever `status_from_issue`
        # had to guess for the merge: it lands in the inbox rather than in Ready.
        labelled = any(str(l).startswith(STATUS_LABEL) for l in issue.labels)
        status = fields.status if (labelled or issue.state == "closed") else IMPORT_STATUS
        if status not in B.WORK_STATUS_FOLDER:
            status = IMPORT_STATUS
        existing = self.board.cards(include_private=False)
        card = B.new_card("work", fields.title or f"{self.repo}#{issue.number}", status,
                          rank=self.board.next_rank([c for c in existing if c.status == status]),
                          labels=list(fields.labels) or None,
                          source=f"{self.repo}#{issue.number}")
        card.body = render_body(fields.title or f"{self.repo}#{issue.number}", fields.prose, fields.tasks)
        links = dict(card.front.get("links") or {})
        links["github"] = f"{self.repo}#{issue.number}"
        card.set("links", links)
        if fields.assignee:
            card.set("assignee", fields.assignee)
        path = B.write_new_card(self.board, card, category)
        self.board.append_thread(card.id, f"- ✦ imported from {self.repo}#{issue.number}",
                                 author=SYNC_AUTHOR, kind="event", via="github", repo=self.repo)
        entry = self.state.card(card.id)
        entry.update({"number": issue.number, "updated_at": issue.updated_at, "etag": issue.etag,
                      "base": self.card_fields(card).to_json(), "card_hash": B.file_hash(path),
                      "current": True})
        plan.card_id = card.id
        result.imported += 1
        # The issue gains the hidden id marker, so a human edit cannot orphan it.
        updated = self.provider.update_issue(
            issue.number, body=issue_body_with_footer(strip_footer(issue.body), card.id or ""))
        if updated is not None:
            entry["updated_at"] = updated.updated_at
            entry["etag"] = updated.etag
        self._pull_comments(card, issue, entry, plan, result)


# -------------------------------------------------------------------------- helpers

def _linked_number(card: B.Card, repo: str) -> int | None:
    links = card.front.get("links")
    value = links.get("github") if isinstance(links, dict) else None
    if not value:
        return None
    match = re.match(r"^(?P<repo>[^#\s]+)?#(?P<number>\d+)$", str(value).strip())
    if not match:
        return None
    if match.group("repo") and repo and match.group("repo") != repo:
        return None
    return int(match.group("number"))


def _plan_links(board: B.Board, card: B.Card) -> list[str]:
    """The ids of the card's *shared* plan cards.  Ids only — a plan's words never leave."""
    links = card.front.get("links")
    values = links.get("plans") if isinstance(links, dict) else None
    out = []
    for value in values or []:
        plan = board.card_by_id(str(value))
        if plan is None or plan.private or _under_private(board, plan):
            continue
        out.append(str(value).upper())
    return out


def _comment_text(comment: Comment) -> str:
    body = normalize(comment.body)
    return body if body else "(empty comment)"


def _tasks_text(tasks) -> str:
    return "; ".join(f"[{'x' if done else ' '}] {text}" for _, done, text in tasks or ())


def _short(value, limit: int = 200) -> str:
    text = " ".join(str(value if value is not None else "").split())
    return text[:limit] + ("…" if len(text) > limit else "")


def _clean(exc: Exception) -> str:
    return logs.scrub(f"{type(exc).__name__}: {exc}")


# ----------------------------------------------------------------------- entry point

def sync(board: B.Board, provider: ForgeProvider, *, dry_run: bool = False,
         confirm_bulk: bool = False, **kwargs) -> dict:
    """Plan or run one sync and return the protocol-shaped result (`docs/GITHUB-SYNC.md`)."""
    engine = ForgeSync(board, provider, **kwargs)
    result = engine.plan() if dry_run else engine.run(confirm_bulk=confirm_bulk)
    return result.to_dict()

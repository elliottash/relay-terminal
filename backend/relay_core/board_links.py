# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Board's link index (#EE42, `docs/PROJECT-BOARD-DESIGN.md` §4).

Links are **stored once, forward** — in a card's front matter (`parent`, `blocked_by`,
`duplicate_of`, `discovered_from`, `supersedes`, `links.*`, `server`), in the addresses its body
and thread mention (`#ID`, `skill:<id>`, `case:c-…`, `run:r-…`, `path@sha`), and in the case
ledger's rows — and the reverse is **computed**.  `build(board)` reads every one of those from the
files and returns a `LinkIndex`; it is rebuilt whole on every call, never patched per event and
never written anywhere (Logseq #7362; org-roam's "the notes still work without it").  Its edges
are `(from, relation, to, where)`, and the relation is the name of the field the edge was read
from, so the vocabulary is the fixed set of keys and nothing else.

The card-to-card part is `board.card_link_edges` / `board.CARD_LINK_FIELDS`, the same table
`links_index` (what `board_read`'s `reverse` is built from) and `check_links` read, so the three
cannot disagree about which fields are links.

The one stored reverse is GitHub's shape: when a board write adds a cross-reference to a card,
the target's thread gets one append-only `mentioned in #X · date · who` entry
(`note_mentions`).  It is an `event` entry carrying `mention=<source id>`, which is how it is
deduplicated per source→target pair and how it is kept from counting as a reference itself.  If
a line and the index disagree, the index wins: it was computed from the forward links.

Prose inside code spans and fenced blocks is not scanned, as on GitHub, so a card that quotes
an example address (`#K7Q2`, `skill:x`) does not link to it.  `event` thread entries are not
scanned either: they echo writes whose links are already in the front matter.
"""
from __future__ import annotations

import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

from . import board as B
from . import cases as CASES

_ID = r"[0-9A-HJKMNP-TV-Z]{4}"
#: `#K7Q2`: four characters of the id alphabet, not part of a URL fragment, a colour or a word.
CARD_RE = re.compile(rf"(?<![\w#&/.\-])#({_ID})(?![\w\-])")
SKILL_RE = re.compile(r"(?<![\w/.\-])skill:([a-z0-9](?:[a-z0-9._\-]*[a-z0-9])?)(@sha256:[0-9a-f]{8,64})?")
CASE_RE = re.compile(r"(?<![\w/.\-])case:(c-[0-9a-f]{4,32})(?![\w\-])")
RUN_RE = re.compile(r"(?<![\w/.\-])run:(r-[0-9a-f]{4,32})(?:#([^\s)`'\"\]>]+))?")
#: `src/BoardPane.cpp:3901@e5a0bc03`: a file with an extension, an optional line, a commit.
PATH_SHA_RE = re.compile(r"(?<![\w/@.\-])((?:[\w.\-]+/)*[\w\-][\w.\-]*\.[A-Za-z0-9]+)(?::(\d+))?@([0-9a-f]{7,40})(?![\w\-])")
SHA_RE = re.compile(r"^[0-9a-f]{7,40}$")
_FENCE_RE = re.compile(r"^(```|~~~).*?^\1[^\n]*$", re.M | re.S)
_CODE_RE = re.compile(r"(`+)(?:(?!\1).)+?\1", re.S)

#: The name each forward relation's reverse goes by, for a page that draws it.  The card-to-card
#: names are `board.CARD_LINK_FIELDS`'s; `server` differs by source (a card builds a skill, a
#: case row was served by one).
INVERSE = {**dict(B.CARD_LINK_FIELDS), "mention": "mentioned_in", "links.commits": "commit_of",
           "links.evidence": "evidence_of", "links.plans": "plan_of", "card": "cases"}

#: What the filter box accepts as a link term (§4.3): `links:<address>` is anything pointing at
#: the address; a bare `skill:` / `case:` / `run:` address is the cards linked to it either way.
FILTER_PREFIXES = ("links:", "skill:", "case:", "run:")

GIT_TIMEOUT = 10


def _prose(text: str) -> str:
    """`text` with fenced blocks and inline code blanked out."""
    text = _FENCE_RE.sub(" ", text or "")
    return _CODE_RE.sub(" ", text)


def kind_of(address: str) -> str:
    if address.startswith("#"):
        return "card"
    for prefix in ("skill", "case", "run"):
        if address.startswith(prefix + ":"):
            return prefix
    if "@" in address:
        return "path@sha"
    if SHA_RE.match(address):
        return "commit"
    return "file"


def normalize(address) -> str | None:
    """One address in its canonical form, or None when it is not one: `#k7q2`, `K7Q2` → `#K7Q2`;
    `skill:x@sha256:…` → `skill:x`; `run:r-1#fig.pdf` → `run:r-1`; a path keeps its line out."""
    if not isinstance(address, str):
        return None
    text = address.strip()
    if text.startswith("links:"):
        text = text[len("links:"):]
    bare = text.lstrip("#").upper()
    if B.ID_RE.match(bare) and any(c.isalpha() for c in bare) and (text.startswith("#") or len(text) == 4):
        return "#" + bare
    for regex, prefix in ((SKILL_RE, "skill:"), (CASE_RE, "case:"), (RUN_RE, "run:")):
        match = regex.fullmatch(text) if prefix != "run:" else regex.match(text)
        if match and (prefix != "run:" or match.end() == len(text)):
            return prefix + match.group(1)
    match = PATH_SHA_RE.fullmatch(text)
    if match:
        return f"{match.group(1)}@{match.group(3)}"
    if SHA_RE.match(text.lower()):
        return text.lower()
    if text and not text.startswith(("http://", "https://")) and " " not in text:
        return text
    return None


def mentions(text: str) -> list[tuple[str, dict]]:
    """Every address the prose of `text` mentions, in order, each once, with what the address
    form carried beside it (a pinned skill version, a run artifact, a line)."""
    prose = _prose(text)
    found: dict[str, dict] = {}
    for match in CARD_RE.finditer(prose):
        if any(c.isalpha() for c in match.group(1)):
            found.setdefault("#" + match.group(1), {})
    for match in SKILL_RE.finditer(prose):
        found.setdefault("skill:" + match.group(1),
                         {"version": match.group(2)[1:]} if match.group(2) else {})
    for match in CASE_RE.finditer(prose):
        found.setdefault("case:" + match.group(1), {})
    for match in RUN_RE.finditer(prose):
        found.setdefault("run:" + match.group(1), {"artifact": match.group(2)} if match.group(2) else {})
    for match in PATH_SHA_RE.finditer(prose):
        found.setdefault(f"{match.group(1)}@{match.group(3)}",
                         {"line": int(match.group(2))} if match.group(2) else {})
    return list(found.items())


def card_refs(text: str) -> set[str]:
    """The card ids (no `#`) the prose of `text` mentions."""
    return {address[1:] for address, _ in mentions(text) if address.startswith("#")}


def _scanned(entry: B.ThreadEntry) -> bool:
    """Whether a thread entry's text counts as references: not an `event` (an echo of a write
    whose links are in the front matter) and not a mentioned-in line (a reverse, not a link)."""
    return entry.attrs.get("kind") != "event" and not entry.attrs.get("mention")


def _thread(board: B.Board, card: B.Card) -> list[B.ThreadEntry]:
    try:
        return board.thread(card.id or "", card.private)
    except (OSError, B.BoardError):
        return []


def card_edges(card: B.Card, thread: Iterable[B.ThreadEntry] = ()) -> list[dict]:
    """Every forward edge one card stores: front matter first, then its body, then its thread."""
    me = "#" + (card.id or "")
    edges: list[dict] = []
    seen: set[tuple] = set()

    def add(relation: str, to: str, where: str, **extra) -> None:
        if not to or to == me:
            return
        key = (relation, to, where.split(" ")[0])
        if key in seen:
            return
        seen.add(key)
        edges.append({"from": me, "relation": relation, "to": to, "where": where, **extra})

    for field, other in B.card_link_edges(card):
        # A value that is not an id (a path in an old board's `links.related`) stays itself,
        # so it resolves — or dangles — as what it is.
        add(field, "#" + other if B.ID_RE.match(other) else other, "front matter")
    links = B.card_links(card)
    for sha in B._link_ids(links.get("commits")):
        if SHA_RE.match(sha.lower()):
            add("links.commits", sha.lower(), "front matter")
    for key in ("evidence", "plans"):
        values = links.get(key)
        for value in values if isinstance(values, list) else [values] if values else []:
            address = normalize(str(value))
            if address:
                add("links." + key, address, "front matter")
    server = card.front.get("server")
    if isinstance(server, str) and server.strip():
        server = server.strip()
        add("server", server if "/" in server else "skill:" + server.removeprefix("skill:"),
            "front matter")
    for address, extra in mentions(card.body):
        add("mention", address, "body", **extra)
    for entry in thread:
        if _scanned(entry):
            for address, extra in mentions(entry.text):
                add("mention", address, f"thread {entry.entry_id}", **extra)
    return edges


def refs_of_card(board: B.Board, card: B.Card | None) -> set[str]:
    """The card ids `card` links to, by any field or mention: what a write compares before and
    after to find the cross-references it added."""
    if card is None:
        return set()
    return {edge["to"][1:] for edge in card_edges(card, _thread(board, card))
            if edge["to"].startswith("#")}


class LinkIndex:
    """Every edge on one board, both ways, computed from the files (see the module doc)."""

    def __init__(self, board: B.Board, cards: list[B.Card], edges: list[dict], ledger: list[dict]):
        self.board = board
        self.cards = {card.id: card for card in cards if card.id}
        self.edges = edges
        self.ledger_ids = {row.get("id") for row in ledger if row.get("id")}
        self.ledger_servers = {str(row.get("server")) for row in ledger if row.get("server")}
        self.forward: dict[str, list[dict]] = {}
        self.reverse: dict[str, list[dict]] = {}
        for edge in edges:
            self.forward.setdefault(edge["from"], []).append(edge)
            self.reverse.setdefault(edge["to"], []).append(edge)
        self._skills: set[str] | None = None
        self._runs: set[str] | None = None
        self._git: dict[str, bool] = {}

    # ---- resolution -------------------------------------------------------------------------
    def _skill_names(self) -> set[str]:
        if self._skills is None:
            names = set(self.ledger_servers)
            try:
                from . import skills as S
                index = S.SkillIndex.load(S.default_directories(self.board.repo), (), defaults=True)
                names |= {skill.name for skill in getattr(index, "skills", [])}
            except Exception:                              # pragma: no cover - unreadable skills
                pass
            self._skills = names
        return self._skills

    def _run_ids(self) -> set[str] | None:
        """The runs ledger's ids (#FVVY), or None while the board has none: until then a `run:`
        address is neither found nor dangling."""
        if self._runs is None:
            path = Path(self.board.root) / "runs.jsonl"
            if not path.exists():
                return None
            import json
            ids = set()
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                try:
                    row = json.loads(line)
                except ValueError:
                    continue
                if isinstance(row, dict) and row.get("id"):
                    ids.add(str(row["id"]))
            self._runs = ids
        return self._runs

    def _git_objects(self, addresses: Iterable[str]) -> None:
        """Resolve commits and `path@sha` addresses with one `git cat-file --batch-check`."""
        wanted = []
        for address in addresses:
            if address in self._git:
                continue
            kind = kind_of(address)
            if kind == "commit":
                wanted.append((address, f"{address}^{{commit}}"))
            elif kind == "path@sha":
                path, _, sha = address.rpartition("@")
                wanted.append((address, f"{sha}:{path}"))
        if not wanted:
            return
        try:
            done = subprocess.run(["git", "-C", str(self.board.repo), "cat-file", "--batch-check"],
                                  input="\n".join(spec for _, spec in wanted) + "\n",
                                  capture_output=True, text=True, timeout=GIT_TIMEOUT)
            lines = done.stdout.splitlines() if done.returncode == 0 else []
        except (OSError, subprocess.SubprocessError):
            lines = []
        if len(lines) != len(wanted):
            return                                # outside a repository: unknown, not dangling
        for (address, _), line in zip(wanted, lines):
            self._git[address] = not line.rstrip().endswith("missing")

    def exists(self, address: str) -> bool | None:
        """True when the address names something, False when it names nothing, None when this
        board cannot tell (no runs ledger yet, no git)."""
        kind = kind_of(address)
        if kind == "card":
            return address[1:] in self.cards
        if kind == "case":
            return address[len("case:"):] in self.ledger_ids
        if kind == "skill":
            return address[len("skill:"):] in self._skill_names()
        if kind == "run":
            runs = self._run_ids()
            return None if runs is None else address[len("run:"):] in runs
        if kind in ("commit", "path@sha"):
            self._git_objects([address])
            return self._git.get(address)
        return (Path(self.board.repo) / address).exists()

    # ---- answers ----------------------------------------------------------------------------
    def _describe(self, address: str) -> dict:
        card = self.cards.get(address[1:]) if address.startswith("#") else None
        return {"title": card.title, "status": card.status} if card is not None else {}

    def query(self, address: str) -> dict:
        """`board_links`'s answer for one address: what it links to and what links to it."""
        self._git_objects([e["to"] for e in self.forward.get(address, [])] + [address])
        forward = [{**{k: v for k, v in edge.items() if k != "from"}, "kind": kind_of(edge["to"]),
                    "exists": self.exists(edge["to"]), **self._describe(edge["to"])}
                   for edge in self.forward.get(address, [])]
        reverse = []
        for edge in self.reverse.get(address, []):
            name = INVERSE.get(edge["relation"], edge["relation"] + "_of")
            if edge["relation"] == "server":
                name = "cases" if edge["from"].startswith("case:") else "built_by"
            reverse.append({**{k: v for k, v in edge.items() if k != "to"}, "name": name,
                            "kind": kind_of(edge["from"]), **self._describe(edge["from"])})
        return {"address": address, "kind": kind_of(address), "exists": self.exists(address),
                **self._describe(address), "forward": forward, "reverse": reverse}

    def dangling(self) -> list[dict]:
        """Every edge whose target resolves to nothing (`exists` False; unknown is not dangling)."""
        self._git_objects(edge["to"] for edge in self.edges)
        return [dict(edge) for edge in self.edges if self.exists(edge["to"]) is False]

    def matching(self, term: str) -> set[str] | None:
        """The card ids a filter term selects (§4.3), or None when the term is not a link term.
        `links:<address>`: cards with any edge to the address.  `skill:x`, `case:c-…`, `run:r-…`:
        cards linked to that address in either direction — the cards building or mentioning a
        skill, and those its case rows name."""
        if not term.startswith(FILTER_PREFIXES):
            return None
        address = normalize(term)
        if address is None:
            return set()
        if address.startswith("#") and not term.startswith("links:"):
            return None
        ids = {edge["from"][1:] for edge in self.reverse.get(address, []) if edge["from"].startswith("#")}
        if not term.startswith("links:"):
            ids |= {edge["to"][1:] for edge in self.forward.get(address, []) if edge["to"].startswith("#")}
            if address.startswith("skill:"):      # the cards its case rows name
                for case in self.reverse.get(address, []):
                    ids |= {e["to"][1:] for e in self.forward.get(case["from"], [])
                            if e["to"].startswith("#")}
        return {i for i in ids if i in self.cards}


def build(board: B.Board, cards: list[B.Card] | None = None) -> LinkIndex:
    """The whole index, from the files as they are now."""
    cards = board.cards() if cards is None else cards
    edges: list[dict] = []
    for card in cards:
        edges.extend(card_edges(card, _thread(board, card)))
    ledger = CASES.read(board.root)
    for row in ledger:
        case = row.get("id")
        if not case:
            continue
        me = "case:" + str(case)
        card = str(row.get("card") or "").lstrip("#").upper()
        if B.ID_RE.match(card):
            edges.append({"from": me, "relation": "card", "to": "#" + card, "where": "cases.jsonl"})
        server = str(row.get("server") or "")
        if server and not server.startswith("card:"):
            edges.append({"from": me, "relation": "server",
                          "to": server if "/" in server else "skill:" + server,
                          "where": "cases.jsonl"})
    return LinkIndex(board, cards, edges, ledger)


# ---- the mentioned-in line ------------------------------------------------------------------
def mention_text(source: str, actor: str, when: datetime | None = None) -> str:
    day = (when or datetime.now(timezone.utc)).strftime("%Y-%m-%d")
    return f"mentioned in #{source} · {day} · {actor}"


def note_mentions(board: B.Board, source: B.Card, targets: Iterable[str], actor: str,
                  when: datetime | None = None, **attrs) -> list[str]:
    """Append one `mentioned in #<source>` entry to each target card's thread that has none for
    this source yet, and return the ids that got one.  The check runs under the threads
    directory's lock (`board.append_to_thread`), so two writers racing on the same pair still
    leave one line.  Targets that are not cards on this board, and the source itself, get none."""
    src = source.id or ""
    added: list[str] = []
    for target in sorted(set(targets)):
        if not target or target == src:
            continue
        card = board.card_by_id(target)
        if card is None:
            continue
        path = board.thread_path(card.id, card.private)

        def add(body: bytes, target=target):
            entries = B.parse_thread(body.decode("utf-8", "replace"))
            if any(e.attrs.get("mention") == src for e in entries):
                return "", False
            ids = [e.entry_id for e in entries]
            entry = B.ThreadEntry(B.next_entry_id(max(ids) if ids else None, when),
                                  {"author": actor, "kind": "event", "mention": src,
                                   **{k: str(v) for k, v in attrs.items() if v is not None}},
                                  mention_text(src, actor, when))
            return entry.render(), True

        if B.append_to_thread(path, add):
            added.append(target)
    return added

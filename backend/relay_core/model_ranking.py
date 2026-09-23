# SPDX-License-Identifier: AGPL-3.0-or-later
"""`model-ranking.md`, the file the default priority lists are built from (card #MDL1).

The owner asked for "a structured ranking MD or YAML in the repo i can review and edit", and chose
Markdown. So the numbers that decide the defaults live in `model-ranking.md` beside this module —
four pipe tables — and this is the whole of the code that reads them. It depends on nothing: no
YAML, no Markdown library, and (at import time) nothing else in `relay_core`, so `presets` can
import it while `check()` imports `presets` back.

* **Providers** (`provider | kind | order`) — who is offered, and in which order ties break.
* **Models** (`name | classes | score | notes`) — which class each model is a default for, and how
  good the owner says it is.
* **Provider picks** (`provider | high | main | flash | lite`) — a provider whose defaults differ
  from the shared Models rows, by model *name*. A blank cell follows the Models table. Added
  2026-09-21; only `openrouter` has a row so far.
* **Levels** (`name | high | main | flash | lite | notes`) — the reasoning level a model starts at
  in each class, in that model's provider's own words ("i want the effort options in relay to be
  determined by the model … so xhigh shows up for codex"). A blank cell is the rule in code: high
  is the model's top level, main the provider's own default, flash and lite its lowest.

`presets.INTELLIGENCE` is a view over `score(name)` and `presets.tier_list_defaults` does its
ranking through `classes()`, `provider_order()`, `pick()` and `level()`;
`model-ranking.md`'s own header says what the rules are and how to edit the tables.

The last two tables are **optional**: a file without them parses, and every rule they carry stays
where it was before they existed. That is what lets the file be edited a table at a time.

What raises and what only reports. A row that cannot be read at all — the wrong number of cells, a
score that is not a number, a `kind` outside the five, a table whose first column is not the one
named above — is a `ValueError` naming the line, because a half-parsed table would silently drop a
model. Everything else is `check()`'s: an unknown class word (in a Models row or as a column of
either new table), a provider the worker has never heard of, a preset with no row, a catalog model
with no row, a duplicate name, two providers sharing an `order`, a pick naming a model its provider
does not serve, and a level cell naming a level the model does not take. So a typo in a class word
gives a readable report from the test rather than a worker that will not start — and the file stays
editable by hand, half-finished, without taking the worker or the model picker down with it.
"""
from __future__ import annotations

import re
import threading
from dataclasses import dataclass, field
from pathlib import Path

# The classes a model can be a default for. "local" is not one of them: the local class is the
# model servers on this machine, in the order they were saved, and belongs to no provider.
CLASSES = ("high", "main", "flash", "lite")
# The words a `kind` cell may hold: how a provider is reached. This is a **set**, and the order it
# is written in means nothing — `Ranking.kinds()` is what answers "in which order", and it answers
# out of the file. It used to be both, spelled as design rule 2.2 (a plan, then a harness, then the
# first-party API, then the router, then Relay Free), and on 2026-09-21 the owner re-ordered the
# table to put the harnesses first. A preference order written in two places is a preference order
# that goes quietly wrong in one of them, and the file is the copy he edits, so the file wins.
KINDS = ("plan", "harness", "api", "router", "free")
# Where a provider with no row of its own sorts: after every one that has one. A custom provider
# the user added by hand is the case that matters. `check()` says so if a row ever reaches it,
# because a file that numbered a band this high would make "no row" sort *before* a real one.
UNKNOWN_PROVIDER_ORDER = 500

DEFAULT_PATH = Path(__file__).resolve().parent / "model-ranking.md"

_PROVIDER_COLUMNS = ("provider", "kind", "order")
_MODEL_COLUMNS = ("name", "classes", "score", "notes")
# The two tables added on 2026-09-21 share one shape: a key column, then one column per class in
# whatever order the file writes them, then an optional `notes` column nobody reads. Only the key
# column's name is fixed, so a class may be left out of a table entirely and the columns may be
# re-ordered — the header is what says which class a cell belongs to.
_PICK_KEY = "provider"
_LEVEL_KEY = "name"
_NOTES_COLUMN = "notes"
_SEPARATOR = re.compile(r":?-{2,}:?")
# "a default for nothing" and "nobody has scored it", in the spellings a person might reach for.
_BLANK = ("", "-", "--", "—", "–", "none", "n/a")


@dataclass(frozen=True)
class Provider:
    """One row of the Providers table."""
    id: str
    kind: str
    order: int


@dataclass(frozen=True)
class Model:
    """One row of the Models table, keyed by the one name the model has (design rule 1)."""
    name: str
    classes: tuple[str, ...] = ()
    score: int | None = None
    notes: str = ""


@dataclass(frozen=True)
class Ranking:
    """A parsed `model-ranking.md`."""
    providers: dict[str, Provider] = field(default_factory=dict)
    models: dict[str, Model] = field(default_factory=dict)
    path: str = ""
    # Names that appeared twice, in the order they were seen. A dict cannot hold both, so the
    # second wins and `check()` says so rather than the file quietly meaning something else.
    duplicate_providers: tuple[str, ...] = ()
    duplicate_models: tuple[str, ...] = ()
    # The Provider picks table: {provider: {class: model name}}, blank cells left out.
    picks: dict[str, dict[str, str]] = field(default_factory=dict)
    # The Levels table: {model name: {class: level}}, blank cells left out.
    levels: dict[str, dict[str, str]] = field(default_factory=dict)
    # The class columns each of those two tables was written with, and their repeated keys — both
    # for `check()`, which is the only thing that cares that a column said "fast".
    pick_columns: tuple[str, ...] = ()
    level_columns: tuple[str, ...] = ()
    duplicate_picks: tuple[str, ...] = ()
    duplicate_levels: tuple[str, ...] = ()

    # ----- what the rest of the worker asks ------------------------------------------------
    def score(self, name) -> int | None:
        """The intelligence number for a model name, or None — for a name with a blank score and
        for a name with no row at all, which are the same thing to a sort: last."""
        row = self.models.get(name) if isinstance(name, str) else None
        return row.score if row is not None else None

    def classes(self, name) -> tuple[str, ...]:
        """The classes this model is a default candidate for, in CLASSES order; () for a model
        that is a default for nothing and for a name with no row."""
        row = self.models.get(name) if isinstance(name, str) else None
        if row is None:
            return ()
        return tuple(c for c in CLASSES if c in row.classes)

    def level(self, name, klass) -> str | None:
        """The reasoning level this model starts at in a class, in its provider's own words, or
        None for a blank cell, a model with no Levels row and a name that is not a string.

        None means "the rule in code": the model's top level for `high`, the provider's own
        default for `main`, its lowest for `flash` and `lite`. The word here is whatever the
        provider says — `xhigh` through codex, `max` through Kimi — and the caller maps it onto
        the levels the model it is about to run actually lists (`presets.nearest_effort`), because
        one name can be served by two providers with two vocabularies.
        """
        row = self.levels.get(name) if isinstance(name, str) else None
        return row.get(klass) if row is not None else None

    def pick(self, provider, klass) -> str | None:
        """The model *name* this provider puts in a class when its defaults differ from the shared
        Models rows, or None for a blank cell and for a provider with no Provider picks row.

        None means "follow the Models table", which is what every provider but `openrouter` does
        today. The answer is a name, not an id: which id serves it is the provider's business
        (`presets.provider_model_id`).
        """
        row = self.picks.get(provider) if isinstance(provider, str) else None
        return row.get(klass) if row is not None else None

    def provider_order(self, preset_id) -> int:
        """The tie-break for a provider, lower first; UNKNOWN_PROVIDER_ORDER for one with no row
        (a custom provider, a model server on this machine)."""
        row = self.providers.get(preset_id) if isinstance(preset_id, str) else None
        return row.order if row is not None else UNKNOWN_PROVIDER_ORDER

    def provider_kind(self, preset_id) -> str:
        """`plan` | `harness` | `api` | `router` | `free`, or "" for a provider with no row."""
        row = self.providers.get(preset_id) if isinstance(preset_id, str) else None
        return row.kind if row is not None else ""

    def kinds(self) -> tuple[str, ...]:
        """The kinds present, **in the file's own order**: each band by its lowest `order`.

        This is the one answer to "which kind of access does Relay prefer", and it is derived
        rather than declared, so re-numbering the table is all it takes to change it — which is
        what the table is for. A band's lowest row is what stands for it, because that is the row
        that wins a tie against another band. Ties between bands (two kinds whose lowest order is
        the same number, which `check()` reports) fall back to the spelling in KINDS, so the
        answer is always stable.
        """
        lowest: dict[str, int] = {}
        for row in self.providers.values():
            if row.order < lowest.get(row.kind, row.order + 1):
                lowest[row.kind] = row.order
        return tuple(sorted(lowest, key=lambda kind: (lowest[kind], KINDS.index(kind))))

    # ----- is the file still true? -----------------------------------------------------------
    def check(self) -> list[str]:
        """Everything wrong with this file that parsing let through, as sentences a person can act
        on. Empty means the file agrees with the worker's own tables.

        `presets` is imported here rather than at the top: it imports this module.
        """
        from . import presets as P

        problems: list[str] = []
        for name in self.duplicate_providers:
            problems.append(f"provider {name!r} has more than one row; only the last one counts")
        for name in self.duplicate_models:
            problems.append(f"model {name!r} has more than one row; only the last one counts")
        for name in self.duplicate_picks:
            problems.append(f"provider {name!r} has more than one Provider picks row; only the "
                            f"last one counts")
        for name in self.duplicate_levels:
            problems.append(f"model {name!r} has more than one Levels row; only the last one counts")

        # A column of either per-class table that is not one of the four classes. The cells under
        # it are parsed and then read by nobody, which looks exactly like a rule that is not being
        # applied — so it is said out loud rather than dropped in silence.
        for heading, columns in (("Provider picks", self.pick_columns), ("Levels", self.level_columns)):
            for word in columns:
                if word not in CLASSES:
                    problems.append(f"the {heading} table has a column {word!r}, which is not one "
                                    f"of {', '.join(CLASSES)}; nothing reads it")

        # `order` is the tie-break between two providers serving the same model at the same score,
        # so two providers at the same number decide nothing: the sort falls through to the name
        # and the file reads as a ruling it is not making. Parsing lets it through — a repeated
        # number is a perfectly well-formed row, and it is usually a slip made while re-numbering
        # a band — so it is reported here rather than raised, and the worker starts either way.
        by_order: dict[int, list[str]] = {}
        for preset_id, row in self.providers.items():
            by_order.setdefault(row.order, []).append(preset_id)
        for order, shared in sorted(by_order.items()):
            if len(shared) > 1:
                problems.append(f"providers {', '.join(sorted(shared))} all have order {order}; "
                                f"`order` is the tie-break, so two providers at the same number "
                                f"break the tie by name rather than by the file")
        # A provider with no row at all sorts at UNKNOWN_PROVIDER_ORDER, which only means "last"
        # for as long as every row in the file is below it.
        for preset_id in sorted(self.providers):
            if self.providers[preset_id].order >= UNKNOWN_PROVIDER_ORDER:
                problems.append(f"provider {preset_id!r} has order "
                                f"{self.providers[preset_id].order}, at or past the "
                                f"{UNKNOWN_PROVIDER_ORDER} a provider with no row sorts at; a row "
                                f"in the file has to come before one that is not in it")

        for name, row in sorted(self.models.items()):
            for word in row.classes:
                if word not in CLASSES:
                    problems.append(f"model {name!r} has an unknown class {word!r} "
                                    f"(one of {', '.join(CLASSES)}, or '-' for none)")

        # Both new tables are keyed by something the two old ones already name, and a key neither
        # of them holds is a row that decides nothing — a renamed model, or a provider spelled the
        # way its label reads rather than the way its id does.
        for name in sorted(self.levels):
            if name not in self.models:
                problems.append(f"the Levels table has a row for {name!r}, which is not a name in "
                                f"the Models table; nothing reads it")
        for provider in sorted(self.picks):
            if provider not in self.providers:
                problems.append(f"the Provider picks table has a row for {provider!r}, which is "
                                f"not a provider in the Providers table; nothing reads it")
            for cls, name in sorted(self.picks[provider].items()):
                if name not in self.models:
                    problems.append(f"{provider}'s {cls} pick is {name!r}, which is not a name in "
                                    f"the Models table")
                elif provider in P.PRESETS and P.provider_model_id(provider, name) is None:
                    problems.append(f"{provider}'s {cls} pick is {name!r}, which is not a model "
                                    f"{provider} serves; that class falls back to the Models table")

        # A Levels cell is keyed by name, and a name can be served by two providers with two
        # vocabularies — `gpt-6-luna` takes `max` through codex and stops at `xhigh` through the
        # OpenAI API — so a cell is wrong only when *no* provider that serves the model has that
        # level. Where one does, `presets.nearest_effort` maps the cell onto whichever provider is
        # about to run it, which is the whole reason the table can be keyed by name at all.
        for name in sorted(self.levels):
            if name not in self.models:
                continue                    # already reported above
            offered = set(P.levels_for_name(name))
            for guest_id, models in _GUEST_MODEL_IDS.items():
                if any(P.model_name(f"guest:{guest_id}", model) == name for model in models):
                    offered |= set(_GUEST_LEVELS.get(guest_id, ()))
            if not offered:
                continue                    # a model with no knob anywhere ignores its row
            for cls, level in sorted(self.levels[name].items()):
                if level not in offered:
                    problems.append(f"{name}'s {cls} level is {level!r}, which no provider that "
                                    f"serves it offers ({', '.join(sorted(offered))})")

        guest_ids = {f"guest:{guest}" for guest in ("claude", "codex")}
        for preset_id in sorted(self.providers):
            if preset_id not in P.PRESETS and preset_id not in guest_ids:
                problems.append(f"provider {preset_id!r} is not a preset the worker has "
                                f"(presets.PRESETS) and is not a guest harness")
        for preset_id in P.PRESETS:
            if preset_id not in self.providers:
                problems.append(f"preset {preset_id!r} has no row in the Providers table")
        for preset_id in sorted(guest_ids):
            if preset_id not in self.providers:
                problems.append(f"guest {preset_id!r} has no row in the Providers table")

        for preset_id, rows in P.MODEL_CATALOG.items():
            for row in rows:
                name = P._ranking_name(preset_id, row["id"])
                if name not in self.models:
                    problems.append(f"model {name!r} ({preset_id} {row['id']!r}) has no row in the "
                                    f"Models table")
        for guest_id, models in _GUEST_MODEL_IDS.items():
            for model in models:
                name = P.model_name(f"guest:{guest_id}", model)
                if name not in self.models:
                    problems.append(f"model {name!r} (guest:{guest_id} {model!r}) has no row in the "
                                    f"Models table")
        return problems


# What the two guest CLIs name, for `check()` alone: the claude adapter's `--model` aliases and the
# four codex-cli lists first (`guest_harness_provider._CODEX_FALLBACK_MODELS`). Named here rather
# than imported, because importing the guest stack to read a Markdown file is a heavy way to spell
# a list of eight strings, and the test that runs `check()` is what keeps the two in step.
_GUEST_MODEL_IDS = {"claude": ("fable", "opus", "sonnet", "haiku"),
                    "codex": ("gpt-6-astra", "gpt-6-sol", "gpt-5.6-terra", "gpt-6-luna")}
# And the level words each CLI names (`guest_harness_claude.EFFORTS`,
# `guest_harness_codex.EFFORTS`), for the same reason and the same check: a Levels cell saying
# `xhigh` for `claude-opus-5.5` is right because Claude Code has an xhigh, even though Anthropic's
# own compat layer has no knob at all. Codex narrows its list per model; the union is what a cell
# is checked against, so a level one codex model does not have is the CLI's to refuse.
_GUEST_LEVELS = {"claude": ("low", "medium", "high", "xhigh", "max"),
                 "codex": ("low", "medium", "high", "xhigh", "max", "ultra")}


# ----- parsing ---------------------------------------------------------------------------------
def _cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def _is_separator(cells: list[str]) -> bool:
    return bool(cells) and all(cell and _SEPARATOR.fullmatch(cell) for cell in cells)


def _scan(text: str, heading: str) -> list[tuple[int, list[str]]] | None:
    """The rows of the one pipe table under `## <heading>`, as (line number, cells), separator
    lines dropped and the header still first. None when the file has no such heading at all, and
    [] when the heading is there with no table under it."""
    rows: list[tuple[int, list[str]]] = []
    found = False
    inside = False
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if line.startswith("#"):
            inside = line.lstrip("#").strip().lower() == heading.lower()
            found = found or inside
            continue
        if not inside or not line.startswith("|"):
            continue
        cells = _cells(line)
        if _is_separator(cells):
            continue
        rows.append((lineno, cells))
    return rows if found else None


def _table(text: str, heading: str, columns: tuple[str, ...], where: str) -> list[tuple[int, list[str]]]:
    """The rows of one of the two required tables, header and separator dropped. Extra whitespace
    anywhere is fine; a missing table, or a header naming something else, is a ValueError."""
    rows = _scan(text, heading)
    if rows is None:
        raise ValueError(f"{where}: no '## {heading}' heading")
    if not rows:
        raise ValueError(f"{where}: the '## {heading}' section has no table")
    lineno, header = rows[0]
    if tuple(cell.lower() for cell in header) != columns:
        raise ValueError(f"{where}:{lineno}: the {heading} table's header is "
                         f"{' | '.join(header)}; expected {' | '.join(columns)}")
    return rows[1:]


def _class_table(text: str, heading: str, key: str, where: str) \
        -> tuple[dict[str, dict[str, str]], tuple[str, ...], tuple[str, ...]]:
    """One of the two per-class tables added on 2026-09-21, as
    ``({key: {class: cell}}, the class columns, the keys that appeared twice)``.

    Both are optional: with no such heading this is ``({}, (), ())`` and every rule the table
    carries stays where it was in code, which is what lets the owner add one table at a time. The
    columns are read off the header rather than fixed, so a class may be left out and the order
    may be anything; a trailing ``notes`` column is dropped, and a blank cell (or "-", or "none")
    is left out of the row rather than stored as "". A first column that is not ``key`` is a
    ValueError: it means this table is not the table it says it is.
    """
    rows = _scan(text, heading)
    if not rows:
        return {}, (), ()
    lineno, header = rows[0]
    if not header or header[0].strip().lower() != key:
        first = header[0] if header else ""
        raise ValueError(f"{where}:{lineno}: the {heading} table's first column is {first!r}; "
                         f"expected {key!r}")
    columns = tuple(cell.strip().lower() for cell in header[1:])
    if columns and columns[-1] == _NOTES_COLUMN:
        columns = columns[:-1]
    out: dict[str, dict[str, str]] = {}
    duplicates: list[str] = []
    for lineno, cells in rows[1:]:
        name = cells[0].strip()
        if not name:
            raise ValueError(f"{where}:{lineno}: a {heading} row needs a {key}")
        row = {}
        for index, column in enumerate(columns, start=1):
            value = cells[index].strip() if index < len(cells) else ""
            if value and value.lower() not in _BLANK:
                row[column] = value.lower()
        if name in out:
            duplicates.append(name)
        out[name] = row
    return out, columns, tuple(duplicates)


def _int(value: str, what: str, where: str, lineno: int) -> int:
    try:
        return int(value)
    except ValueError:
        raise ValueError(f"{where}:{lineno}: {what} must be a whole number, not {value!r}") from None


def parse(text: str, where: str = "model-ranking.md") -> Ranking:
    """Parse the file's text. `where` is what a ValueError calls it."""
    providers: dict[str, Provider] = {}
    duplicate_providers: list[str] = []
    for lineno, cells in _table(text, "Providers", _PROVIDER_COLUMNS, where):
        if len(cells) != len(_PROVIDER_COLUMNS):
            raise ValueError(f"{where}:{lineno}: a Providers row takes "
                             f"{' | '.join(_PROVIDER_COLUMNS)}; this one has {len(cells)} cell(s): "
                             f"{' | '.join(cells)}")
        preset_id, kind, order = cells[0], cells[1].lower(), cells[2]
        if not preset_id:
            raise ValueError(f"{where}:{lineno}: a Providers row needs a provider id")
        if kind not in KINDS:
            raise ValueError(f"{where}:{lineno}: kind {cells[1]!r} is not one of {', '.join(KINDS)}")
        if preset_id in providers:
            duplicate_providers.append(preset_id)
        providers[preset_id] = Provider(preset_id, kind, _int(order, "order", where, lineno))

    models: dict[str, Model] = {}
    duplicate_models: list[str] = []
    for lineno, cells in _table(text, "Models", _MODEL_COLUMNS, where):
        # The notes cell may be left off entirely, which is what a hand-written row usually does.
        if len(cells) == len(_MODEL_COLUMNS) - 1:
            cells = cells + [""]
        if len(cells) != len(_MODEL_COLUMNS):
            raise ValueError(f"{where}:{lineno}: a Models row takes {' | '.join(_MODEL_COLUMNS)} "
                             f"(notes may be left off); this one has {len(cells)} cell(s): "
                             f"{' | '.join(cells)}")
        name, classes, score, notes = cells
        if not name:
            raise ValueError(f"{where}:{lineno}: a Models row needs a name")
        words = () if classes.lower() in _BLANK else \
            tuple(dict.fromkeys(word for word in re.split(r"[,\s]+", classes.lower()) if word))
        number = None if score.lower() in _BLANK else _int(score, "score", where, lineno)
        if name in models:
            duplicate_models.append(name)
        models[name] = Model(name, words, number, notes)

    picks, pick_columns, duplicate_picks = _class_table(text, "Provider picks", _PICK_KEY, where)
    levels, level_columns, duplicate_levels = _class_table(text, "Levels", _LEVEL_KEY, where)

    return Ranking(providers, models, where, tuple(duplicate_providers), tuple(duplicate_models),
                   picks=picks, levels=levels, pick_columns=pick_columns,
                   level_columns=level_columns, duplicate_picks=duplicate_picks,
                   duplicate_levels=duplicate_levels)


# One parse per process per path: the file is read at worker start and never changes under a
# running worker. `reload()` is for the tests that write their own file.
_cache: dict[str, Ranking] = {}
_lock = threading.Lock()


def load(path=None) -> Ranking:
    """The parsed `model-ranking.md`, cached per process. `path` is for tests and for a worker
    pointed at another copy; the default is the one shipped beside this module."""
    resolved = str(Path(path) if path is not None else DEFAULT_PATH)
    with _lock:
        held = _cache.get(resolved)
        if held is not None:
            return held
    try:
        text = Path(resolved).read_text(encoding="utf-8")
    except OSError as exc:
        # Loud rather than quietly empty: with no file there is no score, no class and no provider
        # order, so every default list would come out blank and look like a ranking decision. The
        # file ships with the worker (CMake installs the whole `backend` directory), so this means
        # something went wrong with the install or the copy, not with the tables.
        raise FileNotFoundError(f"{resolved}: the model ranking file the defaults are built from is "
                                f"missing or unreadable ({exc.strerror}). It ships beside the "
                                f"worker's Python modules.") from exc
    ranking = parse(text, resolved)
    with _lock:
        _cache[resolved] = ranking
    return ranking


def reload(path=None) -> Ranking:
    """Drop the cache (for `path`, or all of it) and parse again."""
    with _lock:
        if path is None:
            _cache.clear()
        else:
            _cache.pop(str(Path(path)), None)
    return load(path)

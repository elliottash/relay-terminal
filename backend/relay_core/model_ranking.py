# SPDX-License-Identifier: AGPL-3.0-or-later
"""`model-ranking.md`, the file the default priority lists are built from (card #MDL1).

The owner asked for "a structured ranking MD or YAML in the repo i can review and edit", and chose
Markdown. So the numbers that decide the defaults live in `model-ranking.md` beside this module —
two pipe tables, **Providers** (`provider | kind | order`) and **Models**
(`name | classes | score | notes`) — and this is the whole of the code that reads them. It depends
on nothing: no YAML, no Markdown library, and (at import time) nothing else in `relay_core`, so
`presets` can import it while `check()` imports `presets` back.

`presets.INTELLIGENCE` is a view over `score(name)` and `presets.tier_list_defaults` does its
ranking through `classes()` and `provider_order()`; `model-ranking.md`'s own header says what the
rules are and how to edit the tables.

What raises and what only reports. A row that cannot be read at all — the wrong number of cells, a
score that is not a number, a `kind` outside the five — is a `ValueError` naming the line, because
a half-parsed table would silently drop a model. Everything else is `check()`'s: an unknown class
word, a provider the worker has never heard of, a catalog model with no row, a duplicate name. So
a typo in a class word gives a readable report from the test rather than a worker that will not
start.
"""
from __future__ import annotations

import re
import threading
from dataclasses import dataclass, field
from pathlib import Path

# The classes a model can be a default for. "local" is not one of them: the local class is the
# model servers on this machine, in the order they were saved, and belongs to no provider.
CLASSES = ("high", "main", "flash", "lite")
# How a provider is reached, lowest cost of the person's own money first (design rule 2.2).
KINDS = ("plan", "harness", "api", "router", "free")
# Where a provider with no row of its own sorts: after every one that has one. A custom provider
# the user added by hand is the case that matters.
UNKNOWN_PROVIDER_ORDER = 500

DEFAULT_PATH = Path(__file__).resolve().parent / "model-ranking.md"

_PROVIDER_COLUMNS = ("provider", "kind", "order")
_MODEL_COLUMNS = ("name", "classes", "score", "notes")
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
        """The kinds present, in KINDS order."""
        have = {row.kind for row in self.providers.values()}
        return tuple(kind for kind in KINDS if kind in have)

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

        for name, row in sorted(self.models.items()):
            for word in row.classes:
                if word not in CLASSES:
                    problems.append(f"model {name!r} has an unknown class {word!r} "
                                    f"(one of {', '.join(CLASSES)}, or '-' for none)")

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
                name = P.model_name(preset_id, row["id"])
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
                    "codex": ("gpt-6-astra", "gpt-5.6-sol", "gpt-5.6-terra", "gpt-5.6-luna")}


# ----- parsing ---------------------------------------------------------------------------------
def _cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def _is_separator(cells: list[str]) -> bool:
    return bool(cells) and all(cell and _SEPARATOR.fullmatch(cell) for cell in cells)


def _table(text: str, heading: str, columns: tuple[str, ...], where: str) -> list[tuple[int, list[str]]]:
    """The rows of the one pipe table under `## <heading>`, as (line number, cells), header and
    separator dropped. Extra whitespace anywhere is fine; a missing table, or a header naming
    something else, is a ValueError."""
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
    if not found:
        raise ValueError(f"{where}: no '## {heading}' heading")
    if not rows:
        raise ValueError(f"{where}: the '## {heading}' section has no table")
    lineno, header = rows[0]
    if tuple(cell.lower() for cell in header) != columns:
        raise ValueError(f"{where}:{lineno}: the {heading} table's header is "
                         f"{' | '.join(header)}; expected {' | '.join(columns)}")
    return rows[1:]


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

    return Ranking(providers, models, where, tuple(duplicate_providers), tuple(duplicate_models))


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

#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Refresh Relay's model defaults when new models ship (card #MCP7).

A provider's defaults live in several places that drift apart when edited by hand:

* `backend/relay_core/model-ranking.md` — the `classes` column of the Models table (what the
  default priority lists are built from), and the Provider picks table (openrouter);
* `presets.TIER_DEFAULTS` — each provider's main/flash/lite model and its request extras;
* `presets.MODEL_CATALOG` — the model rows, their `name` overrides and `tier` tags.

This tool reads all of them through `relay_core` itself and edits them together:

    relay-models.py show                          what each provider's defaults are, both ways
    relay-models.py check                         exit 1 when the places disagree
    relay-models.py discover [--json]             what this machine's keys/CLIs can reach
    relay-models.py set openai main=gpt-6-astra   point a provider's class at a model
    relay-models.py rename OLD NEW                repo-wide rename of a model id

The procedure: `discover` (what is new), `rename`/`set` (apply), `check` plus the tests the card
lists (prove), `scripts/land.py` (commit). Every subcommand takes `--root DIR`, the checkout to
read and edit (default: the one this script is in); `relay_core` is imported from DIR/backend.

`discover` never prints, logs or writes a key. `RELAY_MODELS_OFFLINE=1` skips every remote source.
"""
from __future__ import annotations

import argparse
import ast
import concurrent.futures
import datetime
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

CLASSES = ("high", "main", "flash", "lite")
GUESTS = ("guest:claude", "guest:codex")
RANKING = "backend/relay_core/model-ranking.md"
PRESETS_PY = "backend/relay_core/presets.py"
TIMEOUT_S = 10.0
# The tool's own two files talk *about* renaming; a repo-wide rename must not rewrite them.
SELF_FILES = ("scripts/relay-models.py", "tests/test_relay_models.py")
# What `rename` never touches: the board is the record of what was said at the time, evidence is a
# capture, the legacy fixtures are frozen inputs, and build trees are generated.
RENAME_EXCLUDED_PREFIXES = ("issues/", "docs/qa_evidence/", "tests/fixtures/issues_legacy/")
RENAME_EXCLUDED_DIRS = re.compile(r"(^|/)(build[^/]*|\.git|__pycache__|node_modules)(/|$)")

P = None      # relay_core.presets, once load_core has run
MR = None     # relay_core.model_ranking


class Refused(Exception):
    """An edit that cannot be made exactly; nothing has been written."""


def die(message: str, code: int = 2):
    print(f"relay-models: {message}", file=sys.stderr)
    raise SystemExit(code)


def offline() -> bool:
    return os.environ.get("RELAY_MODELS_OFFLINE", "").strip().lower() in ("1", "on", "yes", "true")


# ----- loading relay_core from the root ---------------------------------------------------------
def load_core(root: Path):
    """Import relay_core from ROOT/backend — never from anywhere else on sys.path (PYTHONPATH may
    name another checkout), and refuse when a relay_core from elsewhere is already imported."""
    global P, MR
    backend = (root / "backend").resolve()
    if not (backend / "relay_core" / "presets.py").is_file():
        die(f"{root} is not a Relay checkout (no backend/relay_core/presets.py)")
    loaded = sys.modules.get("relay_core")
    if loaded is not None and Path(loaded.__file__).resolve().parent.parent != backend:
        die(f"relay_core is already imported from {loaded.__file__}, not {backend}")
    sys.path[:] = [str(backend)] + [p for p in sys.path if Path(p or ".").resolve() != backend]
    import relay_core.model_ranking as mr
    import relay_core.presets as presets
    for module in (mr, presets):
        if Path(module.__file__).resolve().parent.parent != backend:
            die(f"imported {module.__name__} from {module.__file__}, not {backend}")
    P, MR = presets, mr
    return presets, mr


def name_of(preset_id: str, model_id: str) -> str:
    return P.model_name(preset_id, model_id) if model_id else ""


def guest_rows() -> list[dict]:
    """The two guest harnesses as `tier_list_defaults` takes them, installed and signed in, with the
    lists the adapters name when no CLI has been asked (codex's fallback, claude's aliases)."""
    rows = []
    try:
        from relay_core.guest_harness_provider import _CODEX_FALLBACK_MODELS
        models = [dict(row) for row in _CODEX_FALLBACK_MODELS]
        rows.append({"id": "guest:codex", "guest": "codex", "harness": "codex", "logged_in": True,
                     "models": models, "efforts": list(models[0].get("efforts") or []) if models else []})
    except Exception as exc:                         # noqa: BLE001 — shown, not fatal
        print(f"(guest:codex rows unavailable: {exc})", file=sys.stderr)
    try:
        from relay_core.guest_harness_claude import EFFORTS, MODEL_ALIASES
        rows.append({"id": "guest:claude", "guest": "claude", "harness": "claude", "logged_in": True,
                     "models": [{"id": alias, "efforts": list(EFFORTS), "default_effort": None}
                                for alias in MODEL_ALIASES],
                     "efforts": list(EFFORTS)})
    except Exception as exc:                         # noqa: BLE001
        print(f"(guest:claude rows unavailable: {exc})", file=sys.stderr)
    return rows


def guest_model_ids(guest: str) -> list[str]:
    for row in guest_rows():
        if row["id"] == guest:
            return [m["id"] for m in row["models"]]
    return list(MR._GUEST_MODEL_IDS.get(guest.split(":", 1)[1], ()))


def single_provider_defaults(provider: str) -> dict:
    """What the defaults give with only this provider usable: {class: entry or None}."""
    if provider in GUESTS:
        rows = [row for row in guest_rows() if row["id"] == provider]
        plain = P.tier_list_defaults([], guests=rows, listing=[])["plain"]
    else:
        plain = P.tier_list_defaults([provider], listing=[])["plain"]
    return {cls: (plain.get(cls) or [None])[0] for cls in CLASSES}


def providers_in_order() -> list[str]:
    rank = MR.load()
    known = list(P.PRESETS) + [g for g in GUESTS]
    return sorted(known, key=lambda p: (rank.provider_order(p), p))


def derived_tier(tier_defaults: dict, preset_id: str, model_id: str):
    """A MODEL_CATALOG row's `tier`: the first of main/flash/lite (then high, which only relay-pro
    names) whose entry in *any* provider's TIER_DEFAULTS is (preset_id, model_id) — a tier pointing
    at another preset (lite via openrouter) tags the row on the target — else None."""
    for tier in tuple(P.PROVIDER_TIERS) + ("high",):
        for table in tier_defaults.values():
            entry = table.get(tier)
            if entry and entry[0] == preset_id and entry[1] == model_id:
                return tier
    return None


# ----- show -------------------------------------------------------------------------------------
def cmd_show(args) -> int:
    width = max(len(p) for p in providers_in_order())

    def cell(entry, provider) -> str:
        if entry is None:
            return "-"
        model = entry["model"] or "(default)"
        return model if entry["preset"] == provider else f"{model}@{entry['preset']}"

    cols = []
    for provider in providers_in_order():
        ranked = single_provider_defaults(provider)
        table = P.TIER_DEFAULTS.get(provider, {})
        cols.append((provider, [cell(ranked[c], provider) for c in CLASSES],
                     [(cell({"preset": table[c][0], "model": table[c][1]}, provider) if c in table else "")
                      for c in CLASSES] if provider in P.TIER_DEFAULTS else None))
    cw = max(len(v) for _, r, t in cols for v in r + (t or [])) + 2
    print("ranked   = what model-ranking.md gives with only that provider usable")
    print("defaults = presets.TIER_DEFAULTS (a guest harness has none); * marks a disagreement")
    for provider, ranked, table in cols:
        line = f"{provider:<{width}}  ranked    " + "".join(f"{c} {v:<{cw}}" for c, v in zip(CLASSES, ranked))
        print(line.rstrip())
        if table is not None:
            parts = []
            for c, v, r in zip(CLASSES, table, ranked):
                mark = "*" if v and c != "lite" and v != r else " "
                parts.append(f"{c} {(v or '') + mark:<{cw}}" if v else " " * (len(c) + 1 + cw))
            print((f"{'':<{width}}  defaults  " + "".join(parts)).rstrip())
    return 0


# ----- check ------------------------------------------------------------------------------------
def problems() -> list[str]:
    out: list[str] = []
    rank = MR.load()
    catalog_ids = {p: {row["id"] for row in rows} for p, rows in P.MODEL_CATALOG.items()}

    # (a) the ranking and TIER_DEFAULTS name the same model for a provider's own high/main/flash.
    # Lite is left out on purpose: the owner emptied the providers' lite cells (lite is Relay
    # Free's), so a provider's TIER_DEFAULTS lite is a fallback the ranking no longer ranks.
    for provider, table in P.TIER_DEFAULTS.items():
        ranked = None
        for cls in ("high", "main", "flash"):
            entry = table.get(cls)
            if not entry or entry[0] != provider:
                continue
            ranked = ranked or single_provider_defaults(provider)
            got = ranked[cls]
            if got is None:
                out.append(f"{provider} {cls}: TIER_DEFAULTS names {entry[1]} "
                           f"({name_of(provider, entry[1])}), but model-ranking.md classes nothing of "
                           f"{provider}'s for {cls}")
            elif (got["preset"], got["model"]) != (provider, entry[1]):
                why = (f"its Provider picks {cls} cell is {rank.pick(provider, cls)}"
                       if rank.pick(provider, cls) else f"that is its highest-scored {cls} model")
                out.append(f"{provider} {cls}: TIER_DEFAULTS names {entry[1]} "
                           f"({name_of(provider, entry[1])}), but model-ranking.md gives "
                           f"{got['model']} ({name_of(got['preset'], got['model'])}) — {why}")

    # (b) every catalog row's tier tag is the one TIER_DEFAULTS implies.
    for preset_id, rows in P.MODEL_CATALOG.items():
        for row in rows:
            want = derived_tier(P.TIER_DEFAULTS, preset_id, row["id"])
            if row.get("tier") != want:
                out.append(f"MODEL_CATALOG[{preset_id!r}] {row['id']}: tier is {row.get('tier')!r}, "
                           f"TIER_DEFAULTS implies {want!r}")

    # (c) every model a tier names is a catalog row on the preset it points at.
    for provider, table in P.TIER_DEFAULTS.items():
        for tier, entry in table.items():
            target, model = entry[0], entry[1]
            if target not in P.PRESETS:
                out.append(f"TIER_DEFAULTS[{provider!r}][{tier!r}] points at {target!r}, not a preset")
            elif model not in catalog_ids.get(target, set()):
                out.append(f"TIER_DEFAULTS[{provider!r}][{tier!r}] names {model}, which has no "
                           f"MODEL_CATALOG[{target!r}] row")

    # (d) model-ranking.md's own check: names with no row, bad picks, bad levels, …
    out.extend(f"model-ranking.md: {line}" for line in rank.check())

    # (e) the other tables that name model ids, for ids a rename left behind.
    every_id = set().union(*catalog_ids.values())
    for model_id in P.OPENROUTER_TWINS:
        if model_id not in every_id:
            out.append(f"OPENROUTER_TWINS has {model_id!r}, which no MODEL_CATALOG row names")
    for alias, model_id in P.GUEST_MODEL_ALIASES.items():
        if model_id not in catalog_ids.get("anthropic", set()):
            out.append(f"GUEST_MODEL_ALIASES[{alias!r}] is {model_id!r}, which is not an anthropic "
                       f"MODEL_CATALOG row")
    for guest in GUESTS:
        short = guest.split(":", 1)[1]
        adapters = tuple(guest_model_ids(guest))
        if adapters and tuple(MR._GUEST_MODEL_IDS.get(short, ())) != adapters:
            out.append(f"model_ranking._GUEST_MODEL_IDS[{short!r}] is "
                       f"{list(MR._GUEST_MODEL_IDS.get(short, ()))}, the adapter names {list(adapters)}")
    # A Models row classed for something that nobody serves by that name decides nothing. The
    # ranking file names its rows by id, and a catalog row's display name is not always that id
    # (relay-free shows "relay free · main" for relay-main), so an id counts as served too.
    served = {name_of(p, row["id"]) for p, rows in P.MODEL_CATALOG.items() for row in rows}
    served |= {row["id"] for rows in P.MODEL_CATALOG.values() for row in rows}
    served |= {name_of("openrouter", slug) for slug in P.OPENROUTER_TWINS.values()}
    for guest in GUESTS:
        served |= {name_of(guest, m) for m in guest_model_ids(guest)}
    for name, row in sorted(rank.models.items()):
        if row.classes and name not in served:
            out.append(f"model-ranking.md: {name} is classed {', '.join(row.classes)}, but no "
                       f"provider's catalog serves a model by that name")
    return out


def cmd_check(args) -> int:
    found = problems()
    for line in found:
        print(line)
    if not found:
        print("ok")
    return 1 if found else 0


# ----- discover ---------------------------------------------------------------------------------
def http_json(url: str, headers: dict) -> object:
    request = urllib.request.Request(url, headers={"Accept": "application/json",
                                                   "User-Agent": "relay-terminal relay-models", **headers})
    with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
        return json.loads(response.read().decode("utf-8"))


def http_error(exc: Exception) -> str:
    """One line about a failed request. Never the request itself, whose headers hold the key."""
    if isinstance(exc, urllib.error.HTTPError):
        return f"HTTP {exc.code}" + (" (the key was refused)" if exc.code in (401, 403) else "")
    if isinstance(exc, urllib.error.URLError):
        return f"unreachable ({type(exc.reason).__name__})"
    return type(exc).__name__ + (f": {exc}" if isinstance(exc, (ValueError, RuntimeError)) else "")


# Ids a first-party /models lists that are not chat models; counted, not listed.
NOT_CHAT = re.compile(r"embed|tts|whisper|dall-e|imagen|veo|image|audio|aqa|moderation|transcribe|"
                      r"realtime|search|babbage|davinci|lyria|robotics|computer-use|native-audio")
NEW_CAP = 12


def summarise(source: str, served: list[dict], known: set[str], known_names: set[str]) -> dict:
    """NEW = served ids that are not in `known` and whose name the ranking does not already hold,
    newest first where the source dates them; GONE = known ids no longer served."""
    ids = {row["id"] for row in served}
    skipped = [row for row in served if NOT_CHAT.search(row["id"].lower())]
    fresh = [row for row in served if row["id"] not in known and row not in skipped]
    fresh.sort(key=lambda row: (-(row.get("created") or 0), row["id"]))
    # Older than the newest model the catalog already has from this source: still served, but
    # most likely a predecessor rather than a release to add.
    newest = max((row.get("created") or 0 for row in served if row["id"] in known), default=0)
    return {"source": source, "status": "ok", "served": len(ids),
            "new": [dict(row, known_name=name_of(source, row["id"]) in known_names,
                         older=bool(row.get("created") and newest and row["created"] < newest))
                    for row in fresh],
            "gone": sorted(known - ids), "not_chat": len(skipped)}


def discover_openrouter() -> dict:
    from relay_core import openrouter_catalog
    payload = openrouter_catalog.fetch(timeout=TIMEOUT_S)
    entries = [e for e in (payload.get("data") if isinstance(payload, dict) else payload) or []
               if isinstance(e, dict) and isinstance(e.get("id"), str)]
    known = {row["id"] for row in P.MODEL_CATALOG.get("openrouter", [])} | set(P.OPENROUTER_TWINS.values())
    vendors = {slug.split("/", 1)[0] for slug in known if "/" in slug}
    created = {e["id"]: e.get("created") if isinstance(e.get("created"), int) else 0 for e in entries}
    # Only vendors Relay already names, only the router's plain slugs (no `:free`, no `~` alias),
    # and only models no older than 60 days before the newest one the catalog knows from that vendor.
    newest = {}
    for slug in known:
        vendor = slug.split("/", 1)[0]
        newest[vendor] = max(newest.get(vendor, 0), created.get(slug, 0))
    served = []
    for e in entries:
        slug = e["id"]
        vendor = slug.split("/", 1)[0]
        if slug in known:
            served.append({"id": slug})
        elif vendor in vendors and ":" not in slug and not slug.startswith("~") \
                and created[slug] >= newest.get(vendor, 0) - 60 * 86400:
            served.append({"id": slug, "created": created[slug]})
    out = summarise("openrouter", served, known, set(MR.load().models))
    out["served"] = len(entries)
    out["note"] = f"public listing; vendors {', '.join(sorted(vendors))}, recent models only"
    return out


def discover_codex() -> dict:
    binary = shutil.which("codex")
    if not binary:
        return {"source": "guest:codex", "status": "skipped", "note": "codex is not installed"}
    from relay_core import guest_harness_provider as ghp
    ghp.CODEX_CATALOG_TIMEOUT = min(getattr(ghp, "CODEX_CATALOG_TIMEOUT", TIMEOUT_S), TIMEOUT_S)
    rows = ghp._read_codex_catalog(binary)
    known = {row["id"] for row in ghp._CODEX_FALLBACK_MODELS}
    out = summarise("guest:codex", [{"id": row["id"]} for row in rows if row.get("id")], known,
                    set(MR.load().models))
    out["note"] = "codex debug models, against guest_harness_provider._CODEX_FALLBACK_MODELS"
    return out


def discover_claude() -> dict:
    """Claude Code names its models by family alias; which model an alias is lives in Relay's own
    table (presets.GUEST_MODEL_ALIASES), so this is static — nothing to ask the CLI."""
    from relay_core.guest_harness_claude import MODEL_ALIASES
    aliases = [{"id": alias, "model": P.GUEST_MODEL_ALIASES.get(alias, ""),
                "name": name_of("guest:claude", alias)} for alias in MODEL_ALIASES]
    return {"source": "guest:claude", "status": "ok", "aliases": aliases,
            "note": "static: Claude Code's aliases and the ids presets.GUEST_MODEL_ALIASES maps them to"
                    + ("" if shutil.which("claude") else "; claude is not installed")}


def discover_preset(preset_id: str) -> dict:
    preset = P.PRESETS[preset_id]
    try:
        from relay_core import keystore
        key = keystore.lookup(preset_id)
    except Exception as exc:                         # noqa: BLE001 — a keyring that will not answer
        return {"source": preset_id, "status": "skipped",
                "note": f"could not read the stored key ({type(exc).__name__})"}
    if not key:
        return {"source": preset_id, "status": "skipped", "note": "no stored key"}
    headers = {"Authorization": f"Bearer {key}"}
    if preset_id == "anthropic":
        headers.update({"x-api-key": key, "anthropic-version": "2023-06-01"})
    try:
        payload = http_json(preset.base_url.rstrip("/") + "/models", headers)
    finally:
        del key, headers
    entries = payload.get("data") if isinstance(payload, dict) else payload
    if isinstance(payload, dict) and not isinstance(entries, list):
        entries = payload.get("models")
    served = []
    for e in entries if isinstance(entries, list) else []:
        model_id = e.get("id") or e.get("name") if isinstance(e, dict) else None
        if isinstance(model_id, str) and model_id:
            model_id = model_id.removeprefix("models/")
            created = e.get("created") if isinstance(e.get("created"), int) else 0
            served.append({"id": model_id, "created": created})
    known = {row["id"] for row in P.MODEL_CATALOG.get(preset_id, [])}
    out = summarise(preset_id, served, known, set(MR.load().models))
    out["note"] = f"GET {preset.base_url.rstrip('/')}/models with the stored key"
    return out


def cmd_discover(args) -> int:
    jobs = [("guest:claude", discover_claude)]
    if offline():
        results = {"guest:claude": discover_claude()}
        order = ["openrouter", "guest:codex"] + [p for p in P.PRESETS if p != "openrouter"] + ["guest:claude"]
        for source in order:
            results.setdefault(source, {"source": source, "status": "skipped",
                                        "note": "RELAY_MODELS_OFFLINE is set"})
    else:
        jobs += [("openrouter", discover_openrouter), ("guest:codex", discover_codex)]
        order = ["openrouter", "guest:codex"]
        for preset_id, preset in P.PRESETS.items():
            if preset_id == "openrouter":
                continue
            order.append(preset_id)
            if preset.hosted:
                continue
            jobs.append((preset_id, lambda p=preset_id: discover_preset(p)))
        order.append("guest:claude")
        results = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            futures = {pool.submit(fn): source for source, fn in jobs}
            for future in concurrent.futures.as_completed(futures):
                source = futures[future]
                try:
                    results[source] = future.result(timeout=TIMEOUT_S * 3)
                except Exception as exc:             # noqa: BLE001 — one line, the rest go on
                    results[source] = {"source": source, "status": "error", "note": http_error(exc)}
        for preset_id, preset in P.PRESETS.items():
            if preset.hosted:
                results[preset_id] = {"source": preset_id, "status": "skipped",
                                      "note": "Relay's hosted service; its models are the gateway's roles"}
    ordered = [results[s] for s in order if s in results]
    if args.json:
        print(json.dumps({"sources": ordered}, indent=2))
        return 0
    for res in ordered:
        head = f"{res['source']}: {res['status']}"
        if res.get("status") == "ok" and "served" in res:
            head += f", {res['served']} served"
        if res.get("note"):
            head += f" — {res['note']}"
        print(head)
        for alias in res.get("aliases", []):
            print(f"  {alias['id']:<8} -> {alias['model'] or '(unmapped)'}  (name {alias['name']})")
        new = res.get("new") or []
        for row in new[:NEW_CAP]:
            when = (datetime.datetime.fromtimestamp(row["created"], datetime.timezone.utc).date().isoformat()
                    if row.get("created") else "")
            flag = ("  (name already ranked)" if row.get("known_name") else "") + \
                   ("  (older than the catalog's newest)" if row.get("older") else "")
            print(f"  NEW   {row['id']}{('  ' + when) if when else ''}{flag}")
        if len(new) > NEW_CAP:
            print(f"  NEW   … and {len(new) - NEW_CAP} more (--json lists them all)")
        for model_id in res.get("gone") or []:
            print(f"  GONE  {model_id}")
        if res.get("not_chat"):
            print(f"  ({res['not_chat']} non-chat ids not listed)")
    return 0


# ----- source editing: model-ranking.md ---------------------------------------------------------
def md_cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def md_row(cells: list[str]) -> str:
    """A table row the way the file writes one: `| a | b | | d |` (a blank cell is one space)."""
    return "|" + "|".join(f" {cell} " if cell else " " for cell in cells) + "|"


def md_table(lines: list[str], heading: str) -> tuple[int, int]:
    """The line range [start, end) of the data rows of the one table under `## heading`."""
    heads = [i for i, line in enumerate(lines) if line.strip().startswith("#")
             and line.strip().lstrip("#").strip().lower() == heading.lower()]
    if len(heads) != 1:
        raise Refused(f"{RANKING}: expected one '## {heading}' heading, found {len(heads)}")
    i = heads[0] + 1
    while i < len(lines) and not lines[i].strip().startswith("|"):
        if lines[i].strip().startswith("#"):
            raise Refused(f"{RANKING}: the '## {heading}' section has no table")
        i += 1
    if i + 1 >= len(lines) or not set(lines[i + 1].replace("|", "").replace(":", "").strip()) <= {"-", " "}:
        raise Refused(f"{RANKING}: the '## {heading}' table has no header and separator")
    start = end = i + 2
    while end < len(lines) and lines[end].strip().startswith("|"):
        end += 1
    return start, end


def class_cell(classes) -> str:
    return ", ".join(c for c in CLASSES if c in classes) or "-"


def sort_key(cells: list[str]):
    score = cells[2].strip()
    try:
        number = int(score)
    except ValueError:
        number = None
    return (-(number if number is not None else -1), cells[0])


def edit_ranking(text: str, provider: str, cls: str, name: str, shared_warnings: list[str],
                 own_names: set[str], other_names: set[str], today: str) -> str:
    lines = text.split("\n")
    rank = MR.parse(text, RANKING)
    if name in rank.duplicate_models:
        raise Refused(f"{RANKING}: the Models table has more than one row for {name!r}")
    start, end = md_table(lines, "Models")
    rows = [md_cells(line) for line in lines[start:end]]
    for cells in rows:
        if len(cells) == 3:
            cells.append("")
        if len(cells) != 4:
            raise Refused(f"{RANKING}: a Models row does not have four cells: {' | '.join(cells)}")

    uses_pick = provider in rank.picks
    if uses_pick:
        # This provider's defaults are its Provider picks row, not the shared Models classes, so
        # the pick cell is what changes; the Models row only has to exist.
        pstart, pend = md_table(lines, "Provider picks")
        header_line = lines[pstart - 2]
        columns = [c.lower() for c in md_cells(header_line)]
        if cls not in columns:
            raise Refused(f"{RANKING}: the Provider picks table has no {cls} column")
        hits = [i for i in range(pstart, pend) if md_cells(lines[i])[0] == provider]
        if len(hits) != 1:
            raise Refused(f"{RANKING}: expected one Provider picks row for {provider}, found {len(hits)}")
        cells = md_cells(lines[hits[0]])
        cells += [""] * (len(columns) - len(cells))
        cells[columns.index(cls)] = name
        lines[hits[0]] = md_row(cells)
    else:
        for cells in rows:
            row_name = cells[0]
            classes = {w for w in re.split(r"[,\s]+", cells[1].lower()) if w and w not in ("-", "none")}
            if row_name == name:
                if cls not in classes:
                    cells[1] = class_cell(classes | {cls})
                    if row_name in other_names:
                        shared_warnings.append(f"{row_name}'s Models row is shared with another "
                                               f"provider's catalog, so that provider is ranked for "
                                               f"{cls} with it too")
            elif cls in classes and row_name in own_names:
                if row_name in other_names:
                    shared_warnings.append(f"{row_name} keeps {cls}: another provider's catalog serves "
                                           f"that name too, and the Models row is shared")
                else:
                    cells[1] = class_cell(classes - {cls})
    if not any(cells[0] == name for cells in rows):
        rows.append([name, "-" if uses_pick else cls, "", f"added by relay-models set {today}"])
    # Re-sort (the shipped-file test wants score descending, then name); rows already in order stay.
    rows.sort(key=sort_key)
    old = {tuple(md_cells(line) + ([""] if len(md_cells(line)) == 3 else [])): line for line in lines[start:end]}
    lines[start:end] = [old.get(tuple(cells)) or md_row(cells) for cells in rows]
    return "\n".join(lines)


# ----- source editing: presets.py ---------------------------------------------------------------
class Source:
    """presets.py as bytes, with edits by AST span (ast offsets are UTF-8 byte columns)."""

    def __init__(self, text: str):
        self.data = text.encode("utf-8")
        self.tree = ast.parse(text)
        self.starts = [0]
        for line in self.data.split(b"\n"):
            self.starts.append(self.starts[-1] + len(line) + 1)
        self.edits: list[tuple[int, int, bytes]] = []

    def span(self, node) -> tuple[int, int]:
        return (self.starts[node.lineno - 1] + node.col_offset,
                self.starts[node.end_lineno - 1] + node.end_col_offset)

    def text(self, node) -> str:
        a, b = self.span(node)
        return self.data[a:b].decode("utf-8")

    def replace(self, node, new: str):
        a, b = self.span(node)
        self.edits.append((a, b, new.encode("utf-8")))

    def insert(self, offset: int, new: str):
        self.edits.append((offset, offset, new.encode("utf-8")))

    def column(self, node) -> int:
        return len(self.data[self.starts[node.lineno - 1]:self.span(node)[0]].decode("utf-8"))

    def result(self) -> str:
        data = self.data
        last = None
        for a, b, new in sorted(self.edits, key=lambda e: (e[0], e[1]), reverse=True):
            if last is not None and b > last:
                raise Refused("two edits to presets.py overlap")
            data = data[:a] + new + data[b:]
            last = a
        return data.decode("utf-8")

    def assignment(self, name: str) -> ast.expr:
        found = [node.value for node in self.tree.body
                 if isinstance(node, (ast.Assign, ast.AnnAssign)) and node.value is not None
                 and any(isinstance(t, ast.Name) and t.id == name
                         for t in (node.targets if isinstance(node, ast.Assign) else [node.target]))]
        if len(found) != 1:
            raise Refused(f"{PRESETS_PY}: expected one assignment to {name}, found {len(found)}")
        return found[0]


def dict_value(node, key: str, where: str) -> ast.expr:
    if not isinstance(node, ast.Dict):
        raise Refused(f"{PRESETS_PY}: {where} is not written as a dict literal")
    hits = [v for k, v in zip(node.keys, node.values) if isinstance(k, ast.Constant) and k.value == key]
    if len(hits) != 1:
        raise Refused(f"{PRESETS_PY}: expected {where} to have one {key!r} key, found {len(hits)}")
    return hits[0]


def has_key(node, key: str) -> bool:
    return isinstance(node, ast.Dict) and any(isinstance(k, ast.Constant) and k.value == key for k in node.keys)


def anthropic_name(model_id: str) -> str:
    """Anthropic's API spells a version with hyphens (`claude-haiku-4-5`); the name uses a dot
    (`claude-haiku-4.5`), as OpenRouter and everyone else do. A date suffix stays as it is."""
    match = re.fullmatch(r"(claude-[a-z]+-\d+)-(\d{1,2})((?:-\d{6,8})?)", model_id)
    return f"{match.group(1)}.{match.group(2)}{match.group(3)}" if match else model_id


def presets_state() -> dict:
    """What `edit_presets` simulates: TIER_DEFAULTS as {provider: {tier: (preset, model)}} and
    MODEL_CATALOG's rows, updated edit by edit so several CLASS=MODEL pairs compose."""
    return {"tiers": {p: {t: (e[0], e[1]) for t, e in table.items()} for p, table in P.TIER_DEFAULTS.items()},
            "catalog": {p: [dict(row) for row in rows] for p, rows in P.MODEL_CATALOG.items()}}


def edit_presets(text: str, provider: str, cls: str, model: str, notes: list[str], state: dict) -> str:
    """Rewrite TIER_DEFAULTS[provider][cls]'s model (when that row exists or cls is main/flash/lite),
    add a MODEL_CATALOG row when the model has none, and re-derive every catalog row's tier tag.
    `state` (presets_state) is updated to match the returned text."""
    src = Source(text)
    tiers, catalog = state["tiers"], state["catalog"]

    td = src.assignment("TIER_DEFAULTS")
    if provider in tiers and (cls != "high" or cls in tiers[provider]):
        table = dict_value(td, provider, f"TIER_DEFAULTS[{provider!r}]")
        if not isinstance(table, ast.Dict):
            raise Refused(f"{PRESETS_PY}: TIER_DEFAULTS[{provider!r}] is not a dict literal")
        if has_key(table, cls):
            entry = dict_value(table, cls, f"TIER_DEFAULTS[{provider!r}]")
            current = tiers[provider][cls]
            if isinstance(entry, ast.Tuple) and len(entry.elts) == 3 \
                    and all(isinstance(e, ast.Constant) and isinstance(e.value, str) for e in entry.elts[:2]):
                if entry.elts[0].value == provider:
                    src.replace(entry.elts[1], json.dumps(model))
                else:
                    notes.append(f"TIER_DEFAULTS[{provider!r}][{cls!r}] pointed at {entry.elts[0].value}; "
                                 f"it is now {provider}'s own, with no extras ({src.text(entry.elts[2])} dropped)")
                    src.replace(entry, f"({json.dumps(provider)}, {json.dumps(model)}, {{}})")
            elif isinstance(entry, ast.Name):
                if current[0] == provider:
                    raise Refused(f"{PRESETS_PY}: TIER_DEFAULTS[{provider!r}][{cls!r}] is the shared "
                                  f"constant {entry.id}; edit it by hand")
                notes.append(f"TIER_DEFAULTS[{provider!r}][{cls!r}] was {entry.id} ({current[0]} "
                             f"{current[1]}); it is now {provider}'s own model, with no extras — review them")
                src.replace(entry, f"({json.dumps(provider)}, {json.dumps(model)}, {{}})")
            else:
                raise Refused(f"{PRESETS_PY}: TIER_DEFAULTS[{provider!r}][{cls!r}] is not written as "
                              f"(\"{provider}\", \"<model>\", <extras>); edit it by hand")
            tiers[provider][cls] = (provider, model)
        elif cls != "high":
            raise Refused(f"{PRESETS_PY}: TIER_DEFAULTS[{provider!r}] has no {cls!r} entry; add it by hand")

    rows_node = dict_value(src.assignment("MODEL_CATALOG"), provider, f"MODEL_CATALOG[{provider!r}]")
    if not isinstance(rows_node, ast.List) or not all(isinstance(e, ast.Dict) for e in rows_node.elts):
        raise Refused(f"{PRESETS_PY}: MODEL_CATALOG[{provider!r}] is not a list of dict literals")
    if len(rows_node.elts) != len(catalog.get(provider, [])):
        raise Refused(f"{PRESETS_PY}: MODEL_CATALOG[{provider!r}] in the source does not match the module")
    new_row = None
    if not any(row["id"] == model for row in catalog.get(provider, [])):
        name = anthropic_name(model)
        new_row = {"id": model}
        if name != P.derived_name(model):
            new_row["name"] = name
        new_row.update({"tier": None, "efforts": None})
        catalog.setdefault(provider, []).append(new_row)
        notes.append(f"added MODEL_CATALOG[{provider!r}] row {model}"
                     + (f" (name {new_row['name']})" if "name" in new_row else ""))

    # Every preset's tags, from the TIER_DEFAULTS this edit leaves behind.
    for preset_id, rows in catalog.items():
        node = dict_value(src.assignment("MODEL_CATALOG"), preset_id, f"MODEL_CATALOG[{preset_id!r}]")
        for row, elt in zip(rows, node.elts):
            want = derived_tier(tiers, preset_id, row["id"])
            if want != row.get("tier"):
                src.replace(dict_value(elt, "tier", f"MODEL_CATALOG[{preset_id!r}] row {row['id']}"),
                            json.dumps(want) if want else "None")
                row["tier"] = want
    if new_row is not None:
        new_row["tier"] = derived_tier(tiers, provider, model)
        last = rows_node.elts[-1] if rows_node.elts else None
        if last is None:
            raise Refused(f"{PRESETS_PY}: MODEL_CATALOG[{provider!r}] is empty; add the row by hand")
        body = ", ".join(f"{json.dumps(k)}: {json.dumps(v) if v is not None else 'None'}"
                         for k, v in new_row.items())
        src.insert(src.span(last)[1], ",\n" + " " * src.column(last) + "{" + body + "}")
    return src.result()


# ----- set --------------------------------------------------------------------------------------
def catalog_names(preset_id: str) -> set[str]:
    return {name_of(preset_id, row["id"]) for row in P.MODEL_CATALOG.get(preset_id, [])}


def unified(path: str, old: str, new: str) -> str:
    return "".join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                        f"a/{path}", f"b/{path}"))


def claim_hint(paths: list[str], written: bool) -> str:
    joined = " ".join(paths)
    if written:
        return ("Written. If you claimed these before running this, commit as usual; if not, claim them "
                f"from HEAD so only this change lands:\n  python3 scripts/land.py begin <me> --from-head {joined}")
    return f"Claim these before running without --dry-run:\n  python3 scripts/land.py begin <me> {joined}"


def cmd_set(args) -> int:
    root = args.root
    provider = args.provider
    if provider not in P.PRESETS and provider not in GUESTS:
        die(f"unknown provider {provider!r} (one of {', '.join(list(P.PRESETS) + list(GUESTS))})")
    assignments = []
    for item in args.assignments:
        cls, sep, model = item.partition("=")
        if not sep or cls not in CLASSES or not model.strip():
            die(f"{item!r}: expected CLASS=MODEL with CLASS one of {', '.join(CLASSES)}")
        assignments.append((cls, model.strip()))
    if len({cls for cls, _ in assignments}) != len(assignments):
        die("a class is given twice")
    if provider in GUESTS and any(cls == "lite" for cls, _ in assignments):
        die("a guest harness is never a lite default (roles.GUEST_TIERS)")

    today = datetime.date.today().isoformat()
    ranking_path, presets_path = root / RANKING, root / PRESETS_PY
    ranking_old = ranking_path.read_text(encoding="utf-8")
    presets_old = presets_path.read_text(encoding="utf-8")
    ranking_new, presets_new = ranking_old, presets_old
    warnings: list[str] = []
    notes: list[str] = []

    if provider in GUESTS:
        own = {name_of(provider, m) for m in guest_model_ids(provider)}
        others = set().union(*(catalog_names(p) for p in P.MODEL_CATALOG))
    else:
        own = catalog_names(provider)
        others = set().union(*(catalog_names(p) for p in P.MODEL_CATALOG if p != provider))
    state = presets_state()
    try:
        for cls, model in assignments:
            if provider in GUESTS:
                name = name_of(provider, model)
                if model not in guest_model_ids(provider):
                    notes.append(f"{model} is not in {provider}'s built-in list; the ranking row is "
                                 f"read when the CLI reports it")
            else:
                if any(row["id"] == model for row in P.MODEL_CATALOG.get(provider, [])):
                    name = name_of(provider, model)
                else:
                    name = anthropic_name(model) if anthropic_name(model) != P.derived_name(model) \
                        else P.derived_name(model)
                if provider == "openrouter" and "/" not in model:
                    die(f"openrouter takes a slug (vendor/model), not {model!r}")
                if P.PRESETS[provider].hosted:
                    notes.append(f"{provider} is Relay's hosted service; its model ids are the gateway's")
            ranking_new = edit_ranking(ranking_new, provider, cls, name, warnings, own, others, today)
            if provider not in GUESTS:
                # One assignment at a time, each against the text and state the last one left.
                presets_new = edit_presets(presets_new, provider, cls, model, notes, state)
            if provider == "anthropic" and cls in ("main", "flash", "lite"):
                family = re.match(r"claude-([a-z]+)-", model)
                alias = family.group(1) if family else ""
                if alias in P.GUEST_MODEL_ALIASES and P.GUEST_MODEL_ALIASES[alias] != model:
                    notes.append(f"Claude Code's {alias!r} still maps to {P.GUEST_MODEL_ALIASES[alias]} "
                                 f"(presets.GUEST_MODEL_ALIASES); if {model} replaced it, use `rename`")
        MR.parse(ranking_new, RANKING)            # the edited file must still parse
        compile(presets_new, PRESETS_PY, "exec")
    except Refused as exc:
        die(f"refused, nothing written: {exc}", 3)

    changed = [(RANKING, ranking_path, ranking_old, ranking_new),
               (PRESETS_PY, presets_path, presets_old, presets_new)]
    changed = [c for c in changed if c[2] != c[3]]
    for line in warnings:
        print(f"warning: {line}")
    for line in notes:
        print(f"note: {line}")
    if not changed:
        print("nothing to change")
        return 0
    if args.dry_run:
        for rel, _, old, new in changed:
            sys.stdout.write(unified(rel, old, new))
        print(claim_hint([c[0] for c in changed], written=False))
        return 0
    for _, path, _, new in changed:
        path.write_text(new, encoding="utf-8")
    for rel, *_ in changed:
        print(f"wrote {rel}")
    result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--root", str(root), "check"],
                            capture_output=True, text=True, env=child_env())
    print("check:", (result.stdout.strip() or result.stderr.strip()).replace("\n", "\n  "))
    print(claim_hint([c[0] for c in changed], written=True))
    print("Then run: tests/test_relay_models.py tests/test_model_ranking.py tests/test_presets.py "
          "tests/test_tier_lists.py")
    return 0


def child_env() -> dict:
    env = dict(os.environ)
    env.pop("PYTHONPATH", None)
    return env


# ----- rename -----------------------------------------------------------------------------------
def boundary_pattern(old: str) -> re.Pattern:
    """`old` as a whole token: not inside a longer id (`claude-opus-5` is not in `claude-opus-5-5`,
    `claude-opus-5.1` or `xclaude-opus-5`), but a serving or dated suffix (`-pro`, `-highspeed`,
    `-20260514`, `:batch`) keeps the match — the stem is renamed and the suffix stays."""
    return re.compile(r"(?<![A-Za-z0-9_.\-])" + re.escape(old) +
                      r"(?![A-Za-z0-9_])(?![.\-]\d{1,3}(?!\d))")


def display_name(model_id: str) -> str | None:
    match = re.fullmatch(r"claude-([a-z]+)-(\d+)(?:[-.](\d{1,2}))?", model_id)
    if not match:
        return None
    version = match.group(2) + (f".{match.group(3)}" if match.group(3) else "")
    return f"Claude {match.group(1).capitalize()} {version}"


def rename_pairs(old: str, new: str, names: list[str]) -> list[tuple[str, str]]:
    pairs = [(old, new)]
    for item in names:
        a, sep, b = item.partition("=")
        if not sep or not a or not b:
            die(f"--name {item!r}: expected OLDNAME=NEWNAME")
        pairs.append((a, b))
    # Anthropic's dotted name is unambiguous (no id is spelled that way), so it follows unless an
    # explicit --name already says what that spelling becomes.
    if anthropic_name(old) != old and all(a != anthropic_name(old) for a, _ in pairs):
        pairs.append((anthropic_name(old), anthropic_name(new)))
    old_display, new_display = display_name(old), display_name(new)
    if old_display and new_display and old_display != new_display:
        pairs.append((old_display, new_display))
        pairs.append((old_display.lower(), new_display.lower()))
    seen, out = set(), []
    for a, b in pairs:
        if a != b and a not in seen:
            seen.add(a)
            out.append((a, b))
    return out


def rename_files(root: Path) -> list[str]:
    try:
        listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True,
                                check=True).stdout.decode("utf-8", "replace").split("\0")
        top = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"], capture_output=True,
                             text=True, check=True).stdout.strip()
        if Path(top).resolve() != root.resolve():
            raise subprocess.CalledProcessError(1, "git")
        files = [f for f in listed if f]
    except (subprocess.CalledProcessError, FileNotFoundError):
        files = [str(p.relative_to(root)).replace(os.sep, "/") for p in root.rglob("*") if p.is_file()]
    out = []
    for rel in sorted(files):
        if rel.startswith(RENAME_EXCLUDED_PREFIXES) or RENAME_EXCLUDED_DIRS.search(rel) or rel in SELF_FILES:
            continue
        if (root / rel).is_file():
            out.append(rel)
    return out


def cmd_rename(args) -> int:
    root = args.root
    pairs = rename_pairs(args.old, args.new, args.name or [])
    patterns = [(boundary_pattern(a), b) for a, b in pairs]
    counts: dict[str, int] = {}
    edits: dict[str, str] = {}
    paths_named = []
    for rel in rename_files(root):
        if any(p.search(rel) for p, _ in patterns):
            paths_named.append(rel)
        try:
            data = (root / rel).read_bytes()
        except OSError:
            continue
        if b"\0" in data[:8192]:
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        total = 0
        for pattern, replacement in patterns:
            text, n = pattern.subn(replacement, text)
            total += n
        if total:
            counts[rel] = total
            edits[rel] = text
    print("renaming: " + ", ".join(f"{a} -> {b}" for a, b in pairs))
    for rel, n in counts.items():
        print(f"{n:5d}  {rel}")
    print(f"{sum(counts.values())} replacement(s) in {len(counts)} file(s)"
          + (" (dry run: nothing written)" if args.dry_run else ""))
    for rel in paths_named:
        print(f"note: the path {rel} names the model; rename the file by hand if it should follow")
    if not counts:
        return 0
    if not args.dry_run:
        for rel, text in edits.items():
            (root / rel).write_text(text, encoding="utf-8")
    print(claim_hint(sorted(counts), written=not args.dry_run))
    if not args.dry_run:
        print("Then run: scripts/relay-models.py check, and the tests that name the model")
    return 0


# ----- entry ------------------------------------------------------------------------------------
def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="relay-models.py", description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__.split("\n\n", 1)[1])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="the Relay checkout to read and edit (default: this script's)")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("show", help="each provider's defaults, from the ranking and from TIER_DEFAULTS")
    sub.add_parser("check", help="exit 1 when the places a provider's defaults live disagree")
    d = sub.add_parser("discover", help="what this machine's keys and CLIs can reach (read-only)")
    d.add_argument("--json", action="store_true")
    s = sub.add_parser("set", help="point a provider's classes at models, everywhere at once")
    s.add_argument("provider")
    s.add_argument("assignments", nargs="+", metavar="CLASS=MODEL")
    s.add_argument("--dry-run", action="store_true")
    r = sub.add_parser("rename", help="rename a model id (and its names) across the checkout")
    r.add_argument("old")
    r.add_argument("new")
    r.add_argument("--name", action="append", metavar="OLDNAME=NEWNAME",
                   help="also rename the person-facing name (repeatable)")
    r.add_argument("--dry-run", action="store_true")
    # `--root` is accepted after the subcommand too, which is where a person types it.
    for p in (sub.choices.values()):
        p.add_argument("--root", type=Path, default=argparse.SUPPRESS, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    args.root = args.root.resolve()
    if args.command == "rename":
        return cmd_rename(args)           # text only; needs no relay_core
    load_core(args.root)
    return {"show": cmd_show, "check": cmd_check, "discover": cmd_discover, "set": cmd_set}[args.command](args)


if __name__ == "__main__":
    raise SystemExit(main())

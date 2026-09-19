# SPDX-License-Identifier: GPL-3.0-or-later
"""Cross-provider QA (card #T71W): the signature a card carries, and who should verify it.

Three jobs, all pure except the two probes at the bottom:

* **`signature(preset_id, model)`** — the canonical `vendor/model` a worker stamps on a card it
  implemented (`implemented_by`) or closed out of a QA lane (`verified_by`).  The `vendor` segment
  is the *model's* vendor, never the aggregator that routed to it: OpenRouter serving
  `deepseek/deepseek-v4.1-flash` signs `deepseek/deepseek-v4.1-flash`, because what QA needs to know
  is whose model wrote the code, not whose invoice it lands on.
* **`family(text)`** — the vendor family of any signature *or* of the free text that
  `implemented_by` held before this card ("Claude Opus 5 (pane 2)").  Both spellings must land on
  the same family, or the independence rule would let Claude close what Claude wrote.
* **`recommend(...)`** — the ranked verifier for a card, from `VERIFIER_RANK`: the implementer's
  own family is skipped, the rest of its lineage is moved behind every other lineage but still
  offered, a local endpoint comes last, and anything this machine cannot run is reported as
  unavailable with what is missing.  `availability()` is the worker-side probe that feeds it; the
  pure function takes availability as arguments so tests never touch PATH or the keyring.

Why a lineage as well as a ranking: the research report
(`docs/qa_evidence/2026-09-19-cross-provider-qa/research-cross-model-qa.md`) finds that error
correlation rises with capability and that distilled models inherit their teacher's blind spots, so
"the strongest available" is not the best verifier of a given author.  The ranking is the owner's
capability order; the lineage table is the correction on top of it.  Both are data, and both are
meant to be edited when the evidence changes — see `LINEAGE` and `VERIFIER_RANK` below.

Stdlib only (plus `relay_core.presets` for the models, `relay_core.guest` for the binary names and
`relay_core.keystore` for the stored keys).  Nothing here calls a model or the network.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import time

from . import presets

# ---------------------------------------------------------------- families and vendors

#: What a token in a model id or in free text says about the vendor.  Matched against the tokens of
#: the *model* segment first and the provider segment second, so `openrouter/deepseek-v4.1-flash` is
#: DeepSeek and `anthropic/claude-opus-5` is Anthropic.  Prefix rules, in order; the first token that
#: matches anything wins.
_VENDOR_PREFIXES: tuple[tuple[str, str], ...] = (
    ("claude", "anthropic"),        # claude-opus-5, claude-code, claude-sonnet-5
    ("anthropic", "anthropic"),
    ("gpt", "openai"),              # gpt-6-astra, gpt-5-codex
    ("chatgpt", "openai"),
    ("codex", "openai"),            # the Codex CLI signs openai/codex
    ("openai", "openai"),
    ("glm", "glm"),                 # glm-5.3, glm-coding, glm-5.3-flash
    ("zai", "glm"),
    ("z.ai", "glm"),
    ("kimi", "kimi"),               # kimi-k3, kimi-code, kimi-for-coding
    ("moonshot", "kimi"),
    ("deepseek", "deepseek"),
    ("gemini", "gemini"),
    ("google", "gemini"),           # the OpenRouter slug for Gemini is google/…
    ("minimax", "minimax"),
    ("qwen", "qwen"),
    ("llama", "llama"),
    ("local", "local"),
)

#: Whole-token rules for ids that no prefix above catches: the Kimi Code plan's models are bare
#: `k3` / `k2.7-…`, and OpenAI's reasoning line is `o3` / `o4-mini`.
_VENDOR_TOKENS: tuple[tuple[re.Pattern, str], ...] = (
    (re.compile(r"^k[0-9](\.[0-9]+)?(-.*)?$"), "kimi"),
    (re.compile(r"^o[1-9](-.*)?$"), "openai"),
)

#: Display names, for a card line and for `relay-board.py verifier`.  Family names are ids; these
#: are what a person reads.
FAMILY_LABELS: dict[str, str] = {
    "openai": "OpenAI", "anthropic": "Claude", "glm": "GLM", "kimi": "Kimi",
    "deepseek": "DeepSeek", "gemini": "Gemini", "minimax": "MiniMax", "qwen": "Qwen",
    "llama": "Llama", "relay-free": "Relay Free", "local": "Local",
}

#: What a runner is called on screen.  Display only: the *model* each runner runs comes from
#: `presets.TIER_DEFAULTS` (see `runner_model`), so this table never decides anything.
RUNNER_LABELS: dict[str, str] = {
    "guest:codex": "Codex", "guest:claude": "Claude Code",
    "preset:openai": "GPT-6 Astra", "preset:anthropic": "Claude Opus 5",
    "preset:glm-coding": "GLM-5.3", "preset:glm": "GLM-5.3",
    "preset:kimi-code": "Kimi K3", "preset:kimi": "Kimi K3",
    "preset:openrouter": "DeepSeek V4.1", "preset:gemini": "Gemini 3.1 Pro",
    "preset:minimax": "MiniMax M3", "preset:relay-free": "Relay Free",
}

#: The model id a guest CLI signs with.  A guest's exact model is not observable from outside, so
#: the signature names the harness (`openai/codex`, `anthropic/claude-code`) and stops there.
GUEST_MODELS: dict[str, str] = {"codex": "codex", "claude": "claude-code"}

_TOKENS = re.compile(r"[a-z0-9.]+")
_PARENS = re.compile(r"\([^)]*\)")


def _tokens(text: str) -> list[str]:
    return _TOKENS.findall(_PARENS.sub(" ", (text or "").lower()))


def _family_of_tokens(tokens: list[str]) -> str:
    for token in tokens:
        for prefix, vendor in _VENDOR_PREFIXES:
            if token.startswith(prefix):
                return vendor
        for pattern, vendor in _VENDOR_TOKENS:
            if pattern.match(token):
                return vendor
    return ""


#: Relay Free is **not a family**: the gateway routes each role to somebody else's model, so a card
#: implemented on `relay-free/relay-main` was written by whatever that role points at today, and a
#: verifier must be chosen against *that*.  Read off `gateway/gateway.example.json` on 2026-09-19;
#: each row is (upstream model, how a person should see it).  The gateway passes the upstream's own
#: `model` string through in its response (`gateway/proxy.py` pumps the upstream stream to the
#: client, and `validate.upstream_body` sets `model` to the upstream's), but nothing in the worker
#: records it yet — when something does, prefer the served string over this table and leave the
#: table as the fallback for a role that never answered.
RELAY_FREE_UPSTREAMS: dict[str, tuple[str, str]] = {
    "relay-main": ("z-ai/glm-5.3-flash", "GLM-5.3 Flash"),
    "relay-flash": ("deepseek/deepseek-v4.1-flash", "DeepSeek V4.1 Flash"),
    "relay-lite": ("google/gemini-3.5-flash-lite", "Gemini 3.5 Flash Lite"),
}
#: A bare `relay-free` with no role names the tier a pane's turns run on.
RELAY_FREE_DEFAULT_ROLE = "relay-main"


def relay_free_upstream(role: str = RELAY_FREE_DEFAULT_ROLE) -> tuple[str, str]:
    """The (model, label) the gateway routes a Relay Free role to today."""
    key = (role or "").strip().lower()
    if key in ("", "relay-free", "relay"):
        key = RELAY_FREE_DEFAULT_ROLE
    return RELAY_FREE_UPSTREAMS.get(key, RELAY_FREE_UPSTREAMS[RELAY_FREE_DEFAULT_ROLE])


def family(text: str | None) -> str:
    """The vendor family of a signature, a model id, a preset id or legacy free text.

    `anthropic/claude-opus-5`, `Claude Opus 5 (pane 2)` and `claude-code` are all `anthropic`;
    `openrouter/deepseek-v4.1-flash` is `deepseek`, not `openrouter`; `gpt-5-codex` and a bare
    `codex` are `openai`; `relay-free/relay-main` is the family of the gateway's upstream for that
    role (`glm` today), because Relay Free is a route, not a lab.  An unrecognized string falls back
    to its provider segment, else its first word, so two unknown models still tell each other apart
    and the independence rule keeps working on a provider this table has never heard of.
    """
    if not isinstance(text, str) or not text.strip():
        return ""
    cleaned = _PARENS.sub(" ", text.strip().lower())
    head, _, tail = cleaned.rpartition("/")
    for segment in (tail.strip(), head.strip()):
        # The whole segment, not its tokens: the role is `relay-flash`, and "relay" alone would
        # answer for the Main role whatever the card actually ran on.
        if segment.startswith("relay"):
            return family(relay_free_upstream(segment)[0])
    # The model segment decides; the provider segment only breaks a tie it could not.
    found = _family_of_tokens(_tokens(tail)) or _family_of_tokens(_tokens(head))
    if found:
        return found
    words = _tokens(head) or _tokens(tail)
    return words[0] if words else ""


def signature(preset_id: str | None, model: str | None = "") -> str:
    """The canonical `vendor/model` for a pane running `model` on `preset_id`.

    `("openrouter", "deepseek/deepseek-v4.1-flash")` → `deepseek/deepseek-v4.1-flash`;
    `("relay-free", "relay-main")` → `relay-free/relay-main`; `("local:bonsai", "bonsai-2-27b")` →
    `local/bonsai-2-27b`; a guest is `openai/codex` or `anthropic/claude-code`.  Empty when neither
    argument says anything — a worker with no signature falls back to the agent's own
    `implemented_by` argument rather than stamping a guess.
    """
    preset = (preset_id or "").strip().lower()
    name = (model or "").strip()
    if preset.startswith("guest:"):
        guest = preset[len("guest:"):]
        return signature(None, GUEST_MODELS.get(guest, guest))
    if preset.startswith("local:"):
        # A local endpoint is signed by the machine, not by whoever trained the weights: two people
        # running "qwen3" locally are not running the same build, and nothing offsite saw it.
        return f"local/{name or preset[len('local:'):]}"
    if preset == "relay-free" or name.lower().startswith("relay-"):
        # The signature names the route the owner chose, not the upstream behind it: the upstream
        # can change under a card, and `family()` resolves it through RELAY_FREE_UPSTREAMS when the
        # verifier is picked, which is the moment the answer has to be current.
        return f"relay-free/{name or RELAY_FREE_DEFAULT_ROLE}"
    if not name and preset:
        row = presets.PRESETS.get(preset)
        name = row.model if row is not None else ""
    if not name and not preset:
        return ""
    vendor = family(name) or family(preset)
    slug = name.split("/")[-1] if name else preset
    if not vendor:
        return ""
    return f"{vendor}/{slug}" if slug else vendor


# ---------------------------------------------------------------------------- lineage

#: Which families share a lineage — a common training line, a shared distillation teacher or a
#: shared corpus — and therefore share blind spots.  From the research report
#: (`docs/qa_evidence/2026-09-19-cross-provider-qa/research-cross-model-qa.md`): Lee et al. 2025
#: (arXiv 2501.12619) scores DeepSeek, Qwen and GLM high on distillation and Claude and Gemini low,
#: and Goel et al. 2025 (arXiv 2502.04313) finds judges favour models similar to themselves
#: (r = 0.84) and that the gain from a weak judge rises with *dis*similarity (r = −0.85).  So a
#: same-lineage verifier is not skipped, it is moved behind every other-lineage one: still a second
#: pair of eyes, just a weaker check.  One row, one reason, so changing it is a one-line edit.
#: Relay Free has no row: it is a route, not a lab (see `RELAY_FREE_UPSTREAMS`).
LINEAGE: dict[str, str] = {
    "openai": "openai",          # its own pretraining line; the teacher much of the field distils
    "anthropic": "anthropic",    # its own line; low distillation score (Lee et al. 2025)
    "gemini": "google",          # Google's own line; low distillation score (Lee et al. 2025)
    "glm": "cn-open",            # Z.AI — high distillation, shared open corpora (Lee et al. 2025)
    "kimi": "cn-open",           # Moonshot — same line, similar RL recipes
    "deepseek": "cn-open",       # DeepSeek — high distillation, and much of cn-open distils it
    "minimax": "cn-open",        # MiniMax — same line
    "qwen": "cn-open",           # Alibaba — high distillation; most local builds derive from it
    "llama": "meta",             # not in the report's table: Meta's open line, kept distinct
    "local": "local",            # whatever is served on this machine; see the local row's why
}


def lineage(family_id: str | None) -> str:
    """The lineage group of a family, or "" for one the table does not know."""
    return LINEAGE.get((family_id or "").strip().lower(), "")


# ------------------------------------------------------------------------ the ranking

#: The owner's preference order (2026-09-19: "codex -> claude -> glm -> kimi -> deepseek -> relay"),
#: extended to every provider Relay has.  `runners` is how that family is opened *here*, best first:
#: a guest CLI before an API key for OpenAI and Anthropic, because the CLI is the stronger harness.
#: Data, not code — reorder the rows to reorder the recommendation.
VERIFIER_RANK: tuple[dict, ...] = (
    # The owner's first choice, and the CLI is installed here more often than an OpenAI key exists.
    {"family": "openai", "label": "OpenAI", "runners": ("guest:codex", "preset:openai")},
    # Second: a different lineage from OpenAI, and Claude Code is the other first-class harness.
    {"family": "anthropic", "label": "Claude", "runners": ("guest:claude", "preset:anthropic")},
    # The Coding Plan key is the one most likely to be stored here; the standard API is the fallback.
    {"family": "glm", "label": "GLM", "runners": ("preset:glm-coding", "preset:glm")},
    {"family": "kimi", "label": "Kimi", "runners": ("preset:kimi-code", "preset:kimi")},
    # DeepSeek has no direct preset: OpenRouter's Main model *is* DeepSeek V4.1 Flash.
    {"family": "deepseek", "label": "DeepSeek", "runners": ("preset:openrouter",)},
    {"family": "gemini", "label": "Gemini", "runners": ("preset:gemini",)},
    {"family": "minimax", "label": "MiniMax", "runners": ("preset:minimax",)},
    # Always available on a fresh install, so it is the floor: some verifier rather than none. Its
    # family is *not* "relay-free" — the row is resolved to the gateway's upstream for the Main role
    # (`RELAY_FREE_UPSTREAMS`), so a GLM card is never handed back to GLM through the gateway.
    {"family": "relay-free", "label": "Relay Free", "runners": ("preset:relay-free",)},
    # Runners are filled in from the local-endpoint registry at recommend time; a machine with no
    # local model simply has no local row. Always last, whatever the lineage: a small local model
    # cannot judge code (Crupi et al. 2025, CodeJudgeBench 2507.10535).
    {"family": "local", "label": "Local", "runners": ()},
)


def _local_entries(local_models) -> list[tuple[str, str, str]]:
    """Normalize `local_models` to (preset id, model, label). Accepts ids, pairs and dicts."""
    out: list[tuple[str, str, str]] = []
    for item in local_models or ():
        if isinstance(item, str):
            out.append((item, "", item))
        elif isinstance(item, dict):
            endpoint = str(item.get("id") or "")
            out.append((endpoint, str(item.get("model") or ""), str(item.get("label") or endpoint)))
        elif isinstance(item, (tuple, list)) and item:
            endpoint = str(item[0])
            model = str(item[1]) if len(item) > 1 else ""
            label = str(item[2]) if len(item) > 2 else (model or endpoint)
            out.append((endpoint, model, label))
    return [entry for entry in out if entry[0]]


def runner_model(runner: str, local_models=()) -> str:
    """The model id a runner would actually run: the guest's harness name, or the preset's Main."""
    kind, _, name = (runner or "").partition(":")
    if kind == "guest":
        return GUEST_MODELS.get(name, name)
    if name.startswith("local:"):
        for endpoint, model, _label in _local_entries(local_models):
            if endpoint == name:
                return model or name
        return name
    row = presets.TIER_DEFAULTS.get(name, {}).get("main")
    if row:
        return row[1]
    preset = presets.PRESETS.get(name)
    return preset.model if preset is not None else name


def runner_label(runner: str, local_models=()) -> str:
    known = RUNNER_LABELS.get(runner)
    if known:
        return known
    _kind, _, name = (runner or "").partition(":")
    for endpoint, _model, label in _local_entries(local_models):
        if endpoint == name:
            return label or name
    return runner_model(runner, local_models) or runner


def _runner_available(runner: str, installed_guests, keys, hosted_ok: bool, local_models) -> bool:
    kind, _, name = (runner or "").partition(":")
    if kind == "guest":
        return name in set(installed_guests or ())
    if name == "relay-free":
        return bool(hosted_ok)
    if name.startswith("local:"):
        return any(endpoint == name for endpoint, _m, _l in _local_entries(local_models))
    return bool((keys or {}).get(name))


def _missing(runner: str) -> str:
    """Why a runner is not available, in the words a person can act on."""
    kind, _, name = (runner or "").partition(":")
    if kind == "guest":
        return f"{name} not on PATH"
    if name == "relay-free":
        return "Relay Free is off"
    if name.startswith("local:"):
        return "no local endpoint"
    return f"no {name} key"


def _why_available(runner: str) -> str:
    """The one-word reason a runner *is* available, for the card line: "(installed)", "(key)"."""
    kind, _, name = (runner or "").partition(":")
    if kind == "guest":
        return "installed"
    if name == "relay-free":
        return "included, no key"
    if name.startswith("local:"):
        return "on this machine"
    return "key"


def _rows_for(implementer_family: str, local_models) -> tuple[list[dict], list[dict]]:
    """(rows to try in order, rows skipped) for an implementer family.

    The order the card's design fixes: (a) the implementer's own family is skipped outright;
    (b) every other lineage first, in `VERIFIER_RANK` order; (c) the implementer's own lineage
    after them, still offered; (d) Relay Free sits wherever its upstream's lineage puts it;
    (e) a local endpoint last whatever its lineage, because a model small enough to serve here is
    below the capability floor for judging code.
    """
    rows, skipped = [], []
    group = lineage(implementer_family)
    for row in VERIFIER_RANK:
        entry = dict(row)
        if entry["family"] == "relay-free":
            model, label = relay_free_upstream(runner_model("preset:relay-free"))
            # The row is judged as its upstream, and says so, so nobody has to know the routing.
            entry["family"] = family(model)
            entry["label"] = f"Relay Free ({label})"
            entry["via"] = "relay-free"
        if entry["family"] == "local":
            entry["runners"] = tuple(f"preset:{endpoint}"
                                     for endpoint, _m, _l in _local_entries(local_models))
            if not entry["runners"]:
                continue
        if implementer_family and entry["family"] == implementer_family:
            why = ("implemented this card" if not entry.get("via") else
                   f"routes to the implementer's family ({FAMILY_LABELS.get(implementer_family, implementer_family)})")
            skipped.append({"family": entry["family"], "label": entry["label"], "why": why})
            continue
        rows.append(entry)
    local = [r for r in rows if r["family"] == "local"]
    rest = [r for r in rows if r["family"] != "local"]
    if group:
        rest = ([r for r in rest if lineage(r["family"]) != group]
                + [r for r in rest if lineage(r["family"]) == group])
    return rest + local, skipped


def recommend(implemented_by: str | None, *, installed_guests=(), keys=None, hosted_ok: bool = True,
              local_models=()) -> dict:
    """Who should verify a card its `implemented_by` says was written by `implemented_by`.

    Returns the `qa` object of protocol 19.15, minus `commits`: `implemented_by`,
    `implementer_family`, `recommended` (or None when nothing on this machine can verify),
    `alternates`, `skipped` and `unavailable`.  Pure: availability comes in as arguments, so a test
    never touches PATH or the keyring.  `keys` is `{preset id: bool}` as `keystore.available()`
    returns it; `installed_guests` is a set of guest ids; `local_models` is what
    `localmodels.catalog()` holds.
    """
    keys = dict(keys or {})
    implementer = (implemented_by or "").strip()
    implementer_family = family(implementer)
    rows, skipped = _rows_for(implementer_family, local_models)
    group = lineage(implementer_family)

    offers: list[dict] = []
    unavailable: list[dict] = []
    for row in rows:
        runner = next((r for r in row["runners"]
                       if _runner_available(r, installed_guests, keys, hosted_ok, local_models)), None)
        if runner is None:
            reasons = []
            for candidate in row["runners"]:
                reason = _missing(candidate)
                if reason not in reasons:
                    reasons.append(reason)
            # Two stored-key runners for one family read as one missing key, not two.
            if reasons and all(r.startswith("no ") and r.endswith(" key") for r in reasons):
                reasons = ["no key"]
            unavailable.append({"family": row["family"], "label": row["label"],
                                "why": ", ".join(reasons) or "no runner"})
            continue
        entry = {"family": row["family"], "label": row.get("label") or FAMILY_LABELS.get(row["family"], row["family"]),
                 "runner": runner, "model": runner_model(runner, local_models),
                 "available": _why_available(runner),
                 "same_lineage": bool(group) and lineage(row["family"]) == group}
        if row["family"] != "local" and runner in RUNNER_LABELS and "via" not in row:
            entry["label"] = RUNNER_LABELS[runner]
        if row.get("via"):
            entry["via"] = row["via"]
        if row["family"] == "local":
            entry["label"] = runner_label(runner, local_models)
        offers.append(entry)

    out = {"implemented_by": implementer, "implementer_family": implementer_family,
           "recommended": None, "alternates": [], "skipped": skipped, "unavailable": unavailable}
    if not offers:
        return out
    first, rest = offers[0], offers[1:]
    if not implementer_family:
        first["why"] = ("the implementer is unknown, so this is the first verifier in the ranking "
                        f"that is available here ({first['available']})")
    elif first["same_lineage"]:
        first["why"] = ("only verifier available; same lineage as the implementer "
                        f"({group}), so a weaker check")
    else:
        first["why"] = ("first available verifier outside the implementer's lineage "
                        f"({group or 'unknown'})")
    for entry in rest:
        entry["why"] = (f"same lineage as the implementer ({group}): shares training data, "
                        "weaker check" if entry["same_lineage"] else
                        f"next in the ranking and available here ({entry['available']})")
    for entry in [*offers]:
        if entry["family"] == "local":
            entry["why"] = ("offered last: a model small enough to serve here is below the "
                            "capability floor for judging code (Crupi et al. 2025, CodeJudgeBench)")
    out["recommended"] = first
    out["alternates"] = rest
    # One line the GUI can show when the best this machine can do is a weak check.
    if first["same_lineage"]:
        out["note"] = (f"The recommended verifier shares the implementer's lineage ({group}): it "
                       "shares training data, so it is a weaker check than a different lineage.")
    elif first["family"] == "local":
        out["note"] = ("The only verifier available is a local model, which is below the capability "
                       "floor for judging code: read its verdict as a hint, not a pass.")
    return out


def summary_line(result: dict, card_id: str = "") -> str:
    """The recommendation as one line, for `relay-board.py verifier` and the card detail.

    "Verify #K7Q2 with Codex (installed) · then GLM-5.3 (key) · skipped Claude: implemented this ·
    unavailable Kimi: no key"
    """
    what = f"Verify #{card_id.lstrip('#').upper()}" if card_id else "Verify"
    best = result.get("recommended")
    parts = [f"{what} with {best['label']} ({best['available']})" if best else
             f"{what}: no verifier is available on this machine"]
    alternates = result.get("alternates") or []
    if alternates:
        parts.append("then " + " · ".join(f"{a['label']} ({a['available']})" for a in alternates))
    for entry in result.get("skipped") or []:
        parts.append(f"skipped {entry['label']}: {entry['why']}")
    missing = result.get("unavailable") or []
    for entry in missing[:2]:
        parts.append(f"unavailable {entry['label']}: {entry['why']}")
    if len(missing) > 2:
        parts.append(f"and {len(missing) - 2} more unavailable")
    return " · ".join(parts)


# ------------------------------------------------------------------ this machine's probe

_CACHE: tuple[float, dict] | None = None


def availability(cache_seconds: float = 60.0) -> dict:
    """What this machine can actually run: `{installed_guests, keys, hosted_ok, local_models}`.

    Three probes, all local: `shutil.which` over the guest registry's binary names (never a version
    subprocess — a hung CLI must not stall a board read), `keystore.available()` for the stored keys
    (which honours `RELAY_KEYRING=off`, so a test run never touches the desktop keyring), and the
    local-endpoint registry.  Cached for a minute, because `keystore.available()` shells out once
    per preset and a card detail must not pay that every time it is opened.
    """
    global _CACHE
    now = time.monotonic()
    if _CACHE is not None and cache_seconds > 0 and now - _CACHE[0] < cache_seconds:
        return dict(_CACHE[1])
    from . import guest                      # imported late: guest imports nothing from here
    installed = set()
    for spec in guest.GUESTS:
        if any(shutil.which(name) for name in spec.binaries):
            installed.add(spec.id)
    try:
        from . import keystore
        keys = keystore.available()
    except Exception:                        # pragma: no cover - a broken keyring is "no keys"
        keys = {}
    local: list[tuple[str, str, str]] = []
    try:
        from . import localmodels
        local = [(endpoint.id, endpoint.model, endpoint.label)
                 for endpoint in localmodels.catalog().values()]
    except Exception:                        # pragma: no cover - an unreadable registry is "none"
        local = []
    out = {"installed_guests": installed, "keys": keys, "hosted_ok": True,
           "local_models": tuple(local)}
    _CACHE = (now, out)
    return dict(out)


def recommend_here(implemented_by: str | None, cache_seconds: float = 60.0) -> dict:
    """`recommend()` with this machine's availability."""
    return recommend(implemented_by, **availability(cache_seconds))


# --------------------------------------------------------------------- commit trailers

TRAILER = "Implemented-By"
VERIFIED_TRAILER = "Verified-By"
_GIT_TIMEOUT = 10.0


def _git(repo, args: list[str]) -> str:
    """`git -C <repo> …`, or "" when git, the repo or the object is not there."""
    try:
        run = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                             timeout=_GIT_TIMEOUT)
    except (OSError, subprocess.SubprocessError):
        return ""
    return run.stdout if run.returncode == 0 else ""


def _trailer_of(message: str, name: str = TRAILER) -> str:
    for line in message.splitlines():
        head, sep, value = line.partition(":")
        if sep and head.strip().lower() == name.lower():
            return value.strip()
    return ""


def commit_trailers(repo, hashes, expected: str | None = None, name: str = TRAILER) -> list[dict]:
    """`[{hash, trailer, agrees}]` for each commit, from its `Implemented-By:` trailer.

    `agrees` is None when either side is unknown; otherwise it is whether the trailer's family
    matches `expected`'s — a commit signed by a different family than the card claims is the thing
    worth showing, and it is the only reason this reads git at all.  A missing repo, a bad hash or
    no git at all yields `trailer: ""`, never an exception: the `qa` block is advisory.
    """
    want = family(expected) if expected else ""
    out = []
    for value in list(hashes or [])[:20]:
        text = str(value).strip()
        if not text:
            continue
        body = _git(repo, ["log", "-1", "--format=%B", text])
        trailer = _trailer_of(body, name)
        agrees = None if not trailer or not want else family(trailer) == want
        out.append({"hash": text[:40], "trailer": trailer, "agrees": agrees})
    return out


def commits_for(repo, card_id: str, limit: int = 50) -> list[str]:
    """The commit hashes whose message mentions `#ID`, newest first and bounded."""
    ident = (card_id or "").strip().lstrip("#").upper()
    if not ident:
        return []
    text = _git(repo, ["log", f"-n{max(1, int(limit))}", "--format=%h", f"--grep=#{ident}"])
    return [line.strip() for line in text.splitlines() if line.strip()]


def card_commits(repo, card_id: str, links: dict | None = None, expected: str | None = None,
                 limit: int = 50) -> list[dict]:
    """The commit rows of the `qa` block: `links.commits` first, then `git log --grep '#ID'`."""
    listed = [str(h).strip() for h in ((links or {}).get("commits") or []) if str(h).strip()]
    found = commits_for(repo, card_id, limit)
    ordered: list[str] = []
    for value in [*listed, *found]:
        # `links.commits` may hold a full hash where `git log --format=%h` gives a short one, so
        # two spellings of one commit are the same commit: compare by prefix, keep the first.
        if any(value.startswith(seen) or seen.startswith(value) for seen in ordered):
            continue
        ordered.append(value)
    return commit_trailers(repo, ordered[:20], expected)

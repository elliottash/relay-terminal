# SPDX-License-Identifier: AGPL-3.0-or-later
"""Model roles: one configurable model per job (docs/AGENT-SESSIONS-PROTOCOL.md section 13).

Every role defaults to "same as the main agent". A role may instead name a built-in preset, or a
custom ``base_url``/``model``/``extra``, plus an optional effort. Keys are never sent over the
protocol: a role resolves its key through the keystore (environment variable, then the desktop
keyring) for the preset that matches its endpoint. The main agent's key is reused when a role lands
on the main agent's preset, so switching roles inside one provider never touches the keyring.

A role whose key is missing is never a hard failure: it falls back to the main agent and the
resolver records a one-line warning, which the worker emits in ``model_roles``.
"""
from __future__ import annotations

import copy
import random
import time
import urllib.parse
from dataclasses import dataclass, replace

from .presets import (EFFORT_LADDER, PRESETS, TIER_LABELS, TIERS, apply_effort, effort_levels,
                      effort_style,
                      match_preset, model_efforts, model_extra, model_name, nearest_effort,
                      openrouter_twin, provider_tier_model, tier_default,
                      tier_fallbacks, validate_effort, validate_tier)
from .provider import MIN_OUTPUT_TOKENS, ProviderConfig
from . import customproviders, hosted, localmodels, relay_pro


def _hostname(base_url: str) -> str:
    """The host a preset's endpoint lives on, for "two keys, one service" (card #G9VE)."""
    try:
        return (urllib.parse.urlsplit(base_url or "").hostname or "").lower()
    except ValueError:
        return ""


# The preset whose key the "same model on OpenRouter" failover spends (owner, 2026-09-20).
OPENROUTER_PRESET_ID = "openrouter"
# Relay's own hosted allowance, which a background job falls through to (`_relay_free_role`).
HOSTED_PRESET_ID = "relay-free"


def _hosted(preset_id) -> bool:
    """Whether a preset id names Relay's own hosted service (the relay-free preset)."""
    preset = PRESETS.get(preset_id) if isinstance(preset_id, str) else None
    return bool(preset is not None and preset.hosted)


def _preset(preset_id):
    """A built-in preset, a saved model server on this machine (localmodels.py) or a saved custom
    provider (customproviders.py), all in the same shape."""
    if not preset_id:
        return None
    found = PRESETS.get(preset_id)
    if found is None and localmodels.is_local_id(preset_id):
        endpoint = localmodels.find(preset_id)
        found = endpoint.as_preset() if endpoint is not None else None
    if found is None and customproviders.is_custom_id(preset_id):
        entry = customproviders.find(preset_id)
        found = entry.as_preset() if entry is not None else None
    return found

# Protocol names. "switchboard" is stored and resolved even though the Board itself is not
# built yet (owner decision 2026-09-17); "route_assist" keeps its own fast default.
# "summaries", "suggestions" and "audit" were split out of "flash"/"chores" on 2026-09-17 so the roles
# modal's Advanced list can name one action per row; their defaults resolve exactly as before.
# "local" (2026-09-18) is the pane role /local switches to, the way "flash" is the one /flash
# switches to: a role a pane runs, not a job some side call does.
# "planning" (2026-09-19) serves plan-mode turns. Its default is the pane's own model pushed to
# max reasoning, so a plan is investigated harder without switching the pane's own model. From
# 2026-09-20 it followed the High tier instead, but once the tier lists are filled by defaults
# that rerouted every plan turn to another provider (card #HR5E), so the owner put it back on
# 2026-09-21: only a hand-pinned `roles.planning` entry routes a plan turn off the pane's model.
# Since 2026-09-22 (owner: "its supposed to go into /high"; "it doesnt need to be the same model")
# entering plan mode puts the pane on /high (#PH9G), so the unpinned default adds nothing on top:
# no per-turn effort boost, no swap. The High list decides the model and its level.
# "high" (2026-09-21, card #MDL1) is the pane role /high switches to, exactly as "flash" is the
# one /flash switches to and "local" the one /local does: the box's modes are high, main, flash and
# local, so each of the three that is not the pane's own model needs a role a pane can be put on.
ROLES = ("main", "terminal_use", "subagent", "switchboard", "high", "flash", "local", "planning",
         "summaries", "suggestions", "chores", "audit", "loop_check", "vision", "route_assist")
# The role the helper worker runs (protocol 30.7): one per tab, serving the Board and the
# Options, Actions and Sessions panes. Named because it is the one role that may not run on a
# guest harness (`leave_guest`, card #GH5T) — its whole job is Relay's own tools.
HELPER_ROLE = "switchboard"
SETTABLE = tuple(r for r in ROLES if r != "main")
# Lower-case, and the same words as the GUI's one table (src/ModelRows.cpp `roleLabel`): a role
# is named the same way wherever it is printed (card #MDL1, rule 1). These reach a person in the
# warning a role with no key raises — "flash: no stored key for kimi; using main."
LABELS = {"main": "main", "terminal_use": "terminal use", "subagent": "subagents",
          # "switchboard" keeps its protocol name — settings, the model box (#BRD3), its Options ›
          # Models row and its Main default are untouched — and is the word for the job since
          # card #FEJQ (owner decision 4, 2026-09-20): one helper worker per tab now serves the
          # Board *and* the Options, Actions and Sessions panes, so the label names the job
          # rather than the one pane it started in (protocol 30.7).
          "switchboard": "helpers", "high": "high", "flash": "flash",
          "local": "local",
          "planning": "plan mode", "summaries": "summaries", "suggestions": "suggestions",
          "chores": "chores", "audit": "request audit", "loop_check": "loop check",
          "vision": "vision", "route_assist": "route assist"}

# The pane-agent role was called "fast" until 2026-09-18. It is renamed to "flash" so the one word
# names the tier, the role and the /flash command, and so nothing in Relay says "fast" — in Codex and
# Claude Code /fast means the same model served faster, which is close to the opposite trade.
# Settings, saved layouts and user subagent definitions on disk still carry the old name, so it is
# accepted wherever a role is read and normalized on the way in. Nothing writes it.
DEPRECATED_ROLES = {"fast": "flash"}


def canonical_role(role):
    """The current name for a role, translating names Relay used to write."""
    return DEPRECATED_ROLES.get(role, role) if isinstance(role, str) else role

# The roles modal's Advanced list: one row per thing a model actually does, in display order, named
# after the job rather than the protocol id (owner, 2026-09-17). The GUI mirrors this table.
ACTIONS: tuple[tuple[str, str, str], ...] = (
    ("main", "Agent turns", "the main conversation in this pane"),
    ("planning", "Plan mode", "investigating and writing plans; plan mode puts the pane on /high unless overridden here"),
    ("high", "Panes on the High tier (/high)", "the hardest turns; empty, the pane's own model at its top level"),
    ("subagent", "Subagents", "agents the main agent starts"),
    ("terminal_use", "Driving programs in the terminal", "answering prompts, fixing failed commands"),
    ("flash", "New panes (Flash agent)", "panes that open on the Flash agent"),
    ("local", "Panes on the Local agent (/local)", "a model served on this machine; no key, nothing leaves it"),
    ("suggestions", "Next-command and next-prompt suggestions",
     "sends recent command output, so it stays on your own provider"),
    ("summaries", "Summaries, compaction and recaps", "condensing the conversation"),
    ("switchboard", "Helper agent", "the Board's agent, and the helper in Options, "
     "Actions and Sessions"),
    ("chores", "Chores: duplicate checks, labels, titles, note scans", "small structured judgements"),
    ("audit", "Request audit", "flags asks that may be unaddressed after a turn"),
    ("loop_check", "Loop check", "asked only when a turn repeats itself, before it is stopped"),
    ("vision", "Images and vision turns", "used when the main model cannot read images"),
    ("route_assist", "Command routing", "decides shell or agent for one line of input"),
)

MAX_MODEL = 200
MAX_URL = 400

# --- built-in defaults (issues/features/2026-09-17-model-roles-and-fast-agent.md) -------------------
# Roles are grouped into tiers (presets.TIERS, protocol 13.7): a role either follows the pane's own
# model ("main"), the High tier above it (main at max reasoning unless `tiers.high` picks a model;
# owner, 2026-09-20), or takes the provider's Flash or Lite model. "planning" is not tiered (None):
# unpinned it is the pane as it is, because entering plan mode puts the pane on /high (#PH9G).
ROLE_TIERS: dict[str, str | None] = {
    "planning": None, "high": "high",
    "main": "main", "subagent": "main", "switchboard": "main",
    "terminal_use": "flash", "flash": "flash", "summaries": "flash", "suggestions": "flash",
    "chores": "lite", "audit": "lite", "loop_check": "lite", "local": "local",
    # Vision and route assist are not tiered: they have their own fixed defaults below.
    "vision": None, "route_assist": None,
}
# Vision turns on presets without image support: GLM-5.3 Flash for GLM, unset elsewhere.
VISION_DEFAULTS: dict[str, tuple[str, str, dict]] = {
    "glm": ("glm", "glm-5.3-flash", {}),
    "glm-coding": ("glm-coding", "glm-5.3-flash", {}),
    # DeepSeek is the same shape: Pro is text-only and the Flash line reads images
    # (https://api-docs.deepseek.com/quick_start/pricing), so an image turn goes to Flash and back.
    "deepseek": ("deepseek", "deepseek-flash", {}),
}
# Route assist keeps its fixed fast model whatever the tiers say: the routing budget is under a second
# and Gemini 3.5 Flash-Lite measured 0.5-0.6 s against 2.3-4.9 s for 3.8 Flash. See route_assist.py.
ROUTE_ASSIST_DEFAULT = ("openrouter", "google/gemini-3.5-flash-lite", {})
# Where routing goes when that OpenRouter key is not stored, per main preset. A pane on Relay Free
# has no key at all, and routing on the main model would spend the day's allowance on a job the
# gateway's Lite role exists for; with an OpenRouter key present the default above still wins.
ROUTE_ASSIST_DEFAULTS: dict[str, tuple[str, str, dict]] = {
    "relay-free": ("relay-free", "relay-lite", {}),
}


def tier_catalog() -> dict:
    """The per-provider tier table for the roles modal, sent with the ``presets`` event.

    Data only: labels, hints, the recommended pairings and, per provider, the model each tier picks.
    ``tiers`` is in display order, High first. ``providers`` covers the three provider tiers only:
    the High tier has no per-provider row (its default is the pane's own model at max reasoning),
    and the Local tier belongs to no provider — the endpoints it can name are the ``presets`` rows
    with ``local: true`` in the same event.
    """
    from .presets import RECOMMENDED, TIER_DEFAULTS, TIER_HINTS
    return {
        "tiers": [{"id": tier, "label": TIER_LABELS[tier], "hint": TIER_HINTS[tier]} for tier in TIERS],
        "recommended": [list(pair) for pair in RECOMMENDED],
        "providers": {provider: {tier: {"preset": entry[0], "model": entry[1]}
                                 for tier, entry in table.items()}
                      for provider, table in TIER_DEFAULTS.items()},
    }


def action_catalog() -> list[dict]:
    """The roles modal's Advanced list: one row per job, with the tier it follows by default."""
    return [{"role": role, "label": label, "hint": hint, "tier": ROLE_TIERS.get(role),
             "settable": role != "main"}
            for role, label, hint in ACTIONS]


def _text(value, name: str, limit: int) -> str:
    if not isinstance(value, str):
        raise ValueError(f"roles: {name} must be text.")
    value = value.strip()
    if len(value) > limit:
        raise ValueError(f"roles: {name} is too long.")
    return value


def validate_role(role) -> str:
    role = canonical_role(role)
    if role not in ROLES:
        raise ValueError("Unknown model role: " + repr(role)[:60] + ".")
    return role


def validate_roles(raw) -> dict[str, dict]:
    """Normalize the ``roles`` object from configure / set_agent_options.

    ``null``, ``{}`` and ``{"inherit": true}`` all mean "same as the main agent" and are dropped,
    so an empty result means every role follows the main agent.
    """
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise ValueError("roles must be an object.")
    out: dict[str, dict] = {}
    for name, value in raw.items():
        name = validate_role(name)
        if name == "main":
            raise ValueError('The "main" role is the pane\'s own model; set it with configure or set_model.')
        if value is None:
            continue
        if not isinstance(value, dict):
            raise ValueError(f"roles.{name} must be an object or null.")
        unknown = set(value) - {"preset", "base_url", "model", "extra", "effort", "inherit", "tier", "candidates"}
        if unknown:
            raise ValueError(f"roles.{name}: unknown field {sorted(unknown)[0]!r}.")
        if value.get("inherit") is True:
            continue
        entry: dict = {}
        if "candidates" in value:
            if name not in {"planning", "subagent", "switchboard"}:
                raise ValueError(f"roles.{name}: ranked candidates are not available for this job.")
            if any(value.get(field) is not None
                   for field in ("preset", "base_url", "model", "extra", "effort", "tier")):
                raise ValueError(f"roles.{name}: give candidates or one override, not both.")
            raw_candidates = value["candidates"]
            if not isinstance(raw_candidates, list):
                raise ValueError(f"roles.{name}.candidates must be a list.")
            tier = "high" if name == "planning" else "main"
            candidates = []
            for item in raw_candidates[:MAX_TIER_ENTRIES * 4]:
                candidate = _list_entry(tier, item)
                if candidate is not None and _same_target(candidate) not in [_same_target(e) for e in candidates]:
                    candidates.append(candidate)
            if candidates:
                out[name] = {"candidates": candidates[:MAX_TIER_ENTRIES]}
            continue
        if value.get("tier") is not None:
            # A tiered role takes the High/Main/Flash/Lite model (protocol 13.7). It is exclusive
            # with a hand-picked endpoint, so the two can never disagree.
            if any(value.get(field) is not None for field in ("preset", "base_url", "model", "extra")):
                raise ValueError(f"roles.{name}: give a tier or an endpoint, not both.")
            entry["tier"] = validate_tier(value["tier"])
            if value.get("effort") is not None:
                entry["effort"] = validate_effort(value["effort"])
            out[name] = entry
            continue
        preset = value.get("preset")
        if preset is not None:
            preset = _text(preset, f"{name}.preset", 64)
            if preset and _preset(preset) is None:
                raise ValueError(f"roles.{name}: unknown preset {preset!r}.")
            if preset:
                entry["preset"] = preset
        if value.get("base_url") is not None:
            entry["base_url"] = _text(value["base_url"], f"{name}.base_url", MAX_URL)
        if value.get("model") is not None:
            entry["model"] = _text(value["model"], f"{name}.model", MAX_MODEL)
        if value.get("extra") is not None:
            if not isinstance(value["extra"], dict):
                raise ValueError(f"roles.{name}.extra must be an object.")
            entry["extra"] = copy.deepcopy(value["extra"])
        if value.get("effort") is not None:
            entry["effort"] = validate_effort(value["effort"])
        if not entry:
            continue
        if "preset" not in entry and not (entry.get("base_url") and entry.get("model")):
            raise ValueError(f"roles.{name}: give a preset, or both base_url and model.")
        out[name] = entry
    return out


MAX_TIER_ENTRIES = 32
GUEST_PRESET_PREFIX = "guest:"      # guest_harness_provider.PRESET_PREFIX, not imported: it pulls in the guest stack
GUEST_BASE_SCHEME = "harness://"    # guest_harness_provider.BASE_SCHEME, likewise
# The output budget a guest's config carries, for the context arithmetic only
# (guest_harness_provider._guest_max_tokens): the guest decides its own reply length.
GUEST_MAX_TOKENS = max(MIN_OUTPUT_TOKENS, 32_768)
# The tiers a guest entry may serve (protocol 13.7): Main as a pane's own agent, High for a plan
# turn, which starts the guest's harness for that one turn (`Agent._begin_plan_turn`), and — since
# 2026-09-21, at the owner's ask ("the worker should allow the harness for flash, and defaults
# should be the same across plans / apis / harnesses") — Flash, for a pane that is *on* the Flash
# tier. Lite and Local are never a guest's: Lite is nothing but background jobs and Local is a
# model server on this machine.
#
# Flash is the tier where the distinction matters, because it is both a pane and a set of chores.
# A /flash pane is a conversation the harness can own from its first turn; terminal use, summaries
# and suggestions are side calls *into* a conversation that is already running somewhere else, and
# a harness — a whole agent of its own, with its own transcript — cannot be handed one mid-way.
# BACKGROUND_ROLES is that line, and `_tier` skips a guest entry for every role on it.
GUEST_TIERS = ("main", "high", "flash")


# The roles on the Flash and Lite tiers that are **not** a pane's own turn: the jobs Relay does
# around a conversation rather than in it (owner, 2026-09-21). Two rules are theirs alone:
#
# * a guest entry of their tier's list is skipped, and the next entry taken instead — a harness
#   cannot serve a side call (GUEST_TIERS above);
# * when nothing in their list can take them at all, they fall through to Relay Free's role for
#   that tier rather than to the pane's own model. The owner asked for exactly this: "so if
#   somebody just has a harness, the flash chores run on relay flash?" — "i agree", "and yes to
#   the rule on the relay free fallbacks as well". A pane's own turn never does this: /flash on a
#   provider with no key is the user's business to fix, and silently moving their conversation
#   onto Relay's allowance is not the same kind of act as running a title through it.
#
# Derived from ROLE_TIERS so a role added to either tier is covered without a second list to keep:
# everything on flash and lite except `flash` itself, which is the pane role /flash switches to.
BACKGROUND_ROLES = tuple(role for role, tier in ROLE_TIERS.items()
                         if tier in ("flash", "lite") and role != "flash")


def is_guest_preset(preset_id) -> bool:
    """Whether a preset id names a guest harness (Claude Code, Codex; protocol 29.3)."""
    return isinstance(preset_id, str) and preset_id.startswith(GUEST_PRESET_PREFIX) \
        and len(preset_id) > len(GUEST_PRESET_PREFIX)


def guest_id_of(preset_id) -> str:
    """`"codex"` for `"guest:codex"`, `"claude:work"` for a registered account's preset
    (guest_accounts, #M8S2); "" for anything that is not a guest preset. This key is what a login
    answer and a usage figure belong to."""
    return preset_id[len(GUEST_PRESET_PREFIX):] if is_guest_preset(preset_id) else ""


def guest_base_url(preset_id) -> str:
    """`harness://claude`, or `harness://claude/work` for an account's preset: the base URL
    `guest_harness_provider.base_url` gives, spelled here without importing the guest stack."""
    family, _, account = guest_id_of(preset_id).partition(":")
    return GUEST_BASE_SCHEME + family + ("/" + account if account else "")


def guest_runnable(guest_id: str) -> bool:
    """Whether this guest's harness can be started here: the adapter imports, the CLI is on PATH,
    and it has not said it is signed out (protocol 29.3). The default `guest_check` of a
    RoleResolver; imported lazily because the guest stack is heavy and most resolvers never ask.
    Any failure is "cannot run", never an error out of a role resolution."""
    if not guest_id:
        return False
    try:
        from . import guest_accounts, guest_harness_provider as ghp
        family, account = guest_accounts.split_key(guest_id)
        if account and guest_accounts.find(family, account) is None:
            return False                # a removed account never falls back to another login
        return bool(ghp.adapter_available(family)
                    and (ghp.installations().get(family) or {}).get("installed")
                    and ghp.login_status(guest_id) is not False)
    except Exception:
        return False


def validate_tiers(raw) -> dict[str, list[dict]]:
    """Normalize the ``tiers`` object from configure / set_agent_options (protocol 13.7) into
    ``{tier: [entry…]}``, each tier an ordered priority list (owner, 2026-09-20).

    ``{"main": […], "high": […], "flash": […], "lite": […], "local": […]}`` with
    ``entry = {"preset": id, "model": id or "", "effort": level or absent}``. A tier resolves to
    its first usable entry, and a failover walks the list the turn is on, so order is the meaning.
    The GUI sends whatever sits in its lists, so **a list never raises**: an entry that cannot be
    read — not an object, no preset, a preset nobody knows, a Local entry that is not on this
    machine — is dropped, an effort that is not a level is dropped from its entry, an entry
    repeated lower down is dropped (it could never be reached), and an empty list is the same as
    no list. A tier name Relay does not know is ignored. ``main`` is a list like the others, but
    it never picks the pane's model (``configure`` / ``set_model`` do): it is the order a failing
    Main turn walks.

    The form before 2026-09-20 — one object per tier, ``{"flash": {"preset": "glm"}}`` — is still
    accepted and is a one-element list. It stays as strict as it was (an unknown preset or field
    is a ``ValueError``), because the GUIs that send it show that sentence; ``null`` restores the
    tier's built-in default — for ``high``, the pane's own model at max reasoning.
    """
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise ValueError("tiers must be an object.")
    out: dict[str, list[dict]] = {}
    for name, value in raw.items():
        if name not in TIERS or value is None:
            continue
        if isinstance(value, (list, tuple)):
            entries: list[dict] = []
            for item in value[:MAX_TIER_ENTRIES * 4]:
                entry = _list_entry(name, item)
                if entry is not None and _same_target(entry) not in [_same_target(e) for e in entries]:
                    entries.append(entry)
            entries = entries[:MAX_TIER_ENTRIES]
        elif isinstance(value, dict):
            entry = _strict_entry(name, value)
            entries = [entry] if entry else []
        else:
            raise ValueError(f"tiers.{name} must be a list, an object or null.")
        if entries:
            out[name] = entries
    return out


def _same_target(entry: dict) -> tuple:
    """What makes two entries of one list the same place to send a turn: the level is not part of
    it, because a provider is asked once per turn whatever level the second mention names."""
    return entry.get("preset"), entry.get("base_url"), entry.get("model", "")


def _list_entry(tier: str, value) -> dict | None:
    """One entry of a tier list, or None when it cannot be read. Never raises (validate_tiers)."""
    if not isinstance(value, dict):
        return None
    preset = value.get("preset")
    preset = preset.strip() if isinstance(preset, str) else ""
    if len(preset) > 64:
        return None
    model = value.get("model")
    model = model.strip() if isinstance(model, str) else ""
    base_url = value.get("base_url")
    base_url = base_url.strip() if isinstance(base_url, str) else ""
    if len(model) > MAX_MODEL or len(base_url) > MAX_URL:
        return None
    guest = is_guest_preset(preset)
    entry: dict = {}
    if preset:
        if not guest and _preset(preset) is None:
            return None                     # a key since deleted is fine; an id nobody knows is not
        entry["preset"] = preset
        entry["model"] = model
    elif base_url and model:
        entry["base_url"], entry["model"] = base_url, model
    else:
        return None
    if isinstance(value.get("extra"), dict) and not guest:
        entry["extra"] = copy.deepcopy(value["extra"])
    rank = value.get("rank")
    if type(rank) is int and 1 <= rank <= 1000:
        entry["rank"] = rank
    effort = value.get("effort")
    # Every level is its provider's own word now (card #MDL1, 2026-09-21), not one of Relay's
    # four, so a list entry keeps what it was written with — "xhigh" on the OpenAI API and codex,
    # "max" on Kimi, "ultra" from a codex model that offers it. This used to drop anything outside
    # EFFORTS, which would now silently throw away half the levels the defaults themselves fill
    # the lists with. Which levels *this* model has is decided where it is resolved, against that
    # model's own list (`presets.validate_effort`).
    if isinstance(effort, str) and 0 < len(effort.strip()) <= 32:
        word = effort.strip().lower()
        if guest:
            entry["effort"] = word          # the CLI's own vocabulary; the harness judges it
        elif not preset:
            # A bare base_url: Relay cannot name the endpoint, so it cannot name its levels
            # either. A word off the ladder is still a typo, and those are dropped as they were.
            if word in EFFORT_LADDER:
                entry["effort"] = word
        else:
            levels = _levels_for(preset, entry.get("model") or "")
            if word in levels:
                entry["effort"] = word
            elif levels and word in EFFORT_LADDER:
                # An older client's word, or one carried over from another provider: the level
                # this model actually has for it (`nearest_effort`), never the word itself.
                entry["effort"] = nearest_effort(word, levels)
            # Anything else — a typo, or a level for a model with no knob — is dropped, not the
            # entry: the model still belongs in the list, it just runs at its own default.
    if tier == "local" and not _is_local_endpoint(entry):
        return None
    return entry


def _usage_weight(preset_id: str | None, now: int | None = None,
                  limits: dict | None = None) -> float | None:
    """Remaining percentage per hour until reset; None means no fresh evidence."""
    if now is None:
        now = int(time.time())
    if limits is None and is_guest_preset(preset_id):
        from . import guest_harness_provider
        limits = guest_harness_provider.last_limits(guest_id_of(preset_id))
    elif limits is None and preset_id in ("glm-coding", "kimi-code"):
        from . import provider_limits
        limits = provider_limits.last(preset_id)
    if not isinstance(limits, dict):
        return None
    updated = limits.get("updated_at")
    if type(updated) not in (int, float) or not 0 <= now - updated <= 1800:
        return None
    rates = []
    for window in limits.get("windows") or ():
        if not isinstance(window, dict):
            continue
        used, reset = window.get("used_percent"), window.get("resets_at")
        if type(used) not in (int, float) or type(reset) not in (int, float) or reset <= now:
            continue
        hours = max((reset - now) / 3600.0, 0.25)
        rates.append(max(0.0, min(100.0, 100.0 - used)) / hours)
    if limits.get("status") == "rejected":
        return 0.0
    return min(rates) if rates else None


def ordered_candidates(entries: list[dict], *, choose: bool = False, draw=None,
                       limits_lookup=None, now: int | None = None) -> list[dict]:
    """Sort ranks, then optionally draw a weighted order within each tied rank."""
    groups: dict[int, list[dict]] = {}
    for index, entry in enumerate(entries, 1):
        rank = entry.get("rank", index)
        if type(rank) is not int or rank < 1:
            rank = index
        groups.setdefault(rank, []).append(entry)
    result = []
    if draw is None:
        draw = random.random
    for rank in sorted(groups):
        group = groups[rank].copy()
        if choose:
            group = [entry for entry in group
                     if _usage_weight(entry.get("preset"), now,
                                      limits_lookup(entry.get("preset")) if limits_lookup else None) != 0]
        while group:
            if not choose or len(group) == 1:
                result.append(group.pop(0))
                continue
            scores = [_usage_weight(e.get("preset"), now,
                                    limits_lookup(e.get("preset")) if limits_lookup else None)
                      for e in group]
            fresh = sorted(score for score in scores if score is not None and score > 0)
            neutral = fresh[len(fresh) // 2] if fresh else 1.0
            weights = [neutral if score is None else score for score in scores]
            if not any(weights):
                result.extend(group)
                break
            position = max(0.0, min(float(draw()), 0.999999999999)) * sum(weights)
            selected = len(group) - 1
            for index, weight in enumerate(weights):
                if position < weight:
                    selected = index
                    break
                position -= weight
            result.append(group.pop(selected))
    return result


def _strict_entry(name: str, value: dict) -> dict:
    """The one-object-per-tier form of before 2026-09-20, validated as it always was."""
    unknown = set(value) - {"preset", "base_url", "model", "extra", "effort"}
    if unknown:
        raise ValueError(f"tiers.{name}: unknown field {sorted(unknown)[0]!r}.")
    entry: dict = {}
    if value.get("preset") is not None:
        preset = _text(value["preset"], f"{name}.preset", 64)
        if preset and _preset(preset) is None and not is_guest_preset(preset):
            raise ValueError(f"tiers.{name}: unknown preset {preset!r}.")
        if preset:
            entry["preset"] = preset
    if value.get("base_url") is not None:
        entry["base_url"] = _text(value["base_url"], f"{name}.base_url", MAX_URL)
    if value.get("model") is not None:
        entry["model"] = _text(value["model"], f"{name}.model", MAX_MODEL)
    if value.get("extra") is not None:
        if not isinstance(value["extra"], dict):
            raise ValueError(f"tiers.{name}.extra must be an object.")
        entry["extra"] = copy.deepcopy(value["extra"])
    if value.get("effort") is not None:
        entry["effort"] = validate_effort(value["effort"])
    if not entry:
        return {}
    if "preset" not in entry and not (entry.get("base_url") and entry.get("model")):
        raise ValueError(f"tiers.{name}: give a preset, or both base_url and model.")
    if name == "local" and not _is_local_endpoint(entry):
        raise ValueError("tiers.local must name a model server on this machine: a saved local "
                         "endpoint id, or a plain http:// base_url on localhost, 127.0.0.1 or "
                         "::1 with its model.")
    return entry


def _levels_for(preset_id, model: str) -> list[str]:
    """The reasoning levels this (provider, model) pair offers, in the provider's own words: the
    catalog row's list where Relay names the model, else the endpoint's, else none at all."""
    levels = model_efforts(preset_id, model)
    if levels is not None:
        return levels
    found = _preset(preset_id)
    return effort_levels(found.effort_style) if found is not None else []


def _file_level(preset_id: str, model: str, tier: str) -> str | None:
    """``model-ranking.md``'s Levels cell for this model in this class, in the words the provider
    about to run it uses, or None when the file says nothing (or there is no knob).

    Keyed by the model's **name**, like every other row of that file, and mapped onto this
    provider's own list because one name can be served two ways (`presets.nearest_effort`).
    """
    from . import model_ranking
    if not preset_id or tier not in model_ranking.CLASSES:
        return None
    listed = model_ranking.load().level(model_name(preset_id, model), tier)
    return nearest_effort(listed, _levels_for(preset_id, model)) if listed else None


def _is_local_endpoint(entry: dict) -> bool:
    """Whether a tier entry points at a model server on this machine (for the Local tier).

    A hosted preset here would be a Local tier that is not local, which is the one thing the tier
    promises, so it is refused rather than quietly accepted.
    """
    preset = entry.get("preset")
    if preset:
        return localmodels.find(preset) is not None
    return localmodels.loopback_http(entry.get("base_url") or "")


@dataclass
class Resolved:
    """One role's effective model. Never carries key material."""
    role: str
    config: ProviderConfig
    preset_id: str | None
    effort: str | None
    source: str                      # main | configured | default | fallback
    warning: str | None = None       # a configured role that could not be used; surfaced in model_roles
    tier: str | None = None          # the High/Main/Flash/Lite/Local tier this role came from, when tiered
    note: str | None = None          # an expected tier step-down, shown inline; never a protocol warning

    @property
    def model(self) -> str:
        return self.config.model

    @property
    def is_main(self) -> bool:
        return self.source in ("main", "fallback")

    def to_dict(self) -> dict:
        out = {"role": self.role, "model": self.config.model, "preset": self.preset_id,
               "base_url": self.config.base_url, "effort": self.effort, "source": self.source,
               "label": LABELS[self.role], "tier": self.tier}
        if self.warning:
            out["warning"] = self.warning
        if self.note:
            out["note"] = self.note
        return out


class RoleResolver:
    """Resolves each role to a ProviderConfig, caching the answer (and its keyring lookups)."""

    def __init__(self, main_config: ProviderConfig, main_preset_id: str | None = None,
                 roles: dict | None = None, *, key_lookup=None, main_effort: str | None = None,
                 tiers: dict | None = None, guest_check=None):
        from . import keystore
        self.main_config = main_config
        main = match_preset(main_config.base_url, main_config.model)
        self.main_preset_id = main_preset_id if _preset(main_preset_id) else (main.id if main else None)
        self.roles = dict(roles or {})
        self.tiers = dict(tiers or {})
        self.key_lookup = key_lookup or keystore.lookup
        # Whether a guest's harness can run here (`guest_runnable`), the guest counterpart of the
        # key lookup: what makes a `guest:` entry of the High list usable. Tests inject it, so
        # nothing here ever looks for a real claude or codex on PATH.
        self.guest_check = guest_check or guest_runnable
        self.main_effort = main_effort
        self._cache: dict[str, Resolved] = {}
        self.warnings: list[str] = []
        # Why the main model is not the one the window names (card #GH5T): the helper worker left
        # a guest harness for a model off the priority list. Shown inline, on the Main tier and on
        # every role that follows it, the way a tier's step-down note is — an expected fallback the
        # user should be able to read, not a protocol warning. None for every ordinary resolver.
        self.main_note: str | None = None

    # ----- helpers ----------------------------------------------------------------------
    def _main(self, role: str, source: str = "main", warning: str | None = None,
              tier: str | None = None, note: str | None = None) -> Resolved:
        return Resolved(role, self.main_config, self.main_preset_id, self.main_effort, source, warning,
                        tier, note or self.main_note)

    def _key_for(self, preset_id: str | None) -> str:
        if localmodels.is_local_id(preset_id) or _hosted(preset_id):
            # A `local:` id is not a keyring name, and keystore refuses it; Relay Free has no key
            # to look up, its transport takes a token (hosted.py) and leaves it in the main
            # config's api_key, which is not a key to hand on.
            return ""
        if preset_id and preset_id == self.main_preset_id and self.main_config.api_key:
            return self.main_config.api_key
        return self.key_lookup(preset_id) if preset_id else ""

    def has_key(self, preset_id: str) -> bool:
        """Whether a tier on this preset can run: a stored key, or Relay Free where it works."""
        if preset_id == "relay-pro":
            return relay_pro.status()["available"]
        if _hosted(preset_id):
            return hosted.available()
        return bool(self._key_for(preset_id))

    def _build(self, role: str, preset_id: str | None, base_url: str, model: str, extra: dict,
               effort: str | None, source: str, tier: str | None = None) -> Resolved:
        key = self._key_for(preset_id)
        # A model server on this machine has no key and needs none. The test is the URL (plain HTTP
        # to a loopback host), never the missing key: an https endpoint without one still falls back.
        local = localmodels.provider_fields(preset_id, base_url, model)
        # Relay Free has none either: its transport takes a token. It is usable only where the
        # token can be made (cryptography imports), and otherwise falls back like a missing key.
        is_hosted = _hosted(preset_id) and self.has_key(preset_id)
        if not key and not local and not is_hosted:
            where = preset_id or base_url
            return self._main(role, "fallback",
                              f"{LABELS[role]}: no stored key for {where}; using main.")
        preset = _preset(preset_id)
        extra = copy.deepcopy(extra or {})
        if effort is not None:
            extra, _ = apply_effort(extra, effort_style(preset, extra, base_url), effort)
        config = ProviderConfig(base_url, model, "" if local else key, extra,
                                localmodels.clamp_max_tokens(self.main_config.max_tokens, local),
                                hosted=is_hosted, **local)
        config.validate()
        return Resolved(role, config, preset_id, effort, source, tier=tier)

    def _configured(self, role: str, entry: dict, *, choose: bool = False,
                    skip_presets=()) -> Resolved:
        if "candidates" in entry:
            tier = "high" if role == "planning" else "main"
            for candidate in ordered_candidates(entry["candidates"], choose=choose):
                preset_id = candidate.get("preset")
                if preset_id in skip_presets:
                    continue
                if is_guest_preset(preset_id):
                    if role == "switchboard" or not self.guest_check(guest_id_of(preset_id)):
                        continue
                    chosen, base_url, model, _extra, effort = self._guest_target(candidate)
                    return self._guest(role, chosen, base_url, model, effort, "configured", tier)
                try:
                    chosen, base_url, model, extra, effort = self._list_target(candidate, tier)
                    resolved = self._build(role, chosen, base_url, model, extra, effort,
                                           "configured", tier)
                    if resolved.source != "fallback":
                        return resolved
                except (KeyError, ValueError):
                    continue
            return self._main(role, "fallback",
                              f"{LABELS[role]}: no ranked model can run here; using main.")
        if entry.get("tier"):
            return self._tier(role, entry["tier"], "configured", entry.get("effort"))
        preset_text = entry.get("preset")
        if isinstance(preset_text, str) and is_guest_preset(preset_text.strip()):
            # A role pinned to a guest harness (protocol 13.7): a plan turn starts that harness
            # for the turn; any other role takes it where a harness may serve (GUEST_TIERS).
            tier = ROLE_TIERS.get(role) or "main"
            if tier not in GUEST_TIERS or role in BACKGROUND_ROLES \
                    or not self.guest_check(guest_id_of(preset_text.strip())):
                return self._main(role, "fallback",
                                  f"{LABELS[role]}: {preset_text.strip()} cannot run here; "
                                  "using the main agent.")
            clean: dict = {"preset": preset_text.strip()}
            if entry.get("model") is not None:
                clean["model"] = _text(entry["model"], "model", MAX_MODEL)
            guest_preset, base_url, model, _extra, guest_effort = self._guest_target(clean)
            return self._guest(role, guest_preset, base_url, model,
                               entry.get("effort") or guest_effort, "configured", tier)
        preset = _preset(entry.get("preset"))
        base_url = entry.get("base_url") or (preset.base_url if preset else "")
        chosen = entry.get("model") or ""
        model = chosen or (preset.model if preset else "")
        if entry.get("extra") is not None:
            extra = entry["extra"]
        elif preset is not None and chosen and chosen != preset.model and preset.id in PRESETS:
            # A role pinned to one of a provider's *other* models runs the request that model runs
            # wherever it is ranked (presets.model_extra) — GLM's Flash model at low reasoning,
            # Kimi's high-speed one with no effort field at all — which is the rule a tier list
            # entry already followed. The helper agent's model box lists a provider's models one by
            # one since card #PK5Q, so this is now the ordinary way a role names a model, not the
            # roles modal's free-text box alone. A value naming no model, or the preset's own, is
            # untouched: it means exactly what it meant before.
            extra = model_extra(preset.id, chosen)
        else:
            extra = dict(preset.extra) if preset else {}
        preset_id = preset.id if preset else (match_preset(base_url, model).id if match_preset(base_url, model) else None)
        effort = entry.get("effort")
        if effort is not None and model_efforts(preset_id, model) == []:
            effort = None           # a model with no effort knob (Kimi's high-speed ones): not sent
        return self._build(role, preset_id, base_url, model, extra, effort, "configured")

    # ----- tiers (protocol 13.7) ---------------------------------------------------------
    def _list_target(self, entry: dict, tier: str) -> tuple[str | None, str, str, dict, str | None]:
        """(preset, base_url, model, extra, effort) for one entry of a tier list."""
        preset = _preset(entry.get("preset"))
        base_url = entry.get("base_url") or (preset.base_url if preset else "")
        model = entry.get("model") or ""
        extra = entry["extra"] if entry.get("extra") is not None else None
        if preset and not model:
            # An entry that names a provider and no model means that provider's model *for this
            # tier* (presets.provider_tier_model): "Flash on Z.AI" is glm-5.3-flash, not glm-5.3.
            # High normally uses Main; Pro has a distinct server route for High.
            try:
                model, tier_extra = provider_tier_model(preset.id, tier)
            except KeyError:        # a custom provider has no tier table: its own model
                model, tier_extra = preset.model, dict(preset.extra)
            if extra is None:
                extra = tier_extra
        if not model and preset:
            model = preset.model
        if extra is None:
            # The extras that model runs with wherever it is ranked (presets.model_extra): the
            # user picks a model and a level, never a request body.
            extra = model_extra(preset.id, model) if preset and preset.id in PRESETS else \
                (dict(preset.extra) if preset else {})
        match = match_preset(base_url, model)
        preset_id = preset.id if preset else (match.id if match else None)
        effort = entry.get("effort")
        if effort is not None and model_efforts(preset_id, model) == []:
            effort = None           # a model with no effort knob (Kimi's high-speed ones): not sent
        return preset_id, base_url, model, extra, effort

    @staticmethod
    def _guest_target(entry: dict) -> tuple[str, str, str, dict, str | None]:
        """(preset, base_url, model, extra, effort) for a `guest:` entry: the harness scheme for a
        base URL, the entry's model ("" is the guest's own default) and its level in the guest's
        own words. No key, no extras — a guest turn is a process on a pipe, not a request body."""
        preset_id = entry["preset"]
        return (preset_id, guest_base_url(preset_id), entry.get("model") or "", {},
                entry.get("effort"))

    def _tier_entries(self, tier: str, guests: bool = True, choose: bool = False) -> list[tuple[str | None, str, str, dict, str | None]]:
        """The (preset, base_url, model, extra, effort) candidates of one tier, in order: the
        user's list when there is one (protocol 13.7), otherwise the one built-in default — the
        default provider's row of TIER_DEFAULTS, or the first saved endpoint for Local. [] when
        there is neither. A guest entry is a candidate only in the tiers a guest may serve
        (GUEST_TIERS: High, where a plan turn starts its harness for the turn) and only while
        ``guests`` is asked for; elsewhere it is left out, because a Flash or Lite call is a side
        call or a per-turn swap of a running conversation, which a harness cannot take."""
        listed = [entry for entry in self.tiers.get(tier) or []
                  if not is_guest_preset(entry.get("preset")) or (guests and tier in GUEST_TIERS)]
        if listed:
            listed = ordered_candidates(listed, choose=choose)
            return [self._guest_target(entry) if is_guest_preset(entry.get("preset"))
                    else self._list_target(entry, tier) for entry in listed]
        if self.tiers.get(tier):
            return []               # a list of nothing but guests: nothing this tier can run on
        if tier == "local":
            # The Local tier belongs to no provider, so there is no TIER_DEFAULTS row to read: with
            # no list it is the first saved endpoint, so one saved server just works.
            endpoint = next(iter(localmodels.catalog().values()), None)
            if endpoint is None:
                return []
            return [(endpoint.id, endpoint.base_url, endpoint.model, dict(endpoint.extra), None)]
        entry = tier_default(self.main_preset_id, tier)
        if entry is None:
            return []
        preset_id, model, extra = entry
        return [(preset_id, PRESETS[preset_id].base_url, model, dict(extra), None)]

    def _high_default(self, role: str, source: str, effort: str | None = None) -> Resolved:
        """The High tier with no ``tiers.high`` override: the pane's own model pushed to max reasoning
        (owner, 2026-09-19 for plan mode; a tier of its own since 2026-09-20).

        The main config is reused as it is — same endpoint, key and preset, whether or not Relay can
        name the preset — with only the effort raised. When the knob cannot move — the provider has
        no effort parameter, or the pane's effort is already there — there is nothing to swap, so
        the role is the main agent, still marked as the High tier.
        """
        if self.main_config.base_url.startswith(GUEST_BASE_SCHEME):
            # A guest pane's own harness has no request-body effort knob: its level is the
            # harness's own word, staged for one turn by `Agent._begin_guest_plan_boost` (#HR5E) —
            # never a bogus `reasoning_effort` written into the harness's config.
            return self._main(role, tier="high")
        style = effort_style(_preset(self.main_preset_id), self.main_config.extra,
                             self.main_config.base_url)
        if effort is None:
            # "Max reasoning" is the model's **top level**, in its provider's own word — `max` on
            # Kimi and GLM, `xhigh` on the OpenAI API, `high` on Gemini, `medium` on Relay Free.
            # It was the literal "max" until 2026-09-21, which only worked because every level
            # was mapped onto Relay's four on the way out; with the level words the provider's
            # own, "max" is a word half the endpoints do not have.
            levels = model_efforts(self.main_preset_id, self.main_config.model)
            if levels is None:
                levels = effort_levels(style)
            effort = levels[-1] if levels else None
        if effort is None:
            return self._main(role, tier="high")        # no knob to turn: the main agent as it is
        raised, _ = apply_effort(self.main_config.extra, style, effort)
        if raised != self.main_config.extra:
            return Resolved(role, replace(self.main_config, extra=raised), self.main_preset_id,
                            effort, source, tier="high")
        return self._main(role, tier="high")

    def _guest(self, role: str, preset_id: str, base_url: str, model: str, effort: str | None,
               source: str, tier: str) -> Resolved:
        """A `guest:` entry of the High list as a role's model (protocol 13.7): a config on the
        harness scheme that `Agent._begin_plan_turn` starts the guest from, at the entry's level in
        the guest's own words. Not `config.validate()`d, for the reason
        `guest_harness_provider.config_for_preset` gives: there is no endpoint and no key here."""
        config = ProviderConfig(base_url, model, "", {}, GUEST_MAX_TOKENS)
        return Resolved(role, config, preset_id, effort, source, tier=tier)

    def _tier(self, role: str, tier: str, source: str, effort: str | None = None,
              guests: bool = True, choose: bool = False) -> Resolved:
        """A tier's model for one role: the **first usable entry** of the tier's list, at that
        entry's level (protocol 13.7) — usable meaning a stored key, a model server on this machine
        or Relay Free where it works, or, for a guest entry of the High list, a harness that can
        run here (`guest_check`). With no list the tier is its one built-in default.

        A tier with nothing usable steps one tier towards Main (Lite → Flash → Main); the Main tier
        is the pane's own model, so this never hard-fails. High with no list is the main model at
        max reasoning (_high_default); with one it is resolved like any other tier, and a list
        with nothing usable steps straight down to Main. ``guests=False`` resolves as if the High
        list had no guest entries: where a plan turn goes when the guest it resolved to could not
        be started after all (`planning_target`).

        A **background role** (BACKGROUND_ROLES: terminal use, summaries, suggestions, chores,
        audit, loop check) differs twice. Its tier's guest entries are skipped whatever
        ``guests`` says — a side call cannot be handed a harness — so it takes the next entry of
        the list instead. And when nothing in its list can take it *and* the pane's own model is a
        harness, which cannot take it either, it lands on Relay Free's role for its own tier
        (`relay-flash`, `relay-lite`) instead of on a "using main" that is no answer at all
        (owner, 2026-09-21: "so if somebody just has a harness, the flash chores run on relay
        flash?" — "i agree"). `_relay_free_role` says why it is that narrow."""
        validate_tier(tier)
        background = role in BACKGROUND_ROLES
        guests = guests and not background
        if tier == "main":
            # The pane's own model, whatever `tiers.main` lists: that list is the order a failing
            # Main turn walks (failover_chain), never a pick.
            return self._main(role, "main", tier="main")
        if tier == "high" and not self.tiers.get("high") and self.main_preset_id != "relay-pro":
            return self._high_default(role, source, effort)
        for candidate in tier_fallbacks(tier):
            if candidate == "main":
                break
            entries = self._tier_entries(candidate, guests, choose)
            if not entries and not self.tiers.get(candidate):
                break               # no list and no built-in default: nothing further down either
            guest_skipped = False
            for skipped, (preset_id, base_url, model, extra, tier_effort) in enumerate(entries):
                level = effort if effort is not None else tier_effort
                if is_guest_preset(preset_id):
                    # A guest of the High list: usable when its harness runs here. Skipped
                    # otherwise, and the level is kept as written — it is the guest's own word.
                    if not self.guest_check(guest_id_of(preset_id)):
                        guest_skipped = True
                        continue
                    resolved = self._guest(role, preset_id, base_url, model, level, source, candidate)
                else:
                    try:
                        resolved = self._build(role, preset_id, base_url, model, extra, level, source,
                                               candidate)
                    except ValueError:
                        if len(entries) == 1:
                            raise           # the one-model form has always said what is wrong with it
                        continue            # one bad entry must not cost the list the ones below it
                    if resolved.source == "fallback":
                        continue            # no key for that provider: the next entry, then the next tier
                if candidate != tier:
                    # Expected and harmless: a Lite/Flash model on a provider with no key steps towards
                    # Main. Shown inline by the roles modal, never raised as a protocol warning.
                    resolved.note = (f"No stored key for the {TIER_LABELS[tier]} model; "
                                     f"using {TIER_LABELS[candidate]}.")
                elif skipped:
                    why = "no stored key, or a guest that cannot run here" if guest_skipped else "no stored key"
                    # The model by its name, never the raw id (card #MDL1, rule 1).
                    resolved.note = (f"The first {skipped} of the {TIER_LABELS[tier]} list cannot be "
                                     f"used right now ({why}); using "
                                     f"{model_name(preset_id, model) or preset_id}.")
                return resolved
        if tier == "local":
            # Nothing set up rather than no key, and Local is not a step on the ladder: it falls
            # straight back to Main, with the note the roles modal shows inline.
            return self._main(role, "main", tier="main", note="No local model is set up; using main.")
        if background and self.main_config.base_url.startswith(GUEST_BASE_SCHEME):
            # Nothing in the list, and the pane's own model is a harness — which cannot take a
            # side call either, so "using main" would be no answer at all. See `_relay_free_role`.
            spare = self._relay_free_role(role, tier, source)
            if spare is not None:
                return spare
        return self._main(role, "main", tier="main",
                          note=(f"No stored key for the {TIER_LABELS[tier]} model; using main."
                                if self.tiers.get(tier) or self._tier_entries(tier) else None))

    def _relay_free_role(self, role: str, tier: str, source: str) -> Resolved | None:
        """Relay Free's own role for a tier — `relay-flash` for Flash, `relay-lite` for Lite — for
        a background job whose list has nothing left in it, or None when there is no such role or
        this worker cannot use Relay Free.

        The owner's rule of 2026-09-21, and the case that prompted it is a user whose only
        provider is a guest harness: a harness can serve their panes but not one of these jobs, so
        without this every summary, every suggestion and every title would run on the pane's own
        model — which is to say through the harness, which cannot take it either. Relay's own
        allowance is the answer he chose. It is never reached from a pane's own turn.

        Deliberately narrow: it fires only where "using main" is not an answer, which today means
        a pane whose own model is a harness. A pane on a provider Relay cannot name — a custom
        endpoint, a model server on this machine — has no tier defaults either, and there main
        *is* an answer and a better one: `suggestions` carries recent command output and its own
        row in `ACTIONS` promises it "stays on your own provider", which moving it to Relay's
        gateway would break. If the owner wants the fall-through wider than the case he asked
        about, this is the one line to widen.
        """
        entry = tier_default(HOSTED_PRESET_ID, tier)
        if entry is None:
            return None
        preset_id, model, extra = entry
        resolved = self._build(role, preset_id, PRESETS[preset_id].base_url, model, dict(extra),
                               None, source, tier)
        if resolved.source == "fallback":
            return None                     # Relay Free cannot run here either
        resolved.note = (f"Nothing in the {TIER_LABELS[tier]} list can take this job; "
                         f"using relay free.")
        return resolved

    def _default(self, role: str) -> Resolved:
        tier = ROLE_TIERS.get(role)
        if tier is not None:
            return self._main(role, tier="main") if tier == "main" else self._tier(role, tier, "default")
        if role == "planning":
            # Plan mode puts the pane on /high itself (#PH9G, owner 2026-09-22), so the unpinned
            # role is the pane as it is. Only a hand-pinned `roles.planning` entry routes a plan
            # turn elsewhere.
            return self._main(role)
        if role == "vision":
            candidates = [VISION_DEFAULTS.get(self.main_preset_id or "")]
        elif role == "route_assist":
            # The fixed fast model first, then the main preset's own routing model (Relay Free's
            # Lite role); a pane on Relay Free with an OpenRouter key stored still routes there.
            candidates = [ROUTE_ASSIST_DEFAULT, ROUTE_ASSIST_DEFAULTS.get(self.main_preset_id or "")]
        else:
            candidates = []
        for entry in candidates:
            if entry is None:
                continue
            preset_id, model, extra = entry
            preset = PRESETS[preset_id]
            resolved = self._build(role, preset_id, preset.base_url, model, dict(extra), None, "default")
            if resolved.source != "fallback":
                return resolved
        # Built-in defaults are implicit: a missing key quietly means "use the main agent".
        return self._main(role)

    # ----- failover (card #G9VE) ----------------------------------------------------------
    def fallback_candidate(self, fallback, tier: str, exclude, hosts=(), role: str = "main",
                           quota: bool = False) -> Resolved | None:
        """One entry of the Options › Models priority list as a failover target (owner,
        2026-09-20), or None when it cannot take this turn.

        ``fallback`` is one entry of the list the turn is on (`failover_chain`): a ``tiers`` list
        entry ``{"preset", "model", "effort"?}``, or one element of the older ``fallbacks`` option,
        which is the same thing without the level. An entry's level is the level the spare runs
        at; without one it runs at the model's own default, on every tier — a High entry means the
        same thing whether it is the first usable one or the one a failing plan turn reaches (the
        default lists give every High entry its top level, so "max" is said, not implied). ``role`` is the role the result is built for (a side call's; "main" for a turn).
        The agent tries the entries in the user's order and this builds each one on the same terms as
        any candidate: the key comes from the same lookup, the failing preset and its hostname are
        skipped (an entry that names the pane's own provider, or another key on the same host, is
        nothing to move to), and so is a preset already asked this turn. Relay Free is a target
        only when the list names it — the user put it there — and only while this worker can use
        it (`hosted.available`); there is no pane-wide switch for it any more. A model server on
        this machine is allowed for the same reason: the user ranked it, so its answers are what
        they asked for. A guest harness or an unknown id is not a provider this resolver can
        build, and gets None: a guest serves a pane, or a plan turn from its first step
        (`planning_target`), and is never where a turn already under way is moved to. A missing
        model means the provider's own model for the tier the turn is on.

        None here means "skip this entry", never an error: a stale entry (a key since deleted, an
        endpoint since removed) must not cost the turn the entries below it.
        """
        if not isinstance(fallback, dict):
            return None
        preset_id = fallback.get("preset")
        if not isinstance(preset_id, str) or not preset_id or preset_id in exclude:
            return None
        if quota and is_guest_preset(preset_id):
            if not self.guest_check(guest_id_of(preset_id)):
                return None
            tier = validate_tier(tier)
            guest_preset, base_url, model, _extra, effort = self._guest_target(fallback)
            return self._guest(role, guest_preset, base_url, model, effort, "failover", tier)
        preset = _preset(preset_id)
        if preset is None:
            return None
        if preset.hosted and not hosted.available():
            return None
        skip_hosts = {_hostname(PRESETS[p].base_url) for p in exclude if p in PRESETS}
        skip_hosts |= {(h or "").lower() for h in hosts}
        skip_hosts.discard("")
        if not quota and _hostname(preset.base_url) in skip_hosts:
            return None
        tier = validate_tier(tier)
        try:
            # Built the way the tier itself resolves an entry (_list_target): a missing model is
            # the provider's own for this tier, and the extras are that model's.
            _, base_url, model, extra, effort = self._list_target(
                {"preset": preset_id, "model": fallback.get("model") if isinstance(fallback.get("model"), str) else "",
                 "effort": (fallback["effort"].strip().lower()
                            if isinstance(fallback.get("effort"), str) and fallback["effort"].strip()
                            else None),
                 **({"extra": fallback["extra"]} if isinstance(fallback.get("extra"), dict) else {})},
                tier)
            resolved = self._build(role, preset_id, base_url, model, extra, effort, "failover", tier)
        except (ValueError, KeyError):
            return None                 # a preset whose config will not validate: not a spare
        return None if resolved.source == "fallback" else resolved

    def failover_chain(self, tier: str, preset_id, model, fallbacks=(), *, entries=None) -> list[dict]:
        """Where a failing turn may go next, in order: **the rest of the list the turn is on**
        (owner, 2026-09-20; protocol 15.2.2). Entries only — whether each can take the turn is
        `fallback_candidate`'s question, asked when its moment comes.

        ``tier`` is the list: "main" for a pane's own turns, "high" for a plan turn, "flash" /
        "lite" / "local" for a pane or a side call running on that tier. ``preset_id`` / ``model``
        are what the turn is running on now. When that pair is in the list the chain is the
        entries *after* it — the ones above it were ranked higher and the user chose not to be on
        them — and when it is not (a model picked by hand, off the list) the chain is the whole
        list from the top.

        The Main list is ``tiers.main`` when one was sent, else the ``fallbacks`` option of
        earlier the same day, which is the same list minus the levels. A Flash, Lite or Local turn
        with no list of its own walks the Main list, as it did before there were lists; High with
        no list has no chain (its default is the pane's own model, and `Agent._drop_routing`
        returns the turn to it).
        """
        tier = validate_tier(tier)
        if entries is None:
            entries = list(self.tiers.get(tier) or [])
            if not entries and tier != "high":
                entries = list(self.tiers.get("main") or []) or [dict(e) for e in (fallbacks or [])
                                                                 if isinstance(e, dict)]
        else:
            entries = list(entries)
        ranked = sorted(((entry.get("rank", index), index, entry)
                         for index, entry in enumerate(entries, 1)),
                        key=lambda row: (row[0], row[1]))
        for selected_rank, _index, selected in ranked:
            if self._is_entry(selected, preset_id, model, tier):
                return [entry for rank, _at, entry in ranked
                        if (rank == selected_rank and entry is not selected) or rank > selected_rank]
        return [entry for _rank, _index, entry in ranked]

    def _is_entry(self, entry: dict, preset_id, model, tier: str) -> bool:
        """Whether a list entry names the (preset, model) a turn is running on."""
        if not preset_id or entry.get("preset") != preset_id:
            return False
        named = entry.get("model") or ""
        if is_guest_preset(preset_id):
            # A guest entry is the guest: what it runs is what the CLI reported once started
            # ("claude-fable-5-1" for an entry that said "fable"), so the name is matched loosely
            # and an entry without one matches whatever it runs.
            return not named or not model or named == model or named in model
        if named:
            return named == model
        try:
            return self._list_target(entry, tier)[2] == model
        except KeyError:
            return False

    def naming_tier(self, preset_id, model, tiers=TIERS) -> str | None:
        """The first of ``tiers`` whose list names (preset, model), or None when none does.

        Read-only, and asked by anything that wants to know which row of Options › Models a turn is
        running on rather than where it may fail over to: the prompt profile is short on the Lite
        tier (`prompt_profiles.SHORT_TIERS`, #GMCF, owner 2026-09-20), which `turn_tier` cannot
        answer because it deliberately does not look at the Lite list.
        """
        for tier in tiers:
            if any(self._is_entry(entry, preset_id, model, tier) for entry in self.tiers.get(tier) or []):
                return tier
        return None

    def turn_tier(self, preset_id, model, default: str = "main") -> str:
        """Which list a pane running (preset, model) is on, for its failover: Main when the Main
        list names it, else the Flash or Local list that does, else ``default`` — the agent's own
        reading of the provider's tier table, which is all there was before the lists.

        Lite and High are not among them on purpose: a failing Lite turn steps *up* to Main rather
        than down (`Agent._failover_tier`), and High is the plan turn's list, walked by
        `_next_plan_model` before an ordinary failover starts.
        """
        return self.naming_tier(preset_id, model, ("main", "flash", "local")) or default

    def openrouter_twin_candidate(self, model, tier: str, exclude, hosts=()) -> Resolved | None:
        """The same model on OpenRouter (owner, 2026-09-20), as the failover target after every
        entry of the priority list — and the last one — or None when it cannot take this turn.

        ``model`` is the id that failed — the pane's own, not whatever spare is serving by the
        second move — and `presets.openrouter_twin` says which OpenRouter slug serves it; a model
        with no listing, or one that is already an OpenRouter slug, has no twin. Whether the user
        wanted this for *that* model is the agent's question (`failover_openrouter` names the ids
        they opted in): this resolver only says whether the move can be made, on the same terms as
        any candidate — the `openrouter` preset's stored key through the same lookup, never when
        that preset has already been asked this turn, and never back to openrouter.ai when the
        failing host is OpenRouter itself. The tier is carried for the record; the twin is the same
        model whatever tier the pane called it, and High runs it at max like every other candidate.

        None means "the old order stands", never an error.
        """
        slug = openrouter_twin(model)
        if slug is None:
            return None
        preset_id = OPENROUTER_PRESET_ID
        if preset_id in exclude:
            return None
        preset = _preset(preset_id)
        if preset is None:
            return None
        skip_hosts = {_hostname(PRESETS[p].base_url) for p in exclude if p in PRESETS}
        skip_hosts |= {(h or "").lower() for h in hosts}
        skip_hosts.discard("")
        if _hostname(preset.base_url) in skip_hosts:
            return None
        tier = validate_tier(tier)
        effort = "max" if tier == "high" else None
        try:
            resolved = self._build("main", preset_id, preset.base_url, slug, dict(preset.extra),
                                   effort, "failover", tier)
        except ValueError:
            return None
        return None if resolved.source == "fallback" else resolved

    # ----- the helper agent may not run on a guest (card #GH5T) --------------------------
    def leave_guest(self, fallbacks, guest_name: str) -> Resolved | None:
        """Move this resolver off a guest harness onto a model it can actually build.

        The helper agent (``HELPER_ROLE``) works through Relay's own ``board_*`` and ``app_*``
        tools, which a guest does not take (#4NXH), so a helper worker whose Main is Claude Code
        or Codex has to run somewhere else. *Where* is not a new question: it is the Options ›
        Models priority list, walked in the user's own order on exactly the terms a failover walks
        it (``fallback_candidate``) — a guest row, an entry whose key has gone and Relay Free
        unless the list names it and this machine can use it are all skipped.

        The first entry that can take a turn becomes this resolver's main model, so "Follow Main",
        every tier, the subagent factory and the helper's own role land on it instead of on the
        harness; ``main_note`` then says why, and rides into the model box's tooltip.

        Returns what it landed on, or None when the list holds nothing usable — the resolver is
        then left on the guest, still with a note, and the caller says the one sentence
        ``guest_harness_provider.helper_refusal`` gives it.
        """
        # The Main list when one was sent, else the `fallbacks` option: the same order a failing
        # Main turn walks, from the top, because a guest is never where the helper already is.
        for entry in self.failover_chain("main", None, None, fallbacks):
            spare = self.fallback_candidate(entry, "main", ())
            if spare is None:
                continue
            self.rebase(spare.config, spare.preset_id, spare.effort)
            self.main_note = (f"Main is {guest_name}, a guest session the helper agent cannot run "
                              f"on, so it fell back to {spare.config.model}.")
            return spare
        self.main_note = (f"Main is {guest_name}, a guest session the helper agent cannot run on, "
                          "and the Options › Models priority list has nothing else it can use.")
        return None

    # ----- api --------------------------------------------------------------------------
    def resolve(self, role: str) -> Resolved:
        role = validate_role(role)   # also translates the pre-2026-09-18 name "fast"
        if role in self._cache:
            return self._cache[role]
        if role == "main":
            resolved = self._main("main")
        else:
            entry = self.roles.get(role)
            resolved = (self._configured(role, entry, choose=(role == "switchboard"))
                        if entry else self._default(role))
        self._cache[role] = resolved
        if resolved.warning and resolved.warning not in self.warnings:
            self.warnings.append(resolved.warning)
        return resolved

    def choose_role(self, role: str, *, skip_presets=()) -> Resolved:
        """One lifecycle draw; unlike resolve, never reads or writes the display cache."""
        role = validate_role(role)
        if role == "main":
            return self._main("main")
        entry = self.roles.get(role)
        if entry:
            resolved = self._configured(role, entry, choose=True, skip_presets=skip_presets)
        else:
            tier = ROLE_TIERS.get(role)
            resolved = (self._tier(role, tier, "default", choose=True)
                        if tier and tier != "main" else self._default(role))
        if resolved.warning and resolved.warning not in self.warnings:
            self.warnings.append(resolved.warning)
        return resolved

    def resolve_entry(self, role: str, entry: dict) -> Resolved:
        """One pane's own pick for a role (protocol 13.5), resolved exactly as the same entry of
        ``tiers.<tier>`` would be — same model, same extras, same level.

        This is what the model box's mode pages do (card #MDL1, section 5.1): picking
        ``glm-5.3-flash`` while the box is showing the Flash list puts *that pane* on the Flash role
        **and** on that model. Before this the role's model came from ``tiers.flash`` alone, so the
        only way to run one pane's Flash on another model was to re-order the list — which belongs
        to every other pane and to every side call. Nothing is stored here and ``tiers`` is not
        touched: the resolver holds no per-pane state, and the pane's config is what the caller
        then sets.

        A preset with no key here, a guest whose harness cannot run, or an entry that will not
        validate falls back to the main agent exactly as a list entry does — never an error, so a
        pick made against a stale catalog cannot cost the pane its model.
        """
        role = validate_role(role)
        if role == "main":
            raise ValueError('The "main" role is the pane\'s own model; set it with set_model.')
        if not isinstance(entry, dict):
            raise ValueError("A role pick must be an object.")
        preset_id = entry.get("preset")
        if not isinstance(preset_id, str) or not preset_id.strip():
            raise ValueError("A role pick must name a preset.")
        preset_id = preset_id.strip()
        clean: dict = {"preset": preset_id}
        if entry.get("model") is not None:
            clean["model"] = _text(entry["model"], "model", MAX_MODEL)
        if entry.get("effort") is not None:
            # Against this model's own levels: a pick carrying `max` for a model whose top is
            # `xhigh` runs at `xhigh`, and one carrying a word no provider has is the error it
            # always was (card #MDL1, 2026-09-21).
            clean["effort"] = validate_effort(
                entry["effort"],
                None if is_guest_preset(preset_id) else _levels_for(preset_id, entry.get("model") or ""))
        tier = ROLE_TIERS.get(role) or "main"
        if clean.get("effort") is None:
            # A pick made in the model box carries no level of its own, and it is not in any tier
            # list to take one from — so it starts where `model-ranking.md`'s Levels table says
            # this model starts in this class, which is where the same model would start if the
            # `defaults` button had put it in the list (card #MDL1, 2026-09-21). A model the file
            # says nothing about keeps what it always did: the model's own default.
            listed = _file_level(preset_id, clean.get("model") or "", tier)
            if listed:
                clean["effort"] = listed
        if is_guest_preset(preset_id):
            # A guest is a process, not an endpoint: it serves the tiers a harness may serve, and
            # only where this machine can start it (GUEST_TIERS, protocol 29.3) and only for a
            # role that is a pane's own turn rather than a side call (BACKGROUND_ROLES).
            if tier not in GUEST_TIERS or role in BACKGROUND_ROLES \
                    or not self.guest_check(guest_id_of(preset_id)):
                return self._main(role, "fallback",
                                  f"{LABELS[role]}: {preset_id} cannot run here; using the main agent.")
            guest_preset, base_url, model, _extra, guest_effort = self._guest_target(clean)
            return self._guest(role, guest_preset, base_url, model,
                               clean.get("effort", guest_effort), "configured", tier)
        if _preset(preset_id) is None:
            return self._main(role, "fallback",
                              f"{LABELS[role]}: no provider called {preset_id}; using the main agent.")
        try:
            chosen, base_url, model, extra, effort = self._list_target(clean, tier)
            return self._build(role, chosen, base_url, model, extra, effort, "configured", tier)
        except (ValueError, KeyError):
            return self._main(role, "fallback",
                              f"{LABELS[role]}: {preset_id} cannot be used here; using the main agent.")

    def planning_target(self, guests: bool = True) -> Resolved | None:
        """Where a plan-mode turn goes when the planning role is not the main agent (owner, 2026-09-19).

        Unpinned it is None: plan mode has already put the pane on /high (#PH9G, owner
        2026-09-22), and a plan turn adds no swap or effort boost of its own. Only a hand-pinned
        `roles.planning` entry routes the turn elsewhere, and a `guest:` pin starts that guest's
        harness for the turn (protocol 13.7). ``guests=False`` is the agent's second ask when the
        harness would not start: the same pin with the guest entries left out.
        """
        entry = self.roles.get("planning")
        if entry and not guests:
            if entry.get("tier"):
                # A pin onto a tier, re-resolved without its guest entries after a harness would
                # not start.
                resolved = self._tier("planning", entry["tier"], "configured", entry.get("effort"),
                                      guests=False)
            elif is_guest_preset(str(entry.get("preset") or "")):
                # The pinned harness itself would not start: the pane as it is instead.
                return None
            else:
                resolved = self.resolve("planning")     # a pinned endpoint is never a guest to skip
        else:
            resolved = self.resolve("planning")
        return None if resolved.is_main else resolved

    def vision_target(self) -> Resolved | None:
        """Where a turn carrying an image goes when the main model cannot read one (issue EM1E).

        None means "nothing is configured and the provider has no image model", which the agent
        turns into a refusal with a message rather than a provider error. A resolution that lands
        back on the main agent counts as None for exactly that reason: sending an image to a model
        that cannot read it is the failure this is here to avoid.
        """
        resolved = self.resolve("vision")
        return None if resolved.is_main else resolved

    def rebase(self, main_config: ProviderConfig, main_preset_id: str | None, main_effort: str | None = None) -> None:
        """Follow a set_model switch: per-provider defaults are recomputed for the new main model."""
        self.main_config = main_config
        match = match_preset(main_config.base_url, main_config.model)
        self.main_preset_id = main_preset_id if _preset(main_preset_id) else (match.id if match else None)
        self.main_effort = main_effort
        self._cache.clear()
        self.warnings.clear()
        self.main_note = None       # a new main model: whatever was said about the old one is spent

    def set_roles(self, roles: dict) -> None:
        """Replace the configured role table (set_agent_options); applies from the next call."""
        self.roles = dict(roles or {})
        self._cache.clear()
        self.warnings.clear()

    def set_tiers(self, tiers: dict) -> None:
        """Replace the tier lists (protocol 13.7, the output of validate_tiers); applies from the next call."""
        self.tiers = dict(tiers or {})
        self._cache.clear()
        self.warnings.clear()

    def config_for(self, role: str) -> ProviderConfig:
        return self.resolve(role).config

    def summary(self) -> dict:
        """Every role's effective model, for `model_roles` / `configured`. No key material."""
        return {role: self.resolve(role).to_dict() for role in ROLES}

    def tier_summary(self) -> dict:
        """What each tier resolves to right now, for the roles modal. No key material.

        The Main tier is always the pane's own model. High, Flash, Lite and Local report the
        provider and model they land on, whether they fell back and the note the GUI shows inline.
        """
        out: dict[str, dict] = {}
        for tier in TIERS:
            label = TIER_LABELS[tier]
            if tier == "main":
                out[tier] = {"tier": tier, "label": label, "model": self.main_config.model,
                             "preset": self.main_preset_id, "base_url": self.main_config.base_url,
                             "effort": self.main_effort, "source": "main",
                             "list": self._list_summary(tier)}
                if self.main_note:
                    # A helper worker that left a guest (leave_guest): the Main tier is not the
                    # model the window names, and the row says so where the roles modal shows it.
                    out[tier]["note"] = self.main_note
                continue
            # Resolved against a throwaway role so tier probing never pollutes the role cache.
            resolved = self._tier("main", tier, "default")
            entry = {"tier": tier, "label": label, "model": resolved.config.model,
                     "preset": resolved.preset_id, "base_url": resolved.config.base_url,
                     "effort": resolved.effort,
                     "source": "configured" if tier in self.tiers else "default",
                     "using": resolved.tier or "main",
                     # The tier's list as stored, each entry with whether it can take a call right
                     # now, so the GUI can grey the ones resolution and failover will skip.
                     "list": self._list_summary(tier)}
            if resolved.note:
                entry["note"] = resolved.note
            out[tier] = entry
        return out

    def _list_summary(self, tier: str) -> list[dict]:
        """A tier's stored list with ``usable`` per entry: a stored key, a local endpoint or
        Relay Free where it works — and, for a guest, being in the Main list, or in the High list
        with a harness that runs here (GUEST_TIERS). One key lookup per preset, however many
        entries name it."""
        known: dict[str, bool] = {}
        out = []
        for entry in self.tiers.get(tier) or []:
            preset_id = entry.get("preset")
            if is_guest_preset(preset_id):
                if preset_id not in known:
                    known[preset_id] = tier == "main" or (tier in GUEST_TIERS
                                                          and bool(self.guest_check(guest_id_of(preset_id))))
                usable = known[preset_id]
            elif preset_id:
                if preset_id not in known:
                    known[preset_id] = bool(localmodels.find(preset_id)) or self.has_key(preset_id)
                usable = known[preset_id]
            else:
                usable = localmodels.loopback_http(entry.get("base_url") or "")
            out.append({"preset": preset_id or "", "model": entry.get("model") or "",
                        "effort": entry.get("effort"), "usable": usable})
        return out

    def event(self, agent_role: str = "main", request_id=None) -> dict:
        summary = self.summary()
        event = {"event": "model_roles", "roles": summary, "tiers": self.tier_summary(),
                 "agent_role": agent_role, "warnings": list(self.warnings)}
        if request_id is not None:
            event["id"] = request_id
        return event

    def stored(self) -> dict:
        """The configured role table as sent by the GUI (for `agent_options`)."""
        return copy.deepcopy(self.roles)

    def stored_tiers(self) -> dict:
        """The five tier lists as validated — ``{tier: [entry…]}`` — for `agent_options`."""
        return copy.deepcopy(self.tiers)

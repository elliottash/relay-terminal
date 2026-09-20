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
import urllib.parse
from dataclasses import dataclass, replace

from .presets import (EFFORTS, PRESETS, TIER_LABELS, TIERS, apply_effort, effort_style, match_preset,
                      model_efforts, model_extra, openrouter_twin, provider_tier_model, tier_default,
                      tier_fallbacks, validate_effort, validate_tier)
from .provider import ProviderConfig
from . import customproviders, hosted, localmodels


def _hostname(base_url: str) -> str:
    """The host a preset's endpoint lives on, for "two keys, one service" (card #G9VE)."""
    try:
        return (urllib.parse.urlsplit(base_url or "").hostname or "").lower()
    except ValueError:
        return ""


# The preset whose key the "same model on OpenRouter" failover spends (owner, 2026-09-20).
OPENROUTER_PRESET_ID = "openrouter"


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

# Protocol names. "switchboard" is stored and resolved even though the Switchboard itself is not
# built yet (owner decision 2026-09-17); "route_assist" keeps its own fast default.
# "summaries", "suggestions" and "audit" were split out of "flash"/"chores" on 2026-09-17 so the roles
# modal's Advanced list can name one action per row; their defaults resolve exactly as before.
# "local" (2026-09-18) is the pane role /local switches to, the way "flash" is the one /flash
# switches to: a role a pane runs, not a job some side call does.
# "planning" (2026-09-19) serves plan-mode turns. Since 2026-09-20 it follows the High tier, whose
# default is the main model pushed to max reasoning, so a plan is investigated harder without
# switching the pane's own model.
ROLES = ("main", "terminal_use", "subagent", "switchboard", "flash", "local", "planning",
         "summaries", "suggestions", "chores", "audit", "loop_check", "vision", "route_assist")
# The role the helper worker runs (protocol 30.7): one per tab, serving the Switchboard and the
# Options, Actions and Sessions panes. Named because it is the one role that may not run on a
# guest harness (`leave_guest`, card #GH5T) — its whole job is Relay's own tools.
HELPER_ROLE = "switchboard"
SETTABLE = tuple(r for r in ROLES if r != "main")
LABELS = {"main": "Main agent", "terminal_use": "Terminal-use agent", "subagent": "Subagent",
          # "switchboard" keeps its protocol name — settings, the model box (#BRD3), its Options ›
          # Models row and its Main default are untouched — and is labelled "Helper agent" since
          # card #FEJQ (owner decision 4, 2026-09-20): one helper worker per tab now serves the
          # Switchboard *and* the Options, Actions and Sessions panes, so the label names the job
          # rather than the one pane it started in (protocol 30.7).
          "switchboard": "Helper agent", "flash": "Flash agent", "local": "Local agent",
          "planning": "Plan mode", "summaries": "Summaries", "suggestions": "Suggestions",
          "chores": "Chores", "audit": "Request audit", "loop_check": "Loop check",
          "vision": "Vision", "route_assist": "Route assist"}

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
    ("planning", "Plan mode", "investigating and writing plans; the High tier by default"),
    ("subagent", "Subagents", "agents the main agent starts"),
    ("terminal_use", "Driving programs in the terminal", "answering prompts, fixing failed commands"),
    ("flash", "New panes (Flash agent)", "panes that open on the Flash agent"),
    ("local", "Panes on the Local agent (/local)", "a model served on this machine; no key, nothing leaves it"),
    ("suggestions", "Next-command and next-prompt suggestions",
     "sends recent command output, so it stays on your own provider"),
    ("summaries", "Summaries, compaction and recaps", "condensing the conversation"),
    ("switchboard", "Helper agent", "the Switchboard's agent, and the helper in Options, "
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
# owner, 2026-09-20), or takes the provider's Flash or Lite model.
ROLE_TIERS: dict[str, str | None] = {
    "planning": "high",
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
        unknown = set(value) - {"preset", "base_url", "model", "extra", "effort", "inherit", "tier"}
        if unknown:
            raise ValueError(f"roles.{name}: unknown field {sorted(unknown)[0]!r}.")
        if value.get("inherit") is True:
            continue
        entry: dict = {}
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


def is_guest_preset(preset_id) -> bool:
    """Whether a preset id names a guest harness (Claude Code, Codex; protocol 29.3)."""
    return isinstance(preset_id, str) and preset_id.startswith(GUEST_PRESET_PREFIX) \
        and len(preset_id) > len(GUEST_PRESET_PREFIX)


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
    effort = value.get("effort")
    if guest:
        # A guest's levels are its CLI's own words (claude: "max", codex: "xhigh"; 29.3), not
        # Relay's four, so they are kept as written rather than checked against EFFORTS.
        if isinstance(effort, str) and 0 < len(effort.strip()) <= 32:
            entry["effort"] = effort.strip()
    elif isinstance(effort, str) and effort in EFFORTS:
        entry["effort"] = effort
    if tier == "local" and not _is_local_endpoint(entry):
        return None
    return entry


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
                 tiers: dict | None = None):
        from . import keystore
        self.main_config = main_config
        main = match_preset(main_config.base_url, main_config.model)
        self.main_preset_id = main_preset_id if _preset(main_preset_id) else (main.id if main else None)
        self.roles = dict(roles or {})
        self.tiers = dict(tiers or {})
        self.key_lookup = key_lookup or keystore.lookup
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
        is_hosted = _hosted(preset_id) and hosted.available()
        if not key and not local and not is_hosted:
            where = preset_id or base_url
            return self._main(role, "fallback",
                              f"{LABELS[role]}: no stored key for {where}; using the main agent.")
        preset = _preset(preset_id)
        extra = copy.deepcopy(extra or {})
        if effort is not None:
            extra, _ = apply_effort(extra, effort_style(preset, extra, base_url), effort)
        config = ProviderConfig(base_url, model, "" if local else key, extra,
                                localmodels.clamp_max_tokens(self.main_config.max_tokens, local),
                                hosted=is_hosted, **local)
        config.validate()
        return Resolved(role, config, preset_id, effort, source, tier=tier)

    def _configured(self, role: str, entry: dict) -> Resolved:
        if entry.get("tier"):
            return self._tier(role, entry["tier"], "configured", entry.get("effort"))
        preset = _preset(entry.get("preset"))
        base_url = entry.get("base_url") or (preset.base_url if preset else "")
        model = entry.get("model") or (preset.model if preset else "")
        extra = entry.get("extra") if entry.get("extra") is not None else (dict(preset.extra) if preset else {})
        preset_id = preset.id if preset else (match_preset(base_url, model).id if match_preset(base_url, model) else None)
        return self._build(role, preset_id, base_url, model, extra, entry.get("effort"), "configured")

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
            # High and Main have no smaller model to mean, so they get the provider's Main one.
            try:
                model, tier_extra = provider_tier_model(preset.id, "main" if tier == "high" else tier)
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

    def _tier_entries(self, tier: str) -> list[tuple[str | None, str, str, dict, str | None]]:
        """The (preset, base_url, model, extra, effort) candidates of one tier, in order: the
        user's list when there is one (protocol 13.7), otherwise the one built-in default — the
        default provider's row of TIER_DEFAULTS, or the first saved endpoint for Local. [] when
        there is neither. Guest entries are left out: a guest harness can be a pane's own agent,
        never a per-turn swap or a side call, and Main is the only list that may name one."""
        listed = [entry for entry in self.tiers.get(tier) or []
                  if not is_guest_preset(entry.get("preset"))]
        if listed:
            return [self._list_target(entry, tier) for entry in listed]
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
        effort = "max" if effort is None else effort
        style = effort_style(_preset(self.main_preset_id), self.main_config.extra,
                             self.main_config.base_url)
        raised, _ = apply_effort(self.main_config.extra, style, effort)
        if raised != self.main_config.extra:
            return Resolved(role, replace(self.main_config, extra=raised), self.main_preset_id,
                            effort, source, tier="high")
        return self._main(role, tier="high")

    def _tier(self, role: str, tier: str, source: str, effort: str | None = None) -> Resolved:
        """A tier's model for one role: the **first usable entry** of the tier's list, at that
        entry's level (protocol 13.7) — usable meaning a stored key, a model server on this machine
        or Relay Free where it works. With no list the tier is its one built-in default.

        A tier with nothing usable steps one tier towards Main (Lite → Flash → Main); the Main tier
        is the pane's own model, so this never hard-fails. High with no list is the main model at
        max reasoning (_high_default); with one it is resolved like any other tier, and a list
        with nothing usable steps straight down to Main."""
        validate_tier(tier)
        if tier == "main":
            # The pane's own model, whatever `tiers.main` lists: that list is the order a failing
            # Main turn walks (failover_chain), never a pick.
            return self._main(role, "main", tier="main")
        if tier == "high" and not self.tiers.get("high"):
            return self._high_default(role, source, effort)
        for candidate in tier_fallbacks(tier):
            if candidate == "main":
                break
            entries = self._tier_entries(candidate)
            if not entries and not self.tiers.get(candidate):
                break               # no list and no built-in default: nothing further down either
            for skipped, (preset_id, base_url, model, extra, tier_effort) in enumerate(entries):
                try:
                    resolved = self._build(role, preset_id, base_url, model, extra,
                                           effort if effort is not None else tier_effort, source,
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
                    resolved.note = (f"The first {skipped} of the {TIER_LABELS[tier]} list cannot be "
                                     f"used right now (no stored key); using {model}.")
                return resolved
        if tier == "local":
            # Nothing set up rather than no key, and Local is not a step on the ladder: it falls
            # straight back to Main, with the note the roles modal shows inline.
            return self._main(role, "main", tier="main", note="No local model is set up; using Main.")
        return self._main(role, "main", tier="main",
                          note=(f"No stored key for the {TIER_LABELS[tier]} model; using Main."
                                if self.tiers.get(tier) or self._tier_entries(tier) else None))

    def _default(self, role: str) -> Resolved:
        tier = ROLE_TIERS.get(role)
        if tier is not None:
            # Plan mode follows the High tier: the main model at max reasoning unless `tiers.high`
            # names a model (owner, 2026-09-19 and 2026-09-20). That is decided in _tier, once.
            return self._main(role, tier="main") if tier == "main" else self._tier(role, tier, "default")
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
    def fallback_candidate(self, fallback, tier: str, exclude, hosts=(), role: str = "main") -> Resolved | None:
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
        build, and gets None: a guest can be a pane's own agent, never a per-turn swap. A missing
        model means the provider's own model for the tier the turn is on.

        None here means "skip this entry", never an error: a stale entry (a key since deleted, an
        endpoint since removed) must not cost the turn the entries below it.
        """
        if not isinstance(fallback, dict):
            return None
        preset_id = fallback.get("preset")
        if not isinstance(preset_id, str) or not preset_id or preset_id in exclude:
            return None
        preset = _preset(preset_id)
        if preset is None:
            return None
        if preset.hosted and not hosted.available():
            return None
        skip_hosts = {_hostname(PRESETS[p].base_url) for p in exclude if p in PRESETS}
        skip_hosts |= {(h or "").lower() for h in hosts}
        skip_hosts.discard("")
        if _hostname(preset.base_url) in skip_hosts:
            return None
        tier = validate_tier(tier)
        try:
            # Built the way the tier itself resolves an entry (_list_target): a missing model is
            # the provider's own for this tier, and the extras are that model's.
            _, base_url, model, extra, effort = self._list_target(
                {"preset": preset_id, "model": fallback.get("model") if isinstance(fallback.get("model"), str) else "",
                 "effort": fallback.get("effort") if fallback.get("effort") in EFFORTS else None,
                 **({"extra": fallback["extra"]} if isinstance(fallback.get("extra"), dict) else {})},
                tier)
            resolved = self._build(role, preset_id, base_url, model, extra, effort, "failover", tier)
        except (ValueError, KeyError):
            return None                 # a preset whose config will not validate: not a spare
        return None if resolved.source == "fallback" else resolved

    def failover_chain(self, tier: str, preset_id, model, fallbacks=()) -> list[dict]:
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
        entries = list(self.tiers.get(tier) or [])
        if not entries and tier != "high":
            entries = list(self.tiers.get("main") or []) or [dict(e) for e in (fallbacks or [])
                                                             if isinstance(e, dict)]
        for index, entry in enumerate(entries):
            if self._is_entry(entry, preset_id, model, tier):
                return entries[index + 1:]
        return entries

    def _is_entry(self, entry: dict, preset_id, model, tier: str) -> bool:
        """Whether a list entry names the (preset, model) a turn is running on."""
        if not preset_id or entry.get("preset") != preset_id:
            return False
        named = entry.get("model") or ""
        if named:
            return named == model
        if is_guest_preset(preset_id):
            return True             # a guest entry without a model is the guest, whatever it runs
        try:
            return self._list_target(entry, tier)[2] == model
        except KeyError:
            return False

    def turn_tier(self, preset_id, model, default: str = "main") -> str:
        """Which list a pane running (preset, model) is on, for its failover: Main when the Main
        list names it, else the Flash or Local list that does, else ``default`` — the agent's own
        reading of the provider's tier table, which is all there was before the lists."""
        for tier in ("main", "flash", "local"):
            if any(self._is_entry(entry, preset_id, model, tier) for entry in self.tiers.get(tier) or []):
                return tier
        return default

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
            resolved = self._configured(role, entry) if entry else self._default(role)
        self._cache[role] = resolved
        if resolved.warning and resolved.warning not in self.warnings:
            self.warnings.append(resolved.warning)
        return resolved

    def planning_target(self) -> Resolved | None:
        """Where a plan-mode turn goes when the planning role is not the main agent (owner, 2026-09-19).

        None means plan turns stay on the pane's own model: nothing is configured, and the High
        tier's default (the main model at max reasoning) either cannot move the provider's effort
        knob or the pane's effort is already max.
        """
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
        Relay Free where it works — and, for a guest, being in the Main list, the only one a
        guest can serve. One key lookup per preset, however many entries name it."""
        known: dict[str, bool] = {}
        out = []
        for entry in self.tiers.get(tier) or []:
            preset_id = entry.get("preset")
            if is_guest_preset(preset_id):
                usable = tier == "main"
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

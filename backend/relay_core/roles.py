# SPDX-License-Identifier: GPL-3.0-or-later
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
from dataclasses import dataclass

from .presets import (PRESETS, TIER_LABELS, TIERS, apply_effort, effort_style, match_preset,
                      provider_tier_model, tier_default, tier_fallbacks, validate_effort,
                      validate_tier)
from .provider import ProviderConfig
from . import hosted, localmodels


def _hosted(preset_id) -> bool:
    """Whether a preset id names Relay's own hosted service (the relay-free preset)."""
    preset = PRESETS.get(preset_id) if isinstance(preset_id, str) else None
    return bool(preset is not None and preset.hosted)


def _preset(preset_id):
    """A built-in preset, or a saved model server on this machine (localmodels.py) in the same shape."""
    if not preset_id:
        return None
    found = PRESETS.get(preset_id)
    if found is None and localmodels.is_local_id(preset_id):
        endpoint = localmodels.find(preset_id)
        found = endpoint.as_preset() if endpoint is not None else None
    return found

# Protocol names. "switchboard" is stored and resolved even though the Switchboard itself is not
# built yet (owner decision 2026-09-17); "route_assist" keeps its own fast default.
# "summaries", "suggestions" and "audit" were split out of "flash"/"chores" on 2026-09-17 so the roles
# modal's Advanced list can name one action per row; their defaults resolve exactly as before.
# "local" (2026-09-18) is the pane role /local switches to, the way "flash" is the one /flash
# switches to: a role a pane runs, not a job some side call does.
ROLES = ("main", "terminal_use", "subagent", "switchboard", "flash", "local", "summaries",
         "suggestions", "chores", "audit", "vision", "route_assist")
SETTABLE = tuple(r for r in ROLES if r != "main")
LABELS = {"main": "Main agent", "terminal_use": "Terminal-use agent", "subagent": "Subagent",
          "switchboard": "Switchboard agent", "flash": "Flash agent", "local": "Local agent",
          "summaries": "Summaries", "suggestions": "Suggestions", "chores": "Chores",
          "audit": "Request audit", "vision": "Vision", "route_assist": "Route assist"}

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
    ("subagent", "Subagents", "agents the main agent starts"),
    ("terminal_use", "Driving programs in the terminal", "answering prompts, fixing failed commands"),
    ("flash", "New panes (Flash agent)", "panes that open on the Flash agent"),
    ("local", "Panes on the Local agent (/local)", "a model served on this machine; no key, nothing leaves it"),
    ("suggestions", "Next-command and next-prompt suggestions",
     "sends recent command output, so it stays on your own provider"),
    ("summaries", "Summaries, compaction and recaps", "condensing the conversation"),
    ("switchboard", "Switchboard card threads", "stored now; used when the Switchboard lands"),
    ("chores", "Chores: duplicate checks, labels, titles, note scans", "small structured judgements"),
    ("audit", "Request audit", "flags asks that may be unaddressed after a turn"),
    ("vision", "Images and vision turns", "used when the main model cannot read images"),
    ("route_assist", "Command routing", "decides shell or agent for one line of input"),
)

MAX_MODEL = 200
MAX_URL = 400

# --- built-in defaults (issues/features/2026-09-17-model-roles-and-fast-agent.md) -------------------
# Roles are grouped into three tiers (presets.TIER_DEFAULTS, protocol 13.7): a role either follows the
# pane's own model ("main") or takes the provider's Flash or Lite model.
ROLE_TIERS: dict[str, str | None] = {
    "main": "main", "subagent": "main", "switchboard": "main",
    "terminal_use": "flash", "flash": "flash", "summaries": "flash", "suggestions": "flash",
    "chores": "lite", "audit": "lite", "local": "local",
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
    ``providers`` covers the three provider tiers only; the Local tier belongs to no provider, and
    the endpoints it can name are the ``presets`` rows with ``local: true`` in the same event.
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
            # A tiered role takes the provider's Main/Flash/Lite model (protocol 13.7). It is exclusive
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


def validate_tiers(raw) -> dict[str, dict]:
    """Normalize the ``tiers`` object from configure / set_agent_options (protocol 13.7).

    Each of ``flash`` and ``lite`` may name a preset (with an optional model, extra and effort) or a
    custom ``base_url``/``model``. ``main`` is rejected: it is the pane's own model, set with
    ``configure`` / ``set_model``. ``null`` restores the provider's built-in default for that tier.
    """
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise ValueError("tiers must be an object.")
    out: dict[str, dict] = {}
    for name, value in raw.items():
        validate_tier(name)
        if name == "main":
            raise ValueError('The "main" tier is the pane\'s own model; set it with configure or set_model.')
        if value is None:
            continue
        if not isinstance(value, dict):
            raise ValueError(f"tiers.{name} must be an object or null.")
        unknown = set(value) - {"preset", "base_url", "model", "extra", "effort"}
        if unknown:
            raise ValueError(f"tiers.{name}: unknown field {sorted(unknown)[0]!r}.")
        entry: dict = {}
        if value.get("preset") is not None:
            preset = _text(value["preset"], f"{name}.preset", 64)
            if preset and _preset(preset) is None:
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
            continue
        if "preset" not in entry and not (entry.get("base_url") and entry.get("model")):
            raise ValueError(f"tiers.{name}: give a preset, or both base_url and model.")
        if name == "local" and not _is_local_endpoint(entry):
            raise ValueError("tiers.local must name a model server on this machine: a saved local "
                             "endpoint id, or a plain http:// base_url on localhost, 127.0.0.1 or "
                             "::1 with its model.")
        out[name] = entry
    return out


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
    tier: str | None = None          # the Main/Flash/Lite tier this role came from, when tiered
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

    # ----- helpers ----------------------------------------------------------------------
    def _main(self, role: str, source: str = "main", warning: str | None = None,
              tier: str | None = None, note: str | None = None) -> Resolved:
        return Resolved(role, self.main_config, self.main_preset_id, self.main_effort, source, warning,
                        tier, note)

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
    def _tier_entry(self, tier: str) -> tuple[str | None, str, str, dict, str | None] | None:
        """(preset, base_url, model, extra, effort) for one tier: the user's override if there is one,
        otherwise the default provider's built-in tier. None when the provider has no tier table."""
        override = self.tiers.get(tier)
        if override:
            preset = _preset(override.get("preset"))
            base_url = override.get("base_url") or (preset.base_url if preset else "")
            model = override.get("model") or ""
            extra = override["extra"] if override.get("extra") is not None else None
            if preset and not model:
                # An override that names a provider and no model means that provider's model *for this
                # tier* (presets.provider_tier_model): "Flash on Z.AI" is glm-5.3-flash, not glm-5.3.
                model, tier_extra = provider_tier_model(preset.id, tier)
                if extra is None:
                    extra = tier_extra
            if not model and preset:
                model = preset.model
            if extra is None:
                extra = dict(preset.extra) if preset else {}
            match = match_preset(base_url, model)
            return (preset.id if preset else (match.id if match else None)), base_url, model, extra, override.get("effort")
        if tier == "local":
            # The Local tier belongs to no provider, so there is no TIER_DEFAULTS row to read: with
            # no override it is the first saved endpoint, so one saved server just works.
            endpoint = next(iter(localmodels.catalog().values()), None)
            if endpoint is None:
                return None
            return endpoint.id, endpoint.base_url, endpoint.model, dict(endpoint.extra), None
        entry = tier_default(self.main_preset_id, tier)
        if entry is None:
            return None
        preset_id, model, extra = entry
        return preset_id, PRESETS[preset_id].base_url, model, dict(extra), None

    def _tier(self, role: str, tier: str, source: str, effort: str | None = None) -> Resolved:
        """A tier's model for one role. A tier whose provider has no stored key steps one tier towards
        Main (Lite → Flash → Main); the Main tier is the pane's own model, so this never hard-fails."""
        validate_tier(tier)
        for candidate in tier_fallbacks(tier):
            if candidate == "main":
                break
            entry = self._tier_entry(candidate)
            if entry is None:
                break
            preset_id, base_url, model, extra, tier_effort = entry
            resolved = self._build(role, preset_id, base_url, model, extra,
                                   effort if effort is not None else tier_effort, source, candidate)
            if resolved.source == "fallback":
                continue          # no key for that provider: try the next tier towards Main
            if candidate != tier:
                # Expected and harmless: a Lite/Flash model on a provider with no key steps towards
                # Main. Shown inline by the roles modal, never raised as a protocol warning.
                resolved.note = (f"No stored key for the {TIER_LABELS[tier]} model; "
                                 f"using {TIER_LABELS[candidate]}.")
            return resolved
        if tier == "local":
            # Nothing set up rather than no key, and Local is not a step on the ladder: it falls
            # straight back to Main, with the note the roles modal shows inline.
            return self._main(role, "main", tier="main", note="No local model is set up; using Main.")
        return self._main(role, "main", tier="main",
                          note=(f"No stored key for the {TIER_LABELS[tier]} model; using Main."
                                if self._tier_entry(tier) is not None else None))

    def _default(self, role: str) -> Resolved:
        tier = ROLE_TIERS.get(role)
        if tier is not None:
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

    def set_roles(self, roles: dict) -> None:
        """Replace the configured role table (set_agent_options); applies from the next call."""
        self.roles = dict(roles or {})
        self._cache.clear()
        self.warnings.clear()

    def set_tiers(self, tiers: dict) -> None:
        """Replace the Flash/Lite tier overrides (protocol 13.7); applies from the next call."""
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

        The Main tier is always the pane's own model. Flash and Lite report the provider and model
        they land on, whether they fell back and the note the GUI shows inline.
        """
        out: dict[str, dict] = {}
        for tier in TIERS:
            label = TIER_LABELS[tier]
            if tier == "main":
                out[tier] = {"tier": tier, "label": label, "model": self.main_config.model,
                             "preset": self.main_preset_id, "base_url": self.main_config.base_url,
                             "effort": self.main_effort, "source": "main"}
                continue
            # Resolved against a throwaway role so tier probing never pollutes the role cache.
            resolved = self._tier("main", tier, "default")
            entry = {"tier": tier, "label": label, "model": resolved.config.model,
                     "preset": resolved.preset_id, "base_url": resolved.config.base_url,
                     "effort": resolved.effort,
                     "source": "configured" if tier in self.tiers else "default",
                     "using": resolved.tier or "main"}
            if resolved.note:
                entry["note"] = resolved.note
            out[tier] = entry
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
        """The Flash/Lite overrides as sent by the GUI (for `agent_options`)."""
        return copy.deepcopy(self.tiers)

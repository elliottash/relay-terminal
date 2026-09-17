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

from .presets import PRESETS, apply_effort, effort_style, match_preset, validate_effort
from .provider import ProviderConfig

# Protocol names. "switchboard" is stored and resolved even though the Switchboard itself is not
# built yet (owner decision 2026-09-17); "route_assist" keeps its own fast default.
ROLES = ("main", "terminal_use", "subagent", "switchboard", "fast", "chores", "vision", "route_assist")
SETTABLE = tuple(r for r in ROLES if r != "main")
LABELS = {"main": "Main agent", "terminal_use": "Terminal-use agent", "subagent": "Subagent",
          "switchboard": "Switchboard agent", "fast": "Fast agent", "chores": "Chores",
          "vision": "Vision", "route_assist": "Route assist"}

MAX_MODEL = 200
MAX_URL = 400

# --- built-in defaults (issues/features/2026-09-17-model-roles-and-fast-agent.md) -------------------
# Fast agent by the main agent's provider. GLM-5.3 Flash is measured fastest with thinking off
# (1.7-5.0 s versus 3.1-19.2 s), so the default disables it.
GLM_FAST_EXTRA = {"thinking": {"type": "disabled"}}
FAST_DEFAULTS: dict[str, tuple[str, str, dict]] = {
    "glm": ("glm", "glm-5.3-flash", GLM_FAST_EXTRA),
    "glm-coding": ("glm-coding", "glm-5.3-flash", GLM_FAST_EXTRA),
    "openrouter": ("openrouter", "deepseek/deepseek-v4.1-flash", {}),
    "kimi": ("kimi", "kimi-k2.7-code-highspeed", {}),
    "kimi-code": ("kimi-code", "kimi-for-coding-highspeed", {}),
}
# Chores (duplicate checks, labels, titles, note scans): Gemini 3.8 Flash on OpenRouter when a key
# is stored, otherwise the fast agent.
CHORES_DEFAULT = ("openrouter", "google/gemini-3.8-flash", {})
# Vision turns on presets without image support: GLM-5.3 Flash for GLM, unset elsewhere.
VISION_DEFAULTS: dict[str, tuple[str, str, dict]] = {
    "glm": ("glm", "glm-5.3-flash", {}),
    "glm-coding": ("glm-coding", "glm-5.3-flash", {}),
}
# Route assist keeps its fixed fast model (latency budget under 1 s); see route_assist.py.
ROUTE_ASSIST_DEFAULT = ("openrouter", "google/gemini-3.5-flash-lite", {})


def _text(value, name: str, limit: int) -> str:
    if not isinstance(value, str):
        raise ValueError(f"roles: {name} must be text.")
    value = value.strip()
    if len(value) > limit:
        raise ValueError(f"roles: {name} is too long.")
    return value


def validate_role(role) -> str:
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
        validate_role(name)
        if name == "main":
            raise ValueError('The "main" role is the pane\'s own model; set it with configure or set_model.')
        if value is None:
            continue
        if not isinstance(value, dict):
            raise ValueError(f"roles.{name} must be an object or null.")
        unknown = set(value) - {"preset", "base_url", "model", "extra", "effort", "inherit"}
        if unknown:
            raise ValueError(f"roles.{name}: unknown field {sorted(unknown)[0]!r}.")
        if value.get("inherit") is True:
            continue
        entry: dict = {}
        preset = value.get("preset")
        if preset is not None:
            preset = _text(preset, f"{name}.preset", 64)
            if preset and preset not in PRESETS:
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


@dataclass
class Resolved:
    """One role's effective model. Never carries key material."""
    role: str
    config: ProviderConfig
    preset_id: str | None
    effort: str | None
    source: str                      # main | configured | default | fallback
    warning: str | None = None

    @property
    def model(self) -> str:
        return self.config.model

    @property
    def is_main(self) -> bool:
        return self.source in ("main", "fallback")

    def to_dict(self) -> dict:
        out = {"role": self.role, "model": self.config.model, "preset": self.preset_id,
               "base_url": self.config.base_url, "effort": self.effort, "source": self.source,
               "label": LABELS[self.role]}
        if self.warning:
            out["warning"] = self.warning
        return out


class RoleResolver:
    """Resolves each role to a ProviderConfig, caching the answer (and its keyring lookups)."""

    def __init__(self, main_config: ProviderConfig, main_preset_id: str | None = None,
                 roles: dict | None = None, *, key_lookup=None, main_effort: str | None = None):
        from . import keystore
        self.main_config = main_config
        main = match_preset(main_config.base_url, main_config.model)
        self.main_preset_id = main_preset_id if main_preset_id in PRESETS else (main.id if main else None)
        self.roles = dict(roles or {})
        self.key_lookup = key_lookup or keystore.lookup
        self.main_effort = main_effort
        self._cache: dict[str, Resolved] = {}
        self.warnings: list[str] = []

    # ----- helpers ----------------------------------------------------------------------
    def _main(self, role: str, source: str = "main", warning: str | None = None) -> Resolved:
        return Resolved(role, self.main_config, self.main_preset_id, self.main_effort, source, warning)

    def _key_for(self, preset_id: str | None) -> str:
        if preset_id and preset_id == self.main_preset_id and self.main_config.api_key:
            return self.main_config.api_key
        return self.key_lookup(preset_id) if preset_id else ""

    def has_key(self, preset_id: str) -> bool:
        return bool(self._key_for(preset_id))

    def _build(self, role: str, preset_id: str | None, base_url: str, model: str, extra: dict,
               effort: str | None, source: str) -> Resolved:
        key = self._key_for(preset_id)
        if not key:
            where = preset_id or base_url
            return self._main(role, "fallback",
                              f"{LABELS[role]}: no stored key for {where}; using the main agent.")
        preset = PRESETS.get(preset_id) if preset_id else None
        extra = copy.deepcopy(extra or {})
        if effort is not None:
            extra, _ = apply_effort(extra, effort_style(preset, extra, base_url), effort)
        config = ProviderConfig(base_url, model, key, extra, self.main_config.max_tokens)
        config.validate()
        return Resolved(role, config, preset_id, effort, source)

    def _configured(self, role: str, entry: dict) -> Resolved:
        preset = PRESETS.get(entry.get("preset") or "")
        base_url = entry.get("base_url") or (preset.base_url if preset else "")
        model = entry.get("model") or (preset.model if preset else "")
        extra = entry.get("extra") if entry.get("extra") is not None else (dict(preset.extra) if preset else {})
        preset_id = preset.id if preset else (match_preset(base_url, model).id if match_preset(base_url, model) else None)
        return self._build(role, preset_id, base_url, model, extra, entry.get("effort"), "configured")

    def _default(self, role: str) -> Resolved:
        if role == "fast":
            entry = FAST_DEFAULTS.get(self.main_preset_id or "")
        elif role == "vision":
            entry = VISION_DEFAULTS.get(self.main_preset_id or "")
        elif role == "route_assist":
            entry = ROUTE_ASSIST_DEFAULT
        elif role == "chores":
            entry = CHORES_DEFAULT if self.has_key(CHORES_DEFAULT[0]) else None
            if entry is None:
                fast = self.resolve("fast")
                return Resolved(role, fast.config, fast.preset_id, fast.effort,
                                "default" if not fast.is_main else "main", fast.warning)
        else:
            entry = None
        if entry is None:
            return self._main(role)
        preset_id, model, extra = entry
        preset = PRESETS[preset_id]
        resolved = self._build(role, preset_id, preset.base_url, model, dict(extra), None, "default")
        if resolved.source == "fallback" and role in ("route_assist", "fast", "vision"):
            # Built-in defaults are implicit: a missing key quietly means "use the main agent".
            return self._main(role)
        return resolved

    # ----- api --------------------------------------------------------------------------
    def resolve(self, role: str) -> Resolved:
        validate_role(role)
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

    def rebase(self, main_config: ProviderConfig, main_preset_id: str | None, main_effort: str | None = None) -> None:
        """Follow a set_model switch: per-provider defaults are recomputed for the new main model."""
        self.main_config = main_config
        match = match_preset(main_config.base_url, main_config.model)
        self.main_preset_id = main_preset_id if main_preset_id in PRESETS else (match.id if match else None)
        self.main_effort = main_effort
        self._cache.clear()
        self.warnings.clear()

    def set_roles(self, roles: dict) -> None:
        """Replace the configured role table (set_agent_options); applies from the next call."""
        self.roles = dict(roles or {})
        self._cache.clear()
        self.warnings.clear()

    def config_for(self, role: str) -> ProviderConfig:
        return self.resolve(role).config

    def summary(self) -> dict:
        """Every role's effective model, for `model_roles` / `configured`. No key material."""
        return {role: self.resolve(role).to_dict() for role in ROLES}

    def event(self, agent_role: str = "main", request_id=None) -> dict:
        summary = self.summary()
        event = {"event": "model_roles", "roles": summary, "agent_role": agent_role,
                 "warnings": list(self.warnings)}
        if request_id is not None:
            event["id"] = request_id
        return event

    def stored(self) -> dict:
        """The configured role table as sent by the GUI (for `agent_options`)."""
        return copy.deepcopy(self.roles)

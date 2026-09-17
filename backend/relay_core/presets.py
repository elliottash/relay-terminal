# SPDX-License-Identifier: GPL-3.0-or-later
"""Built-in provider presets. The GUI dialog mirrors this table in src/main.cpp."""
from __future__ import annotations

import copy
from dataclasses import dataclass, field

EFFORTS = ("low", "medium", "high", "max")

# Relay effort -> provider value, per request style. Verified 2026-09-17 against:
# - Kimi K3: https://platform.kimi.ai/docs/guide/kimi-k3-quickstart (top-level reasoning_effort: low|high|max,
#   default max; thinking cannot be disabled).
# - GLM-5.3: https://docs.z.ai/guides/llm/glm-5.3.md (thinking.type must be "enabled"; reasoning_effort
#   low|high|max, default max).
# - OpenRouter: https://openrouter.ai/docs/use-cases/reasoning-tokens.md (reasoning.effort: none|minimal|low|
#   medium|high|xhigh|max; xhigh and max get the same ~95% budget).
EFFORT_MAP: dict[str, dict[str, str]] = {
    "kimi": {"low": "low", "medium": "high", "high": "high", "max": "max"},
    "glm": {"low": "low", "medium": "high", "high": "high", "max": "max"},
    "openrouter": {"low": "low", "medium": "medium", "high": "high", "max": "xhigh"},
}

# Context windows in tokens (verified 2026-09-17; see docs/INTAKE-CLARIFICATION-RESEARCH.md section 4).
# GLM-5.3 uses Z.AI's documented 1,000,000 (OpenRouter lists 1,310,720, but some OpenRouter GLM endpoints
# only offer ~262K; the openrouter preset here is DeepSeek, which is 1,048,576 on every endpoint).
DEFAULT_CONTEXT_WINDOW = 128_000  # conservative fallback for unknown/custom models


@dataclass(frozen=True)
class Preset:
    id: str
    label: str
    base_url: str
    model: str
    extra: dict = field(default_factory=dict)
    context_window: int = DEFAULT_CONTEXT_WINDOW
    effort_style: str = "kimi"

    def to_dict(self) -> dict:
        return {"id": self.id, "label": self.label, "base_url": self.base_url,
                "model": self.model, "extra": dict(self.extra), "context_window": self.context_window,
                "efforts": distinct_efforts(self.effort_style)}


GLM_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "high"}

PRESETS: dict[str, Preset] = {p.id: p for p in [
    Preset("kimi", "Kimi · K3", "https://api.moonshot.ai/v1", "kimi-k3", {"reasoning_effort": "high"},
           1_048_576, "kimi"),
    Preset("glm", "Z.AI · GLM-5.3 · standard API", "https://api.z.ai/api/paas/v4", "glm-5.3", GLM_EXTRA,
           1_000_000, "glm"),
    Preset("glm-coding", "Z.AI · GLM-5.3 · Coding Plan", "https://api.z.ai/api/coding/paas/v4", "glm-5.3", GLM_EXTRA,
           1_000_000, "glm"),
    Preset("openrouter", "OpenRouter · DeepSeek V4.1 Flash", "https://openrouter.ai/api/v1",
           "deepseek/deepseek-v4.1-flash", {}, 1_048_576, "openrouter"),
]}


def normalize_url(url: str) -> str:
    return url.strip().rstrip("/").lower()


def match_preset(base_url: str, model: str = "") -> Preset | None:
    """Find the preset for an endpoint. Model is only used to break ties."""
    candidates = [p for p in PRESETS.values() if normalize_url(p.base_url) == normalize_url(base_url)]
    for preset in candidates:
        if preset.model == model.strip():
            return preset
    return candidates[0] if candidates else None


def resolve_preset(preset_id, base_url: str = "", model: str = "") -> Preset | None:
    if isinstance(preset_id, str) and preset_id in PRESETS:
        return PRESETS[preset_id]
    return match_preset(base_url or "", model or "")


def distinct_efforts(style: str) -> list[str]:
    """Relay levels that map to different provider values (Kimi/GLM have no medium)."""
    seen, out = set(), []
    for level in EFFORTS:
        value = EFFORT_MAP[style][level]
        if value not in seen:
            seen.add(value)
            out.append(level)
    return out


def effort_style(preset: Preset | None, extra: dict | None = None, base_url: str = "") -> str:
    if preset is not None:
        return preset.effort_style
    extra = extra or {}
    if "reasoning" in extra or "openrouter.ai" in (base_url or ""):
        return "openrouter"
    if "thinking" in extra:
        return "glm"
    return "kimi"


def validate_effort(effort) -> str:
    if effort not in EFFORTS:
        raise ValueError("effort must be one of low, medium, high, max.")
    return effort


def apply_effort(extra: dict | None, style: str, effort: str | None) -> tuple[dict, dict]:
    """Return (new extra, applied provider params). effort=None leaves extra unchanged."""
    result = copy.deepcopy(extra or {})
    if effort is None:
        return result, {}
    value = EFFORT_MAP[style][validate_effort(effort)]
    if style == "openrouter":
        reasoning = dict(result.get("reasoning") or {})
        reasoning.pop("max_tokens", None)  # OpenRouter accepts effort or max_tokens, not both
        reasoning["effort"] = value
        applied = {"reasoning": reasoning}
    elif style == "glm":
        applied = {"thinking": {"type": "enabled"}, "reasoning_effort": value}
    else:
        applied = {"reasoning_effort": value}
    result.update(copy.deepcopy(applied))
    return result, applied


def infer_effort(style: str, extra: dict | None) -> str | None:
    """Best Relay level describing the provider params already in extra, if any."""
    extra = extra or {}
    value = (extra.get("reasoning") or {}).get("effort") if style == "openrouter" else extra.get("reasoning_effort")
    if not isinstance(value, str):
        return None
    for level in ("low", "high", "max", "medium"):
        if EFFORT_MAP[style][level] == value:
            return level
    return "max" if value in ("max", "xhigh") else None


def context_window_for(preset: Preset | None) -> int:
    return preset.context_window if preset is not None else DEFAULT_CONTEXT_WINDOW

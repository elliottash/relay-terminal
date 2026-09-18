# SPDX-License-Identifier: GPL-3.0-or-later
"""Built-in provider presets and the Main/Flash/Lite tier table.

The GUI mirrors both in src/main.cpp (``presetMirror()`` / ``tierMirror()``); tests/test_presets.py
checks that the mirror has not drifted. Every endpoint and model id below was verified against the
provider's own documentation on 2026-09-17; the doc URL sits next to the entry it supports.
"""
from __future__ import annotations

import copy
from dataclasses import dataclass, field

EFFORTS = ("low", "medium", "high", "max")

# Relay effort -> provider value, per request style. Verified 2026-09-17 against:
# - Kimi K3: https://platform.kimi.ai/docs/api/chat (top-level reasoning_effort: low|high|max, default max,
#   and only on kimi-k3; thinking cannot be disabled).
# - GLM-5.3: https://docs.z.ai/guides/capabilities/thinking (thinking.type accepts only "enabled";
#   reasoning_effort low|high|max, default max).
# - OpenRouter: https://openrouter.ai/docs/use-cases/reasoning-tokens.md (reasoning.effort: none|minimal|low|
#   medium|high|xhigh|max; xhigh and max get the same ~95% budget).
# - OpenAI: https://developers.openai.com/api/docs/api-reference/chat/create (reasoning_effort none|minimal|
#   low|medium|high|xhigh|max, per-model subsets; gpt-6-astra rejects "none", so Relay never sends it).
# - Gemini: https://ai.google.dev/gemini-api/docs/openai (reasoning_effort minimal|low|medium|high; "minimal"
#   is rejected by gemini-3.8-flash and "none" only works on 2.5 models, so max maps to high).
# - "none": providers with no usable effort knob on their OpenAI-compatible endpoint. Anthropic's compat
#   layer ignores reasoning_effort and rejects `thinking` on Claude 5
#   (https://platform.claude.com/docs/en/cli-sdks-libraries/libraries/openai-sdk); MiniMax has no
#   reasoning_effort at all and only M3 can disable thinking
#   (https://platform.minimax.io/docs/api-reference/text-chat-openai.md).
EFFORT_MAP: dict[str, dict[str, str]] = {
    "kimi": {"low": "low", "medium": "high", "high": "high", "max": "max"},
    "glm": {"low": "low", "medium": "high", "high": "high", "max": "max"},
    "openrouter": {"low": "low", "medium": "medium", "high": "high", "max": "xhigh"},
    "openai": {"low": "low", "medium": "medium", "high": "high", "max": "xhigh"},
    "gemini": {"low": "low", "medium": "medium", "high": "high", "max": "high"},
    "none": {level: "" for level in EFFORTS},
}

# Context windows in tokens (verified 2026-09-17; see docs/INTAKE-CLARIFICATION-RESEARCH.md section 4).
# GLM-5.3 uses Z.AI's documented 1,000,000 (OpenRouter lists 1,310,720, but some OpenRouter GLM endpoints
# only offer ~262K; the openrouter preset here is DeepSeek, which is 1,048,576 on every endpoint).
DEFAULT_CONTEXT_WINDOW = 128_000  # conservative fallback for unknown/custom models

# --- image (vision) support -------------------------------------------------------------------
# Which models can read images, by model-id prefix (issue EM1E, owner decision 2026-09-17). An
# OpenRouter-style slug is matched on its last segment, so "google/gemini-3.8-flash" counts as Gemini.
#
# GLM-5.3 is text-only and Z.AI's image model is GLM-5.3-Flash; that is exactly why a turn carrying
# an image on GLM swaps to Flash for that turn and back afterwards (roles.VISION_DEFAULTS). Kimi's
# coding models, MiniMax M3 and DeepSeek are text-only, so an image turn there needs a configured
# vision model and is otherwise refused with a message instead of being sent and rejected.
VISION_MODELS: tuple[str, ...] = (
    "gpt-", "chatgpt-", "o3", "o4",                              # OpenAI: the GPT family reads images
    "claude-",                                                   # Anthropic: Claude 3 and later
    "gemini-",                                                   # Google: the Gemini family
    "glm-5.3-flash", "glm-4.5v", "glm-4.6v", "glm-5v",           # Z.AI: the Flash and V models
    "qwen-vl", "qwen2-vl", "qwen3-vl", "pixtral", "llava", "minimax-vl",
    "kimi-latest", "moonshot-v1-8k-vision", "moonshot-v1-32k-vision", "moonshot-v1-128k-vision",
)


def model_supports_vision(model) -> bool:
    """Whether a model id names a model that can read images. Unknown ids count as text-only."""
    if not isinstance(model, str):
        return False
    name = model.strip().lower().rsplit("/", 1)[-1]
    return bool(name) and any(name.startswith(prefix) for prefix in VISION_MODELS)


# Keys-modal grouping (GUI only; the backend never treats groups differently).
GROUPS = ("subscription", "aggregator", "payg")
GROUP_LABELS = {"subscription": "Subscriptions", "aggregator": "Aggregator",
                "payg": "Pay-as-you-go"}


@dataclass(frozen=True)
class Preset:
    id: str
    label: str
    base_url: str
    model: str
    extra: dict = field(default_factory=dict)
    context_window: int = DEFAULT_CONTEXT_WINDOW
    effort_style: str = "kimi"
    group: str = "payg"
    key_url: str = ""          # where the user gets a key; shown as a hint, never fetched
    note: str = ""             # one line under the row in the keys modal
    # The company, not the model: the roles modal picks a *provider*, and "Kimi · K3" read as if the
    # tier were pinned to K3. `plan` tells two presets of the same provider apart when both are listed.
    provider: str = ""
    plan: str = ""

    @property
    def vision(self) -> bool:
        """Whether this preset's default model can read images. Derived, so it cannot drift."""
        return model_supports_vision(self.model)

    def to_dict(self) -> dict:
        return {"id": self.id, "label": self.label, "base_url": self.base_url,
                "model": self.model, "extra": dict(self.extra), "context_window": self.context_window,
                "efforts": distinct_efforts(self.effort_style), "group": self.group,
                "key_url": self.key_url, "note": self.note, "vision": self.vision,
                "provider": self.provider or self.label.split(" · ")[0], "plan": self.plan}


GLM_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "high"}
# GLM-5.3-Flash cannot turn thinking off: "GLM-5.3 and GLM-5.3-FLASH no longer support disabling thinking
# (an error will occur if the thinking.type parameter ... is set to disabled)"
# — https://docs.z.ai/guides/capabilities/thinking. The cheapest legal setting is reasoning_effort low.
GLM_FAST_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "low"}

PRESETS: dict[str, Preset] = {p.id: p for p in [
    Preset("kimi", "Kimi · K3", "https://api.moonshot.ai/v1", "kimi-k3", {"reasoning_effort": "high"},
           1_048_576, "kimi", "payg", "https://platform.kimi.ai/console/api-keys",
           "Moonshot platform, pay-as-you-go.",
           provider="Kimi", plan="Pay-as-you-go"),
    # Kimi Code subscription (https://www.kimi.com/code/docs/en/): OpenAI-compatible base
    # https://api.kimi.ai/coding/v1; model ids k3, k3-256k, kimi-for-coding, kimi-for-coding-highspeed.
    Preset("kimi-code", "Kimi Code · K3", "https://api.kimi.ai/coding/v1", "k3", {"reasoning_effort": "high"},
           1_048_576, "kimi", "subscription", "https://www.kimi.com/code/console",
           "Kimi Code subscription key (not a Moonshot platform key).",
           provider="Kimi", plan="Coding Plan"),
    Preset("glm", "Z.AI · GLM-5.3 · standard API", "https://api.z.ai/api/paas/v4", "glm-5.3", GLM_EXTRA,
           1_000_000, "glm", "payg", "https://z.ai/manage-apikey/apikey-list",
           "Z.AI open platform, pay-as-you-go.",
           provider="Z.AI (GLM)", plan="standard API"),
    # https://docs.z.ai/devpack/quick-start lists the Coding Plan's OpenAI base verbatim.
    Preset("glm-coding", "Z.AI · GLM-5.3 · Coding Plan", "https://api.z.ai/api/coding/paas/v4", "glm-5.3", GLM_EXTRA,
           1_000_000, "glm", "subscription", "https://z.ai/manage-apikey/apikey-list",
           "GLM Coding Plan subscription; subscribe at z.ai/subscribe.",
           provider="Z.AI (GLM)", plan="Coding Plan"),
    # MiniMax renamed the Coding Plan to the Token Plan and it shares the pay-as-you-go base URL; only the
    # key differs and the two kinds of key are not interchangeable.
    # https://platform.minimax.io/docs/token-plan/other-tools.md, .../token-plan/quickstart
    # Models: https://platform.minimax.io/docs/api-reference/api-overview (MiniMax-M3, M2.7,
    # MiniMax-M2.7-highspeed, M2.5, M2.1, …). M3 is 1,000,000 tokens; the M2.x family is 204,800.
    Preset("minimax", "MiniMax · M3 · Coding/Token Plan", "https://api.minimax.io/v1", "MiniMax-M3", {},
           1_000_000, "none", "subscription", "https://platform.minimax.io/user-center/payment/token-plan",
           "MiniMax Coding Plan is now the Token Plan; the same base URL serves both key kinds.",
           provider="MiniMax", plan="Token Plan"),
    # https://openrouter.ai/api/v1/models (fetched 2026-09-17): deepseek/deepseek-v4.1-flash exists,
    # a non-flash deepseek/deepseek-v4.1 does not.
    Preset("openrouter", "OpenRouter · DeepSeek V4.1 Flash", "https://openrouter.ai/api/v1",
           "deepseek/deepseek-v4.1-flash", {}, 1_048_576, "openrouter", "aggregator",
           "https://openrouter.ai/keys", "One key for every model; also Relay's router and chores model.",
           provider="OpenRouter"),
    # https://developers.openai.com/api/docs/api-reference/chat/create
    Preset("openai", "OpenAI · GPT-6 Astra", "https://api.openai.com/v1", "gpt-6-astra",
           {"reasoning_effort": "high"}, 1_050_000, "openai", "payg",
           "https://platform.openai.com/api-keys", "Pay-as-you-go OpenAI API key (not a ChatGPT login).",
           provider="OpenAI (ChatGPT)", plan="Pay-as-you-go"),
    # https://platform.claude.com/docs/en/cli-sdks-libraries/libraries/openai-sdk — the OpenAI-compatible
    # layer lives at https://api.anthropic.com/v1 and accepts the key as an Authorization: Bearer header.
    # It ignores reasoning_effort, so Relay sends no effort parameters to it.
    Preset("anthropic", "Anthropic · Claude Opus 5", "https://api.anthropic.com/v1", "claude-opus-5", {},
           1_000_000, "none", "payg", "https://console.anthropic.com/settings/keys",
           "Anthropic's OpenAI-compatible endpoint; effort is the model's own default.",
           provider="Anthropic (Claude)", plan="Pay-as-you-go"),
    # https://ai.google.dev/gemini-api/docs/openai — base URL is .../v1beta/openai (stored without the
    # trailing slash because Relay appends /chat/completions).
    Preset("gemini", "Google · Gemini 3.1 Pro", "https://generativelanguage.googleapis.com/v1beta/openai",
           "gemini-3.1-pro-preview", {"reasoning_effort": "high"}, 1_048_576, "gemini", "payg",
           "https://aistudio.google.com/apikey", "Gemini API key from AI Studio.",
           provider="Google (Gemini)", plan="Pay-as-you-go"),
]}

# --- Main / Flash / Lite tiers (docs/AGENT-SESSIONS-PROTOCOL.md section 13.7) ----------------------
# Three models per provider instead of eight roles. Each entry is (preset id, model, extra); a tier may
# point at another provider, in which case a missing key falls back one tier towards Main (see
# roles.RoleResolver._tier). Tier ids are also used by the GUI's roles modal.
TIERS = ("main", "flash", "lite")
TIER_LABELS = {"main": "Main", "flash": "Flash", "lite": "Lite"}
TIER_HINTS = {"main": "the pane's agent and subagents",
              "flash": "terminal use and quick turns",
              "lite": "titles, labels, duplicate checks"}

# The cross-provider Lite default: Gemini 3.8 Flash through OpenRouter (slug verified against
# https://openrouter.ai/api/v1/models on 2026-09-17).
_LITE_VIA_OPENROUTER = ("openrouter", "google/gemini-3.8-flash", {})

TIER_DEFAULTS: dict[str, dict[str, tuple[str, str, dict]]] = {
    "glm": {"main": ("glm", "glm-5.3", GLM_EXTRA),
            "flash": ("glm", "glm-5.3-flash", GLM_FAST_EXTRA),
            "lite": _LITE_VIA_OPENROUTER},
    "glm-coding": {"main": ("glm-coding", "glm-5.3", GLM_EXTRA),
                   "flash": ("glm-coding", "glm-5.3-flash", GLM_FAST_EXTRA),
                   "lite": _LITE_VIA_OPENROUTER},
    # reasoning_effort is documented for kimi-k3 only, so the high-speed models carry no effort field.
    "kimi": {"main": ("kimi", "kimi-k3", {"reasoning_effort": "high"}),
             "flash": ("kimi", "kimi-k2.7-code-highspeed", {}),
             "lite": _LITE_VIA_OPENROUTER},
    "kimi-code": {"main": ("kimi-code", "k3", {"reasoning_effort": "high"}),
                  "flash": ("kimi-code", "kimi-for-coding-highspeed", {}),
                  "lite": _LITE_VIA_OPENROUTER},
    # No non-flash DeepSeek V4.1 exists on OpenRouter, so Main and Flash are the same model there.
    "openrouter": {"main": ("openrouter", "deepseek/deepseek-v4.1-flash", {}),
                   "flash": ("openrouter", "deepseek/deepseek-v4.1-flash", {}),
                   "lite": ("openrouter", "google/gemini-3.5-flash-lite", {})},
    "minimax": {"main": ("minimax", "MiniMax-M3", {}),
                "flash": ("minimax", "MiniMax-M2.7-highspeed", {}),
                "lite": _LITE_VIA_OPENROUTER},
    # https://platform.claude.com/docs/en/models/overview
    "anthropic": {"main": ("anthropic", "claude-opus-5", {}),
                  "flash": ("anthropic", "claude-sonnet-5", {}),
                  "lite": ("anthropic", "claude-haiku-4-5", {})},
    "openai": {"main": ("openai", "gpt-6-astra", {"reasoning_effort": "high"}),
               "flash": ("openai", "gpt-5.6-terra", {"reasoning_effort": "medium"}),
               "lite": ("openai", "gpt-5.6-luna", {"reasoning_effort": "low"})},
    "gemini": {"main": ("gemini", "gemini-3.1-pro-preview", {"reasoning_effort": "high"}),
               "flash": ("gemini", "gemini-3.8-flash", {}),
               "lite": ("gemini", "gemini-3.5-flash-lite", {})},
}

# Recommended default-provider pairings shown in the roles modal.
RECOMMENDED = (("glm-coding", "openrouter"), ("kimi-code", "openrouter"))


def validate_tier(tier) -> str:
    if tier not in TIERS:
        raise ValueError("tier must be one of main, flash, lite.")
    return tier


def tier_default(preset_id: str | None, tier: str) -> tuple[str, str, dict] | None:
    """The (preset, model, extra) a provider uses for a tier, or None for an unknown provider."""
    return TIER_DEFAULTS.get(preset_id or "", {}).get(validate_tier(tier))


def provider_tier_model(preset_id: str, tier: str) -> tuple[str, dict]:
    """That provider's own (model, extra) for a tier.

    Naming a provider for a tier means "your Flash model on Z.AI", not "Z.AI's headline model":
    picking Z.AI for Flash must give glm-5.3-flash, not glm-5.3. A tier whose built-in entry points at
    another provider (Lite is Gemini through OpenRouter for everyone) does not drag that provider in
    when the user asked for this one; the nearest tier that stays on the provider is used instead, and
    a provider with no tier table at all falls back to its preset model.
    """
    for candidate in tier_fallbacks(tier):          # this tier first, then towards Main
        entry = tier_default(preset_id, candidate)
        if entry is not None and entry[0] == preset_id:
            return entry[1], dict(entry[2])
    preset = PRESETS[preset_id]
    return preset.model, dict(preset.extra)


def tier_fallbacks(tier: str) -> tuple[str, ...]:
    """Tiers to try, in order, when a tier's provider has no stored key: towards Main, then Main."""
    order = TIERS[:TIERS.index(validate_tier(tier)) + 1]
    return tuple(reversed(order))


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
    """Relay levels that map to different provider values (Kimi/GLM have no medium).

    Empty for the "none" style: the provider's OpenAI-compatible endpoint has no effort knob, so
    every level would send the same request and the GUI offers no choice.
    """
    if style == "none":
        return []
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
    validate_effort(effort)
    if style == "none":
        # Anthropic's compat layer ignores reasoning_effort and MiniMax has no such field; sending one
        # would be noise at best and a 400 at worst, so the model's own default stands.
        return result, {}
    value = EFFORT_MAP[style][effort]
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
    if style == "none":
        return None
    value = (extra.get("reasoning") or {}).get("effort") if style == "openrouter" else extra.get("reasoning_effort")
    if not isinstance(value, str):
        return None
    for level in ("low", "high", "max", "medium"):
        if EFFORT_MAP[style][level] == value:
            return level
    return "max" if value in ("max", "xhigh") else None


def context_window_for(preset: Preset | None) -> int:
    return preset.context_window if preset is not None else DEFAULT_CONTEXT_WINDOW

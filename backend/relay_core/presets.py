# SPDX-License-Identifier: AGPL-3.0-or-later
"""Built-in provider presets and the Main/Flash/Lite tier table.

The GUI mirrors both in src/main.cpp (``presetMirror()`` / ``tierMirror()``); tests/test_presets.py
checks that the mirror has not drifted. Every endpoint and model id below was verified against the
provider's own documentation on 2026-09-17; the doc URL sits next to the entry it supports.
"""
from __future__ import annotations

import copy
import re
from collections.abc import Mapping
from dataclasses import dataclass, field

from . import model_ranking

# Relay's own four words. Until 2026-09-21 they were the universe: every provider's levels were
# mapped onto them and the GUI showed the provider's word through a second table (`effort_labels`).
# The owner ended that — "i want the effort options in relay to be determined by the model … so
# xhigh shows up for codex for example" — so a model's levels are now **exactly what its provider
# offers**, in the provider's own words, and the level word travels the wire as it is.
#
# These four survive for one job: reading what an older client sends. A GUI that still holds
# Relay's four asks codex for `max`, and `nearest_effort` turns that into the level the model
# actually has. Nothing writes them any more.
EFFORTS = ("low", "medium", "high", "max")

# Every level word any provider Relay knows uses, weakest first. It is the ladder `nearest_effort`
# walks, and nothing else: a word off it is still a perfectly good level, it just cannot be
# compared to another one. "ultra" is codex's delegation mode and sits at the top because it is
# the most work, not because anyone defaults to it.
EFFORT_LADDER = ("low", "medium", "high", "xhigh", "max", "ultra")

# What each request style's endpoint takes for a reasoning level, **in its own words**, weakest
# first. This replaced EFFORT_MAP, the {Relay level: provider value} table, on 2026-09-21: a
# mapping table has to answer "what is medium on Kimi" for every provider whether or not the
# provider has a medium, and the answer was always a fiction the picker then had to hide. The list
# of words the endpoint takes has no such hole. Verified 2026-09-17 against:
#
# - Kimi K3: https://platform.kimi.ai/docs/api/chat (top-level reasoning_effort: low|high|max, default max,
#   and only on kimi-k3; thinking cannot be disabled).
# - GLM-5.3: https://docs.z.ai/guides/capabilities/thinking (thinking.type accepts only "enabled";
#   reasoning_effort low|high|max, default max).
# - DeepSeek: https://api-docs.deepseek.com/api/create-chat-completion/ and .../guides/thinking_mode/
#   (reasoning_effort none|low|high|max; thinking is on by default and `thinking: {"type":
#   "disabled"}` turns it off).
# - OpenRouter: https://openrouter.ai/docs/use-cases/reasoning-tokens.md (reasoning.effort: none|minimal|low|
#   medium|high|xhigh|max; xhigh and max get the same ~95% budget, so only xhigh is offered).
# - OpenAI: https://developers.openai.com/api/docs/api-reference/chat/create (reasoning_effort none|minimal|
#   low|medium|high|xhigh|max, per-model subsets; gpt-6-astra rejects "none", so Relay never sends it).
# - Gemini: https://ai.google.dev/gemini-api/docs/openai (reasoning_effort minimal|low|medium|high; "minimal"
#   is rejected by gemini-3.8-flash and "none" only works on 2.5 models, so Gemini's top is high).
# - "none": providers with no usable effort knob on their OpenAI-compatible endpoint. Anthropic's compat
#   layer ignores reasoning_effort and rejects `thinking` on Claude 5
#   (https://platform.claude.com/docs/en/cli-sdks-libraries/libraries/openai-sdk); MiniMax has no
#   reasoning_effort at all and only M3 can disable thinking
#   (https://platform.minimax.io/docs/api-reference/text-chat-openai.md). An empty list is what
#   greys the effort box (`effort_fixed`).
# - "relay": Relay Free (owner, 2026-09-18) offers low and medium, because the gateway clamps each
#   role to its ceiling and a picker that offered more would only pretend.
#
# The two guest harnesses are not here: a guest reports its own levels over its own protocol
# (codex low|medium|high|xhigh|max|ultra, claude code low|medium|high|xhigh|max, and a subset per
# model), which is exactly the shape this table now has for everyone else.
EFFORT_LEVELS: dict[str, tuple[str, ...]] = {
    "kimi": ("low", "high", "max"),
    "glm": ("low", "high", "max"),
    # DeepSeek takes the same three words as Kimi and GLM. It is its own style rather than "glm"
    # because `apply_effort` has to send `thinking` with the level and the two providers disagree
    # about "off": GLM-5.3 cannot be turned off, DeepSeek can (the Lite row below is exactly
    # that), so a level picked here must switch thinking back on.
    "deepseek": ("low", "high", "max"),
    "openrouter": ("low", "medium", "high", "xhigh"),
    "openai": ("low", "medium", "high", "xhigh"),
    "gemini": ("low", "medium", "high"),
    "none": (),
    "relay": ("low", "medium"),
    "relay-pro": ("low", "medium", "high"),
}


# Context windows in tokens (verified 2026-09-17; see docs/INTAKE-CLARIFICATION-RESEARCH.md section 4).
# GLM-5.3 uses Z.AI's documented 1,000,000 (OpenRouter lists 1,310,720, but some OpenRouter GLM endpoints
# only offer ~262K; the openrouter preset here is DeepSeek, which is 1,048,576 on every endpoint).
DEFAULT_CONTEXT_WINDOW = 128_000  # conservative fallback for unknown/custom models

# --- output limits ------------------------------------------------------------------------------
# What one model call may be asked to produce, per model, from the provider's own documentation
# (verified 2026-09-18; the URL sits next to each entry below). This is *not* a fraction of the
# context window: the two are published independently, and Gemini 3.1 Pro is the proof — the same
# 1,048,576-token window as GLM-5.3 and half the output cap (65,536 against 131,072). Reasoning is
# spent from this budget on every provider that streams it, so it is also the ceiling on how long a
# model may think in one step (card #G5MK).
#
# The fallback is deliberately conservative: an endpoint Relay cannot name — a custom base URL, an
# OpenRouter route, anything behind a gateway — may cap output far lower than its window suggests
# (OpenRouter's DeepInfra route for Kimi K3 allows 16,384), and a request over the cap is refused
# outright rather than trimmed.
DEFAULT_MAX_OUTPUT = 32_768

# --- image (vision) support -------------------------------------------------------------------
# Which models can read images, by model-id prefix (issue EM1E, owner decision 2026-09-17). An
# OpenRouter-style slug is matched on its last segment, so "google/gemini-3.8-flash" counts as Gemini.
#
# GLM-5.3 is text-only and Z.AI's image model is GLM-5.3-Flash; that is exactly why a turn carrying
# an image on GLM swaps to Flash for that turn and back afterwards (roles.VISION_DEFAULTS). Kimi K3
# and the K2.5+ models read images (OpenRouter lists text+image input for kimi-k3, kimi-k2.7-code,
# kimi-k2.6 and kimi-k2.5; kimi-k2, kimi-k2-0905 and kimi-k2-thinking are text-only, which the
# "kimi-k2." prefix keeps out). DeepSeek's Flash reads images and its Pro does not
# (https://api-docs.deepseek.com/quick_start/pricing, checked 2026-09-21: Vision is a tick for
# `deepseek-flash` and "Not supported" for `deepseek-v4-pro`), which is why a turn carrying an image
# on DeepSeek swaps to Flash the way GLM does. MiniMax M3 is text-only, so an image turn there needs
# a configured vision model and is otherwise refused with a message instead of being sent and
# rejected.
VISION_MODELS: tuple[str, ...] = (
    "gpt-", "chatgpt-", "o3", "o4",                              # OpenAI: the GPT family reads images
    "claude-",                                                   # Anthropic: Claude 3 and later
    "gemini-",                                                   # Google: the Gemini family
    "glm-5.3-flash", "glm-4.5v", "glm-4.6v", "glm-5v",           # Z.AI: the Flash and V models
    "kimi-k3", "kimi-k2.",                                       # Moonshot: K3 and K2.5+ read images
    # DeepSeek: the Flash line only. "deepseek-flash" is the first-party id and its moving
    # -latest alias; "deepseek-v4.1-flash" is the same model's name and OpenRouter's slug.
    "deepseek-flash", "deepseek-v4.1-flash", "deepseek-v4-flash",
    "qwen-vl", "qwen2-vl", "qwen3-vl", "pixtral", "llava", "minimax-vl",
    "kimi-latest", "moonshot-v1-8k-vision", "moonshot-v1-32k-vision", "moonshot-v1-128k-vision",
)


def model_supports_vision(model) -> bool:
    """Whether a model id names a model that can read images. Unknown ids count as text-only."""
    if not isinstance(model, str):
        return False
    name = model.strip().lower().rsplit("/", 1)[-1]
    return bool(name) and any(name.startswith(prefix) for prefix in VISION_MODELS)


# Keys-modal grouping (GUI only; the backend never treats groups differently). "included" is first
# because Relay Free is what a fresh install runs on before any key is stored.
GROUPS = ("included", "subscription", "aggregator", "payg", "local", "custom")
GROUP_LABELS = {"included": "Included", "subscription": "Subscriptions", "aggregator": "Aggregator",
                "payg": "Pay-as-you-go", "local": "On this machine", "custom": "Custom"}


# --- how a provider is reached, per row of the `presets` event (protocol 13.2) -------------------
# `kind` and `order` are that provider's row of `model-ranking.md`, sent with the provider rather
# than kept a second time in the GUI. Until card #MDL1 the client held its own copy of rule 2.2 as a
# hard-coded list (src/ModelCatalog.cpp `accessRank`: plan, then guest, then api, then OpenRouter,
# then Relay Free); the owner edits the file, so the file has to be what the picker sorts by, and
# `grouped()` now reads these two off the row and falls back to that list only for a row with
# neither.
#
# The fallback is deliberately safe, because the ranking file is a file a person edits and may be
# half-way through: a provider it does not name is `api` at `UNKNOWN_PROVIDER_ORDER`, which is after
# every provider it does name. Nothing here raises and nothing is refused — a row simply stops
# carrying an opinion and sorts last among its peers. That covers a custom provider the user added,
# a model server on this machine, and a preset the file has not caught up with yet.
DEFAULT_PROVIDER_KIND = "api"


def provider_rank(preset_id, hosted: bool = False) -> tuple[str, int]:
    """``(kind, order)`` for a provider, from the ranking file, with the safe pair above.

    ``hosted`` and a ``guest:`` id are the two cases the row itself settles, so they keep their
    kind even with no row at all: Relay's own allowance is `free` and a guest CLI is a `harness`.
    """
    rank = model_ranking.load()
    kind = rank.provider_kind(preset_id)
    if not kind:
        if hosted:
            kind = "free"
        elif isinstance(preset_id, str) and preset_id.startswith("guest:"):
            kind = "harness"
        else:
            kind = DEFAULT_PROVIDER_KIND
    return kind, rank.provider_order(preset_id)


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
    # The company, not the model: the roles modal picks a *provider*, and "kimi · k3" read as if the
    # tier were pinned to K3. `plan` tells two presets of the same provider apart when both are listed.
    # Every label, provider and plan is lower-case (owner, 2026-09-20, Warp style): the GUI shows
    # them as they are and never re-cases them.
    provider: str = ""
    plan: str = ""
    # A model server on this machine (localmodels.py): no key, plain HTTP to a loopback host. Never
    # set on an entry of PRESETS; localmodels.LocalEndpoint.as_preset() is the only producer.
    local: bool = False
    server: str = ""           # llamacpp | ollama | lmstudio | vllm | openai-compatible
    # Output tokens one call may ask this model for; see DEFAULT_MAX_OUTPUT above. Last in the field
    # list because every entry below is built positionally up to `server`.
    max_output: int = DEFAULT_MAX_OUTPUT
    # Relay's own hosted service (hosted.py): no key either, but for the opposite reason to
    # `local`. The worker proves an installation identity and gets a short-lived bearer token, so
    # the row is usable with nothing stored and its requests leave the machine through Relay.
    hosted: bool = False
    # A custom provider (customproviders.py): a named OpenAI-compatible endpoint the user added,
    # with a key under its own `custom:<slug>` id and the model ids they gave. Never set on an
    # entry of PRESETS; CustomProvider.as_preset() is the only producer.
    custom: bool = False

    @property
    def vision(self) -> bool:
        """Whether this preset's default model can read images. Derived, so it cannot drift."""
        return model_supports_vision(self.model)

    def to_dict(self) -> dict:
        kind, order = provider_rank(self.id, self.hosted)
        return {"id": self.id, "label": self.label, "base_url": self.base_url,
                "model": self.model, "extra": dict(self.extra), "context_window": self.context_window,
                "max_output": self.max_output,
                # The levels this endpoint takes, in its own words (effort_levels): the word the
                # picker offers is the word that is sent. `effort_fixed` is "grey the box": no
                # knob at all, or Relay Free, whose gateway clamps each role whatever is asked.
                "efforts": effort_levels(self.effort_style),
                "effort_fixed": effort_fixed(effort_levels(self.effort_style), self.id == "relay-free"),
                "effort_note": effort_note(self.effort_style), "group": self.group,
                "key_url": self.key_url, "note": self.note, "vision": self.vision,
                "provider": self.provider or self.label.split(" · ")[0], "plan": self.plan,
                "local": self.local, "server": self.server, "hosted": self.hosted,
                "custom": self.custom,
                # How this provider is reached and where it sorts against the others serving the
                # same model (`provider_rank` above, `model-ranking.md`'s Providers table).
                "kind": kind, "order": order,
                # The per-model catalog (MODEL_CATALOG below): [] for a local endpoint, whose one
                # served model is `model` and whose own list comes from the probe (protocol 28).
                "models": catalog_rows(self.id)}


GLM_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "high"}
# GLM-5.3-Flash cannot turn thinking off: "GLM-5.3 and GLM-5.3-FLASH no longer support disabling thinking
# (an error will occur if the thinking.type parameter ... is set to disabled)"
# — https://docs.z.ai/guides/capabilities/thinking. The cheapest legal setting is reasoning_effort low.
GLM_FAST_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "low"}

# DeepSeek's switch is the same shape and it *can* be turned off: "thinking" takes enabled | disabled
# and is enabled by default (https://api-docs.deepseek.com/api/create-chat-completion/). Relay has no
# "off" among its four levels — a level is how *hard* a model thinks, not whether it does — so "off"
# is a property of the request the tier makes, exactly as GLM's Flash row pins its level: Main asks
# for high, Flash for low, and Lite (titles, labels, duplicate checks) turns thinking off outright
# rather than paying for reasoning tokens on a chore. Picking any level on a Lite row switches
# thinking back on, because `apply_effort` sends the pair (see the "deepseek" style in EFFORT_MAP).
DEEPSEEK_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "high"}
DEEPSEEK_FAST_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "low"}
DEEPSEEK_NO_THINKING_EXTRA = {"thinking": {"type": "disabled"}}

PRESETS: dict[str, Preset] = {p.id: p for p in [
    # Relay Free (owner decision 2026-09-18): a modest included allowance so a fresh install's first
    # ask works with no key. The gateway (gateway/) speaks the OpenAI shape, names its models after
    # the tiers, and caps reasoning at medium per role (EFFORT_MAP["relay"] mirrors the cap, so the
    # picker offers Low and Medium). The context window is the gateway's input cap, not a model's.
    Preset("relay-free", "relay free", "https://api.relay-terminal.ai/v1", "relay-main",
           {"reasoning_effort": "medium"},
           1_000_000, "relay", "included", "https://relay-terminal.ai/free.html",
           "Included with Relay, no API key. Prompts go to Relay's hosted service, then to the model provider.",
           provider="relay", plan="included", hosted=True,
           # The gateway owns the real cap and applies it per role, clamping rather than refusing
           # (`min(max_tokens, role.max_output_tokens)` in gateway/validate.py). This is the cap it
           # serves the Main role, the one a pane's turns run on: 16,000
           # (gateway/gateway.example.json; Flash 12,000 and Lite 512 are clamped down from here).
           # Asking for the old 32,768 fallback was not refused, but it made Relay's own number
           # twice the truth — the reserve it holds back, and the budget a cut-off turn reports.
           # Server-side and remotely configurable, so this tracks the shipped config, not a
           # provider's published limit like the rows above.
           max_output=16_000),
    Preset("relay-pro", "relay pro", "https://api.relay-terminal.ai/v1", "relay-pro-main",
           {"reasoning_effort": "medium"}, 1_000_000, "relay-pro", "subscription",
           "https://relay-terminal.ai", "Per-person access to GLM 5.3 and GLM 5.3 Flash through Relay.",
           provider="relay", plan="pro", hosted=True, max_output=16_000),
    Preset("kimi", "kimi · k3", "https://api.moonshot.ai/v1", "kimi-k3", {"reasoning_effort": "high"},
           1_048_576, "kimi", "payg", "https://platform.kimi.ai/console/api-keys",
           "Moonshot platform, pay-as-you-go.",
           # max_completion_tokens: 131,072 by default, up to the whole window
           # (https://platform.kimi.ai/docs/guide/kimi-k3-quickstart).
           provider="kimi", plan="pay-as-you-go", max_output=131_072),
    # Kimi Code subscription (https://www.kimi.com/code/docs/en/): OpenAI-compatible base
    # https://api.kimi.ai/coding/v1; model ids k3, k3-256k, kimi-for-coding, kimi-for-coding-highspeed.
    Preset("kimi-code", "kimi code · k3", "https://api.kimi.ai/coding/v1", "k3", {"reasoning_effort": "high"},
           1_048_576, "kimi", "subscription", "https://www.kimi.com/code/console",
           "Kimi Code subscription key (not a Moonshot platform key).",
           provider="kimi", plan="coding plan", max_output=131_072),
    Preset("glm", "z.ai · glm-5.3 · standard api", "https://api.z.ai/api/paas/v4", "glm-5.3", GLM_EXTRA,
           1_000_000, "glm", "payg", "https://z.ai/manage-apikey/apikey-list",
           "Z.AI open platform, pay-as-you-go.",
           # Output 128K (https://docs.z.ai/guides/llm/glm-5.3).
           provider="z.ai (glm)", plan="standard api", max_output=131_072),
    # https://docs.z.ai/devpack/quick-start lists the Coding Plan's OpenAI base verbatim.
    Preset("glm-coding", "z.ai · glm-5.3 · coding plan", "https://api.z.ai/api/coding/paas/v4", "glm-5.3", GLM_EXTRA,
           1_000_000, "glm", "subscription", "https://z.ai/manage-apikey/apikey-list",
           "GLM Coding Plan subscription; subscribe at z.ai/subscribe.",
           provider="z.ai (glm)", plan="coding plan", max_output=131_072),
    # MiniMax renamed the Coding Plan to the Token Plan and it shares the pay-as-you-go base URL; only the
    # key differs and the two kinds of key are not interchangeable.
    # https://platform.minimax.io/docs/token-plan/other-tools.md, .../token-plan/quickstart
    # Models: https://platform.minimax.io/docs/api-reference/api-overview (MiniMax-M3, M2.7,
    # MiniMax-M2.7-highspeed, M2.5, M2.1, …). M3 is 1,000,000 tokens; the M2.x family is 204,800.
    Preset("minimax", "minimax · m3 · coding/token plan", "https://api.minimax.io/v1", "MiniMax-M3", {},
           1_000_000, "none", "subscription", "https://platform.minimax.io/user-center/payment/token-plan",
           "MiniMax Coding Plan is now the Token Plan; the same base URL serves both key kinds.",
           # Recommended output limit 131,072, hard maximum 524,288, and input plus output must fit
           # the window (https://platform.minimax.io/docs/guides/text-generation).
           provider="minimax", plan="token plan", max_output=131_072),
    # https://openrouter.ai/api/v1/models (fetched 2026-09-17): deepseek/deepseek-v4.1-flash exists,
    # a non-flash deepseek/deepseek-v4.1 does not.
    # Titled "openrouter" alone (owner, 2026-09-20): the model is one of hundreds the router serves,
    # not the preset's name, and its catalog is the live listing (openrouter_catalog.py).
    Preset("openrouter", "openrouter", "https://openrouter.ai/api/v1",
           "deepseek/deepseek-v4.1-flash", {}, 1_048_576, "openrouter", "aggregator",
           "https://openrouter.ai/keys", "One key for every model; also Relay's router and chores model.",
           # DeepSeek documents 384,000 output, but an aggregator picks the endpoint and the caps
           # differ across them (OpenRouter's DeepInfra route for Kimi K3 allows 16,384), so this
           # one keeps the conservative fallback rather than a number one route may refuse.
           provider="openrouter", max_output=DEFAULT_MAX_OUTPUT),
    # https://developers.openai.com/api/docs/api-reference/chat/create
    # The preset's model is its main model, gpt-6-sol (owner, 2026-09-22); its requests carry the
    # main tier's `medium`. These extras are what a model no tier names runs with (astra, terra).
    Preset("openai", "openai · gpt-6 sol", "https://api.openai.com/v1", "gpt-6-sol",
           {"reasoning_effort": "high"}, 1_050_000, "openai", "payg",
           "https://platform.openai.com/api-keys", "Pay-as-you-go OpenAI API key (not a ChatGPT login).",
           # 1,050,000 context, 128,000 output (owner, 2026-09-22; gpt-6-astra has the same).
           provider="openai (chatgpt)", plan="pay-as-you-go", max_output=128_000),
    # https://platform.claude.com/docs/en/cli-sdks-libraries/libraries/openai-sdk — the OpenAI-compatible
    # layer lives at https://api.anthropic.com/v1 and accepts the key as an Authorization: Bearer header.
    # It ignores reasoning_effort, so Relay sends no effort parameters to it.
    Preset("anthropic", "anthropic · claude opus 5.5", "https://api.anthropic.com/v1", "claude-opus-5-5", {},
           1_000_000, "none", "payg", "https://console.anthropic.com/settings/keys",
           "Anthropic's OpenAI-compatible endpoint; effort is the model's own default.",
           # 1M context, 128k max output (https://platform.claude.com/docs/en/models/opus-5-5/overview);
           # the 300k output beta is the Message Batches API, not this one.
           provider="anthropic (claude)", plan="pay-as-you-go", max_output=131_072),
    # https://ai.google.dev/gemini-api/docs/openai — base URL is .../v1beta/openai (stored without the
    # trailing slash because Relay appends /chat/completions).
    Preset("gemini", "google · gemini 3.1 pro", "https://generativelanguage.googleapis.com/v1beta/openai",
           "gemini-3.1-pro-preview", {"reasoning_effort": "high"}, 1_048_576, "gemini", "payg",
           "https://aistudio.google.com/apikey", "Gemini API key from AI Studio.",
           # "1M / 64k" — the same window as GLM-5.3 and half the output cap, which is why Relay
           # reads this per model instead of as a share of the window
           # (https://ai.google.dev/gemini-api/docs/gemini-3).
           provider="google (gemini)", plan="pay-as-you-go", max_output=65_536),
    # DeepSeek's own API (https://api-docs.deepseek.com/quick_start/pricing and
    # .../api/create-chat-completion/, checked 2026-09-21): OpenAI-compatible at
    # https://api.deepseek.com, two models. `deepseek-v4-pro` is DeepSeek-V4-Pro-0813, the
    # headline one; `deepseek-flash` is DeepSeek-V4.1-Flash, and it is the preset's model on all
    # three tiers since 2026-09-21.
    # Both document a 1M window, which is 1,048,576 on every endpoint that serves them (the same
    # number the `openrouter` preset above carries for the Flash line).
    #
    # The output cap is the one number here that is not the largest the provider will accept.
    # DeepSeek takes max_tokens "between 1 and 384K (393216)", but that is a ceiling on the ask,
    # not what it produces: unset, it answers with "8K in non-thinking mode, 64K in thinking mode
    # (128K with reasoning_effort set to max)". 128K is therefore the real budget of a thinking
    # turn, it is what every sibling preset above documents, and it stays inside the quarter of the
    # window Relay keeps for the reply against the compaction reserve
    # (tests/test_presets.py::test_every_preset_documents_an_output_cap_that_fits_its_window).
    # Asking for 384K of a 1M window would leave the reply fighting that reserve for a length no
    # DeepSeek turn actually reaches.
    # The preset's own model is the Flash line, not Pro (owner, 2026-09-21: "deepseek pro is
    # never used ... use deepseek-flash for all"). Pro keeps its catalog row and its row in the
    # ranking file, classed for nothing: it can still be picked, it is just nobody's default.
    Preset("deepseek", "deepseek · v4.1 flash", "https://api.deepseek.com", "deepseek-flash",
           DEEPSEEK_EXTRA, 1_048_576, "deepseek", "payg",
           "https://platform.deepseek.com/api_keys", "DeepSeek platform, pay-as-you-go.",
           provider="deepseek", plan="pay-as-you-go", max_output=131_072),
]}

# --- Main / Flash / Lite tiers (docs/AGENT-SESSIONS-PROTOCOL.md section 13.7) ----------------------
# Three models per provider instead of eight roles. Each entry is (preset id, model, extra); a tier may
# point at another provider, in which case a missing key falls back one tier towards Main (see
# roles.RoleResolver._tier). Tier ids are also used by the GUI's roles modal.
# "local" is a fourth tier and is NOT one of these three: it belongs to no provider, so it has no
# row in TIER_DEFAULTS. It resolves from the local-endpoint registry instead (roles._tier_entry).
# "high" (owner, 2026-09-20: "a 'high' default on top of main, used by the planner by default") is
# a fifth, listed first: with no `tiers.high` override it is the pane's own model pushed to max
# reasoning (roles.RoleResolver._high_default), so it has no TIER_DEFAULTS row either — no provider
# has an obvious "bigger" model to name — and an override resolves the way Flash and Lite do.
PROVIDER_TIERS = ("main", "flash", "lite")
TIERS = ("high", "main", "flash", "lite", "local")
# Lower-case, like every other word Relay writes for a tier (card #MDL1, rule 1: "tier and role
# words are lower-case too: main, flash, not Main agent"). These reach a person in the notes the
# roles modal shows inline — "No stored key for the flash model; using main."
TIER_LABELS = {"high": "high", "main": "main", "flash": "flash", "lite": "lite", "local": "local"}
TIER_HINTS = {"high": "plan mode and the hardest turns; main at max reasoning unless you pick a model",
              "main": "the pane's agent and subagents",
              "flash": "terminal use and quick turns",
              "lite": "titles, labels, duplicate checks",
              "local": "a model served on this machine. No key, nothing leaves it"}

# The cross-provider Lite default: Gemini 3.8 Flash through OpenRouter (slug verified against
# https://openrouter.ai/api/v1/models on 2026-09-17).
_LITE_VIA_OPENROUTER = ("openrouter", "google/gemini-3.8-flash", {})

TIER_DEFAULTS: dict[str, dict[str, tuple[str, str, dict]]] = {
    # Relay Free's three tiers are the gateway's three roles; nothing steps out to another provider,
    # because that would need a key the row exists to do without.
    "relay-free": {"main": ("relay-free", "relay-main", {"reasoning_effort": "medium"}),
                   "flash": ("relay-free", "relay-flash", {"reasoning_effort": "low"}),
                   "lite": ("relay-free", "relay-lite", {})},   # the gateway's Lite default: minimal
    "relay-pro": {"high": ("relay-pro", "relay-pro-high", {"reasoning_effort": "high"}),
                  "main": ("relay-pro", "relay-pro-main", {"reasoning_effort": "medium"}),
                  "flash": ("relay-pro", "relay-pro-flash", {"reasoning_effort": "low"}),
                  "lite": ("relay-free", "relay-lite", {})},
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
    "anthropic": {"main": ("anthropic", "claude-opus-5-5", {}),
                  "flash": ("anthropic", "claude-sonnet-5", {}),
                  "lite": ("anthropic", "claude-haiku-4-5", {})},
    "openai": {"main": ("openai", "gpt-6-sol", {"reasoning_effort": "medium"}),
               "flash": ("openai", "gpt-6-luna", {"reasoning_effort": "low"}),
               "lite": ("openai", "gpt-6-luna", {"reasoning_effort": "low"})},
    "gemini": {"main": ("gemini", "gemini-3.1-pro-preview", {"reasoning_effort": "high"}),
               "flash": ("gemini", "gemini-3.8-flash", {}),
               "lite": ("gemini", "gemini-3.5-flash-lite", {})},
    # DeepSeek serves its own Lite, so this is the one first-party API that needs no OpenRouter key
    # to fill all three: Flash at its lowest level, and Lite the same model with thinking off.
    "deepseek": {"main": ("deepseek", "deepseek-flash", DEEPSEEK_EXTRA),
                 "flash": ("deepseek", "deepseek-flash", DEEPSEEK_FAST_EXTRA),
                 "lite": ("deepseek", "deepseek-flash", DEEPSEEK_NO_THINKING_EXTRA)},
}

# Recommended default-provider pairings shown in the roles modal.
RECOMMENDED = (("glm-coding", "openrouter"), ("kimi-code", "openrouter"))

# --- the per-model catalog ---------------------------------------------------------------------
# What a provider row can be set to, one row per model, keyed by preset id (owner, 2026-09-20): the
# model box lists these instead of a free text field, the way a guest row's own `models` already
# does (guest_harness_provider.py, protocol 29.3). Every model id here is one the repo already
# names — TIER_DEFAULTS, the comments beside each preset, docs/ARCHITECTURE.md, or the codex
# catalogue (#E516) — and nothing was invented: a model the provider serves but nothing here
# mentions is left out until somebody verifies it, rather than offered and refused.
#
# Row shape, as `catalog_rows()` hands it to the GUI:
#   id           the model id the API takes (for OpenRouter, the slug)
#   name         what a person reads: lower-case, no spaces, no vendor prefix (`model_name`, card
#                #MDL1). Only the handful that cannot be derived from the id carry a `name` in the
#                table below; `catalog_rows` fills it in for every row.
#   label        the same string. It is the key an older GUI and the phone read, kept so they show
#                the name too; nothing computes a second, prettier spelling any more.
#   tier         main | flash | lite | None — the tier this model is the built-in default for. A
#                tier that points at another preset (Lite via OpenRouter) puts its row on the
#                *target* preset, so "google/gemini-3.8-flash" is a lite row of `openrouter`. A
#                model two tiers name (DeepSeek on OpenRouter) carries the first in PROVIDER_TIERS
#                order; a model no tier names carries None.
#   efforts      the levels this model accepts, in its provider's own words, or None for
#                "whatever the preset's endpoint offers" (effort_levels). The value the GUI sees
#                is always a list. Since 2026-09-21 the word here is the word sent — openai and
#                openrouter say "xhigh" rather than being shown "xhigh" for a stored "max" —
#                which is the owner's "i want the effort options in relay to be determined by the
#                model … so xhigh shows up for codex".
#   effort_fixed true when the effort box must be greyed (`effort_fixed`): a model with no knob at
#                all, and every Relay Free model, whose gateway clamps each role whatever is
#                asked. The GUI still shows the level; it just cannot be moved.
#   intelligence INTELLIGENCE[id], or None.
#
# `efforts` is only ever narrowed below what the preset offers: Kimi documents reasoning_effort
# for kimi-k3 alone (the TIER_DEFAULTS comment above), so its other models carry [], exactly as
# their tier entries carry no effort field.
MODEL_CATALOG: dict[str, list[dict]] = {
    "relay-free": [
        {"id": "relay-main", "tier": "main", "efforts": None},
        {"id": "relay-flash", "tier": "flash", "efforts": None},
        {"id": "relay-lite", "tier": "lite", "efforts": None},
    ],
    "relay-pro": [
        {"id": "relay-pro-high", "name": "glm-5.3", "tier": "high", "efforts": None},
        {"id": "relay-pro-main", "name": "glm-5.3", "tier": "main", "efforts": None},
        {"id": "relay-pro-flash", "name": "glm-5.3-flash", "tier": "flash", "efforts": None},
    ],
    "kimi": [
        {"id": "kimi-k3", "tier": "main", "efforts": None},
        {"id": "kimi-k2.7-code-highspeed", "tier": "flash", "efforts": []},
    ],
    # https://www.kimi.com/code/docs/en/ — k3, k3-256k, kimi-for-coding, kimi-for-coding-highspeed.
    # Kimi Code's "k3" is the same model the `kimi` preset calls "kimi-k3", so it is named that and
    # the two fold into one row; "k3-256k" is a serving variant and keeps its own name (§3.3).
    "kimi-code": [
        {"id": "k3", "name": "kimi-k3", "tier": "main", "efforts": None},
        {"id": "k3-256k", "tier": None, "efforts": None},
        {"id": "kimi-for-coding", "tier": None, "efforts": []},
        {"id": "kimi-for-coding-highspeed", "tier": "flash", "efforts": []},
    ],
    "glm": [
        {"id": "glm-5.3", "tier": "main", "efforts": None},
        {"id": "glm-5.3-flash", "tier": "flash", "efforts": None},
    ],
    "glm-coding": [
        {"id": "glm-5.3", "tier": "main", "efforts": None},
        {"id": "glm-5.3-flash", "tier": "flash", "efforts": None},
    ],
    # https://platform.minimax.io/docs/api-reference/api-overview — no effort knob on any of them.
    "minimax": [
        {"id": "MiniMax-M3", "tier": "main", "efforts": None},
        {"id": "MiniMax-M2.7", "tier": None, "efforts": None},
        {"id": "MiniMax-M2.7-highspeed", "tier": "flash", "efforts": None},
        {"id": "MiniMax-M2.5", "tier": None, "efforts": None},
    ],
    "openrouter": [
        {"id": "deepseek/deepseek-v4.1-flash", "tier": "main", "efforts": None},
        {"id": "google/gemini-3.8-flash", "tier": "lite", "efforts": None},
        {"id": "google/gemini-3.5-flash-lite", "tier": "lite", "efforts": None},
    ],
    # The four codex-cli 0.155.1 lists first (#E516); every one takes reasoning_effort.
    "openai": [
        {"id": "gpt-6-astra", "tier": None, "efforts": None},
        {"id": "gpt-6-sol", "tier": "main", "efforts": None},
        {"id": "gpt-5.6-terra", "tier": None, "efforts": None},
        {"id": "gpt-6-luna", "tier": "flash", "efforts": None},
    ],
    # https://platform.claude.com/docs/en/models/overview — the compat layer has no effort knob.
    # Anthropic's API spells two versions with a hyphen where everyone else (and OpenRouter) uses a
    # dot, so those two carry a `name`: without it the same model would sit in the picker twice.
    "anthropic": [
        {"id": "claude-opus-5-5", "name": "claude-opus-5.5", "tier": "main", "efforts": None},
        {"id": "claude-sonnet-5", "tier": "flash", "efforts": None},
        {"id": "claude-haiku-4-5", "name": "claude-haiku-4.5", "tier": "lite", "efforts": None},
        {"id": "claude-fable-5-1", "name": "claude-fable-5.1", "tier": None, "efforts": None},
    ],
    # Google publishes a moving alias beside each concrete version, and the owner asked for both
    # ("i want gemini latest aliases", 2026-09-21). An alias keeps its own name (design 3.4): it
    # is a different thing to pick — you are choosing "whatever Gemini Pro is this week" — so it
    # is its own row rather than a second spelling of today's model. Same levels as the rest of
    # the family; the ranking file classes the two aliases and leaves the concrete rows classed
    # for nothing, so the defaults follow Google forward.
    "gemini": [
        {"id": "gemini-pro-latest", "tier": None, "efforts": None},
        {"id": "gemini-flash-latest", "tier": None, "efforts": None},
        {"id": "gemini-3.1-pro-preview", "tier": "main", "efforts": None},
        {"id": "gemini-3.8-flash", "tier": "flash", "efforts": None},
        {"id": "gemini-3.5-flash-lite", "tier": "lite", "efforts": None},
    ],
    # https://api-docs.deepseek.com/api/list-models/ lists exactly these two ids.
    # `deepseek-flash` is a moving alias for whatever DeepSeek's Flash line currently is — today
    # DeepSeek-V4.1-Flash, which is what OpenRouter serves as `deepseek/deepseek-v4.1-flash` — so it
    # carries that model's name and folds into the row the `openrouter` preset already has (rule 1:
    # one model, one name, whoever serves it). `deepseek-v4-pro` names itself. The legacy ids
    # `deepseek-v4-flash` and `deepseek-v4-flash-vision-exp` are still accepted and served by the
    # same Flash model; they are left out, because a retired name is not a model a person picks.
    "deepseek": [
        {"id": "deepseek-flash", "name": "deepseek-v4.1-flash", "tier": "main", "efforts": None},
        {"id": "deepseek-v4-pro", "tier": None, "efforts": None},
    ],
}

# --- one model, one name (card #MDL1, docs/MODEL-PICKING-DESIGN.md rule 1) -----------------------
# Claude Code names its models by family ("fable", "opus"), so a guest reporting "opus" and the
# `anthropic` preset's "claude-opus-5-5" are one model. This is the only table that says so.
GUEST_MODEL_ALIASES = {"fable": "claude-fable-5-1", "opus": "claude-opus-5-5",
                       "sonnet": "claude-sonnet-5", "haiku": "claude-haiku-4-5"}

# The `name` a MODEL_CATALOG row carries, by model id, for the alias step below: the alias resolves
# to an API id, and that id may itself be one of the few that cannot be derived.
_NAME_OVERRIDES: dict[str, str] = {row["id"]: row["name"]
                                   for rows in MODEL_CATALOG.values() for row in rows if row.get("name")}


def derived_name(model_id) -> str:
    """The name of a model id nothing knows anything else about: lower-case, no vendor prefix, no
    spaces (owner, 2026-09-21: "model names should always be lowercase, no spaces").

    Everything up to the last "/" goes (``openai/gpt-6-sol`` and ``gpt-6-sol`` are one model,
    and stripping the prefix collides on none of OpenRouter's 446 ids), a leading "~" goes
    (OpenRouter writes a moving alias ``~openai/gpt-sol-latest``), and any whitespace becomes "-".
    A serving variant keeps whatever marks it: ``-highspeed``, ``:batch``, ``k3-256k``, ``-pro``
    are different models to the person picking one (design section 3.3).
    """
    text = (model_id or "").strip() if isinstance(model_id, str) else ""
    text = text.lstrip("~").rsplit("/", 1)[-1].lstrip("~").strip().lower()
    return "-".join(text.split())


def model_name(preset_id, model_id) -> str:
    """What this model is called, everywhere a person reads it. Never sent on the wire.

    In order (design rule 1): the ``name`` its own MODEL_CATALOG row carries, for the handful that
    cannot be derived (``kimi-code``'s "k3" is "kimi-k3"; Anthropic spells two versions with a
    hyphen where OpenRouter uses a dot); then a guest alias through GUEST_MODEL_ALIASES, so Claude
    Code's "opus" is "claude-opus-5.5"; else `derived_name`.
    """
    text = (model_id or "").strip() if isinstance(model_id, str) else ""
    if not text:
        return ""
    for row in MODEL_CATALOG.get(preset_id, ()) if isinstance(preset_id, str) else ():
        if row["id"] == text and row.get("name"):
            return row["name"]
    aliased = GUEST_MODEL_ALIASES.get(text)
    if aliased is not None:
        return _NAME_OVERRIDES.get(aliased) or derived_name(aliased)
    return _NAME_OVERRIDES.get(text) or derived_name(text)


# The owner's intelligence ruling, seeded 2026-09-20 from the Artificial Analysis Intelligence
# Index v4.3.2 (https://artificialanalysis.ai/leaderboards/models), each model at its highest
# reasoning level, as the picker's "intelligence" sort. Keyed by `model_name` (card #MDL1) rather
# than by API id, so one model is scored once however many providers serve it: "k3" on Kimi Code
# and "kimi-k3" on the Kimi platform used to be two hand-kept 44s.
#
# The numbers are no longer here. They are the `score` column of `model-ranking.md`, the file the
# owner asked to be able to "review and edit" (card #MDL1, design section 5.4), and this is a
# **view** over it: every reader — `catalog_rows`, the sort in `tier_list_defaults`, the GUI's
# `intelligence` field — goes on saying `INTELLIGENCE.get(name)` and `name in INTELLIGENCE` and
# gets the file's answer. None for a model whose row leaves the score blank *and* for a name with
# no row at all, which are the same thing to a sort: last, and blank in the GUI rather than a zero.
class _Scores(Mapping):
    """`model-ranking.md`'s Models table as `{name: score}`."""

    def _rows(self) -> dict:
        return model_ranking.load().models

    def __getitem__(self, name) -> int | None:
        return self._rows()[name].score

    def __iter__(self):
        return iter(self._rows())

    def __len__(self) -> int:
        return len(self._rows())

    def __repr__(self) -> str:
        return f"INTELLIGENCE({dict(self)!r})"


INTELLIGENCE: Mapping[str, int | None] = _Scores()


# --- the same model on OpenRouter (owner, 2026-09-20) ---------------------------------------------
# "If a model fails, fall back to the same model on OpenRouter": for each cloud catalog model id,
# the OpenRouter slug that serves that model. Every slug below was checked against the listing at
# https://openrouter.ai/api/v1/models (fetched 2026-09-20; no key needed), and a model with no
# listing is simply absent rather than guessed at: kimi-for-coding and kimi-for-coding-highspeed
# are Kimi Code's aliases for whatever it currently serves, not a named model; the relay-free rows
# are Relay's own gateway; and the openrouter preset's rows are already OpenRouter slugs. A
# "-highspeed" id is the same weights on a faster (pricier) serving tier, so it maps to the plain
# slug — the model is the same, the speed is not carried — and k3-256k is K3 at a wider window,
# which OpenRouter's kimi-k3 (1,048,576) covers. The failover (roles.openrouter_twin_candidate,
# protocol 15.2.2) uses this only for the models the user opted in per model: it spends the
# OpenRouter key at pay-as-you-go rates, which nobody wants to start doing for gpt-6 by surprise.
OPENROUTER_TWINS: dict[str, str] = {
    # z.ai
    "glm-5.3": "z-ai/glm-5.3",
    "glm-5.3-flash": "z-ai/glm-5.3-flash",
    # kimi (Moonshot platform) and Kimi Code
    "kimi-k3": "moonshotai/kimi-k3",
    "kimi-k2.7-code-highspeed": "moonshotai/kimi-k2.7-code",
    "k3": "moonshotai/kimi-k3",
    "k3-256k": "moonshotai/kimi-k3",
    # minimax
    "MiniMax-M3": "minimax/minimax-m3",
    "MiniMax-M2.7": "minimax/minimax-m2.7",
    "MiniMax-M2.7-highspeed": "minimax/minimax-m2.7",
    "MiniMax-M2.5": "minimax/minimax-m2.5",
    # openai
    "gpt-6-astra": "openai/gpt-6-astra",
    "gpt-6-sol": "openai/gpt-6-sol",
    "gpt-5.6-terra": "openai/gpt-5.6-terra",
    "gpt-6-luna": "openai/gpt-6-luna",
    # anthropic (OpenRouter spells the version with a dot)
    "claude-opus-5-5": "anthropic/claude-opus-5.5",
    "claude-sonnet-5": "anthropic/claude-sonnet-5",
    "claude-haiku-4-5": "anthropic/claude-haiku-4.5",
    "claude-fable-5-1": "anthropic/claude-fable-5.1",
    # deepseek (its own API). Both lines are on OpenRouter: the Flash alias is today's
    # DeepSeek-V4.1-Flash, and `deepseek/deepseek-v4-pro` is the Pro line's moving slug.
    "deepseek-v4-pro": "deepseek/deepseek-v4-pro",
    "deepseek-flash": "deepseek/deepseek-v4.1-flash",
    # google
    "gemini-3.1-pro-preview": "google/gemini-3.1-pro-preview",
    "gemini-3.8-flash": "google/gemini-3.8-flash",
    "gemini-3.5-flash-lite": "google/gemini-3.5-flash-lite",
}


def openrouter_twin(model_id) -> str | None:
    """The OpenRouter slug serving the same model as this catalog id, or None.

    None for a model OpenRouter does not list, for an id that is already an OpenRouter slug (it has
    a slash: nothing to twin), and for anything that is not a model id at all.
    """
    if not isinstance(model_id, str) or "/" in model_id:
        return None
    return OPENROUTER_TWINS.get(model_id.strip())


def catalog_rows(preset_id) -> list[dict]:
    """The catalog rows of a preset with `name` and `efforts` resolved, `intelligence` filled in
    and the model's OpenRouter twin named.

    `name` is `model_name(preset_id, id)` and `label` is the same string (card #MDL1): one model
    has one name wherever it is read, and the picker folds the providers that serve it into one
    row by that name.

    `efforts` of None becomes the levels the preset's effort style offers, so the GUI always gets a
    list and never has to know about styles. `openrouter` is `openrouter_twin(id)` — the slug that
    serves the same model there, or None — so the GUI can offer the per-model "fall back to the same
    model on OpenRouter" toggle only where there is one. An id with no catalog — a local endpoint, a
    guest, an unknown id — gets [] (a local endpoint's list is the probe's, protocol 28; a guest's
    is its own).

    `openrouter` is the one preset whose list does not end with the table above (owner,
    2026-09-20): after its built-in tier rows come OpenRouter's own live listing
    (openrouter_catalog.py — fetched on a background thread, cached for a day), every model not
    already named, in the listing's order, so the id box completes against what the router
    actually serves. Those rows carry `context_window` as well; the built-in ones do not.
    """
    preset = PRESETS.get(preset_id) if isinstance(preset_id, str) else None
    if preset is None:
        return []
    default = effort_levels(preset.effort_style)
    out = []
    for row in MODEL_CATALOG.get(preset_id, []):
        efforts = list(default if row["efforts"] is None else row["efforts"])
        # The provider's own default level for this model: the level its built-in tier extra names
        # (`infer_effort`), None where no tier names it and the model states nothing of its own. The
        # same key a guest row carries (protocol 29.3), and what `tier_start_efforts` starts Main at.
        provider_default = infer_effort(preset.effort_style, model_extra(preset_id, row["id"]))
        name = model_name(preset_id, row["id"])
        out.append({"id": row["id"], "name": name, "label": name, "tier": row["tier"],
                    "efforts": efforts,
                    "effort_fixed": effort_fixed(efforts, preset_id == "relay-free"),
                    "intelligence": INTELLIGENCE.get(name),
                    "openrouter": openrouter_twin(row["id"]),
                    "default_effort": provider_default,
                    "tier_effort": tier_start_efforts(efforts, provider_default, "", name)})
    if preset_id == "openrouter":
        from . import openrouter_catalog          # here, not at the top: it imports this module
        known = {row["id"] for row in out}
        out.extend(row for row in openrouter_catalog.rows() if row["id"] not in known)
    return out


def validate_tier(tier) -> str:
    if tier not in TIERS:
        raise ValueError("tier must be one of high, main, flash, lite, local.")
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
    preset = PRESETS.get(preset_id) or _local(preset_id)   # a local server has one model for every tier
    if preset is None:
        raise KeyError(preset_id)
    return preset.model, dict(preset.extra)


def tier_fallbacks(tier: str) -> tuple[str, ...]:
    """Tiers to try, in order, when a tier's provider has no stored key: towards Main, then Main.

    Local is not a step on that ladder — it is a different machine's worth of trade-off, not a
    smaller model of the same provider — so a Local tier with nothing set up goes straight to Main
    rather than through Lite and Flash. High sits above Main, so the only step down from it is
    Main itself.
    """
    if validate_tier(tier) in ("high", "local"):
        return (tier, "main")
    order = PROVIDER_TIERS[:PROVIDER_TIERS.index(tier) + 1]
    return tuple(reversed(order))


def model_extra(preset_id, model: str) -> dict:
    """The request extras a provider's model runs with when nothing says otherwise: the extras of
    the tier row that names it (GLM's Flash model runs at low reasoning, Kimi's high-speed model
    carries no effort field at all), else the preset's own.

    A tier *list* entry names a model rather than a tier (protocol 13.7), so the extras cannot be
    read off the tier it sits in: glm-5.3-flash is the same request whether the user ranked it under
    Flash, under Main or under High.
    """
    table = TIER_DEFAULTS.get(preset_id or "", {})
    for tier in PROVIDER_TIERS:
        entry = table.get(tier)
        if entry is not None and entry[0] == preset_id and entry[1] == model:
            return dict(entry[2])
    preset = PRESETS.get(preset_id) if isinstance(preset_id, str) else None
    return dict(preset.extra) if preset is not None else {}


def model_efforts(preset_id, model: str) -> list[str] | None:
    """The Relay levels a catalog model takes, or None when the catalog does not name the model
    (then the preset's effort style decides, as it always has). [] means the model has no effort
    knob — Kimi's high-speed models — and a level asked of it is not sent."""
    preset = PRESETS.get(preset_id) if isinstance(preset_id, str) else None
    if preset is None:
        return None
    for row in MODEL_CATALOG.get(preset_id, []):
        if row["id"] == model:
            return list(effort_levels(preset.effort_style) if row["efforts"] is None else row["efforts"])
    if preset_id == "openrouter":
        from . import openrouter_catalog
        for row in openrouter_catalog.rows():
            if row["id"] == model:
                return list(row["efforts"])
    return None


# --- the two default tier lists (owner, 2026-09-20) ---------------------------------------------
# Options › Models holds five ordered lists — main, high, flash, lite, local — and two buttons that
# fill them. What the buttons fill them *with* is computed here, from the providers usable right
# now, so the GUI only applies it (`tier_list_defaults` in the `presets` event, protocol 13.7).
#
# "openrouter twins are after the subscription models, and are cost sensitive" (owner): a twin is
# pay-as-you-go spending the user did not choose model by model, so the `openrouter` default only
# appends a twin whose completion price, as OpenRouter's live listing gives it, is at or under this
# many US dollars per million tokens. On 2026-09-20 that admits z-ai/glm-5.3 ($2.86),
# minimax/minimax-m3 ($1.20) and the flash models, and leaves out moonshotai/kimi-k3 ($8.50),
# openai/gpt-6-astra and anthropic/claude-opus-5.5 ($25-50). A twin whose price is unknown (no
# listing fetched yet) is left out of Main and High, where a wrong guess is expensive, and kept in
# Flash and Lite, where every twin there is cheap.
OPENROUTER_TWIN_MAX_COMPLETION_USD_PER_MTOK = 3.0

# Which *kind* of access comes first, by the `group` a preset carries. Hand-written until card
# #MDL1; now read off `model-ranking.md`'s Providers table, whose `order` column is the group order
# (design section 5.4: "the provider order replaces the group order"). A group is the lowest order
# of the built-ins that carry it — so `subscription` is the first plan, `payg` the first api — and
# `guest` is the first harness and `custom` sorts with `payg`, because no entry of PRESETS carries
# either group — a custom provider is an OpenAI-compatible pay-as-you-go endpoint the user added,
# which is where the hand-written table put it too. A group nothing covers (`local`) sorts after
# everything that has a row; a model server on this machine is never ranked anyway.
#
# The ranking itself goes through `Ranking.provider_order` per provider. This is what `_order_of`
# falls back to for a provider with no row of its own, which is the custom ones.
def _group_orders() -> dict[str, int]:
    rank = model_ranking.load()
    out: dict[str, int] = {}
    for preset_id, preset in PRESETS.items():
        order = rank.provider_order(preset_id)
        if order < out.get(preset.group, order + 1):
            out[preset.group] = order
    harnesses = [row.order for row in rank.providers.values() if row.kind == "harness"]
    out["guest"] = min(harnesses, default=model_ranking.UNKNOWN_PROVIDER_ORDER)
    out["custom"] = out.get("payg", model_ranking.UNKNOWN_PROVIDER_ORDER)
    return out


_MAIN_GROUP_ORDER = _group_orders()


def _order_of(rank, preset_id: str, group: str = "") -> int:
    """Where a provider sorts when two candidates tie on score: its own row of the ranking file,
    else the order of the `group` it belongs to (_MAIN_GROUP_ORDER), else after everything."""
    row = rank.providers.get(preset_id)
    if row is not None:
        return row.order
    return _MAIN_GROUP_ORDER.get(group, model_ranking.UNKNOWN_PROVIDER_ORDER)

# `LITE_LIST_FIRST` — what the openrouter default used to put first in the Lite list — is gone
# (2026-09-21). It said the same thing twice: the model is now `model-ranking.md`'s **Provider
# picks** row for `openrouter`, and a cell in the file the owner edits is the whole point of the
# file. `_LITE_VIA_OPENROUTER` above is untouched: that is the built-in Lite row a pane resolves
# to with no list at all, which is a different question from what a `defaults` button fills.


def _top_level(levels: list[str]) -> str | None:
    """The highest level of a model's own list, which is what High runs a model at, in the
    provider's own word: "max" on Kimi and GLM, "xhigh" on the OpenAI API and OpenRouter, "high"
    on Gemini, "medium" on Relay Free, None with no knob at all."""
    return levels[-1] if levels else None


def _low_level(preset_id: str, model: str) -> str | None:
    """The lowest level a model offers, which is what every Flash and Lite entry of the default
    lists says outright (owner, 2026-09-20: the lite list is "with no reasoning"): "low" on every
    provider with a knob, None only for a model with none (Kimi's high-speed ones). Explicit so the
    GUI shows a level rather than a blank; a model the catalog does not name gets its provider's."""
    levels = model_efforts(preset_id, model)
    if levels is None:
        preset = PRESETS.get(preset_id)
        levels = effort_levels(preset.effort_style) if preset is not None else []
    return levels[0] if levels else None


def _guest_top_level(guest_id: str, levels: list[str]) -> str | None:
    """A guest's top level for the High list, in the CLI's own words (protocol 29.3): Claude Code's
    last ("max"); Codex's "xhigh" when the model offers it (owner, 2026-09-20: "for codex planning
    you pick xhigh, not max"), else its last. None when the guest names no levels."""
    if guest_id == "codex" and "xhigh" in levels:
        return "xhigh"
    return levels[-1] if levels else None


def _list_entry(preset_id: str, model: str, effort: str | None) -> dict:
    entry = {"preset": preset_id, "model": model}
    if effort:
        entry["effort"] = effort
    return entry


def tier_start_efforts(efforts, default_effort: str | None = None, guest_id: str = "",
                       name: str = "") -> dict:
    """``{tier: level}`` for main, high, flash and lite: where a model starts when the user adds it
    to that list by hand (Options › Models' ``+ add a model…``, card #TKN7).

    ``model-ranking.md``'s **Levels** table first, by the model's ``name`` — the owner asked for
    "a similar defaults file for the reasoning levels across model X class" and then filled it in,
    so a cell there is the answer, mapped into this model's own vocabulary (`nearest_effort`,
    because one name can be served by two providers and `claude-opus-5.5` is `xhigh` through Claude
    Code and has no level at all through Anthropic's compat layer).

    A blank cell, or no ``name`` to look up, is the rule that was there before it, which is also
    the rule the ``defaults`` buttons fill the lists by, so a hand-added row and a filled list
    agree:

    * **main** — the provider's own default level for this model (``default_effort``: a guest's
      ``default_effort``, a cloud model's ``infer_effort`` of its tier extra), and ``None`` when
      the provider states none. Not the top level: Main is the pane's agent, and codex's top level
      is ``ultra``, a delegation mode rather than a reasoning depth (owner, 2026-09-21: *"it
      defaulted effort to ultra reasoning"*).
    * **high** — the model's top level, in the guest's own words where it has them
      (``_guest_top_level``: ``xhigh`` for codex, the owner's rule for a plan turn).
    * **flash** / **lite** — the model's lowest level (``_low_level``: Lite is "with no reasoning").

    ``None`` for a tier means the entry carries no level and the model's own default applies.
    """
    levels = [level for level in (efforts or []) if isinstance(level, str)]
    main = default_effort if default_effort in levels else None
    high = _guest_top_level(guest_id, levels) if guest_id else _top_level(levels)
    low = levels[0] if levels else None
    rule = {"main": main, "high": high, "flash": low, "lite": low}
    if not name:
        return rule
    rank = model_ranking.load()
    return {cls: (nearest_effort(rank.level(name, cls), levels) or rule[cls]) for cls in rule}


# The classes a guest harness may be a default for (`roles.GUEST_TIERS`, spelled here rather than
# imported: roles imports this module). Main is the pane's own agent and High is a plan turn, both
# of which start the harness deliberately. Flash joined them on 2026-09-21, at the owner's ask —
# "the worker should allow the harness for flash, and defaults should be the same across plans /
# apis / harnesses" — so Claude Code's sonnet can sit in the Flash list and a /flash pane can run
# on it. It serves the /flash *pane* only: every background job on that tier (terminal use,
# summaries, suggestions) skips a guest entry and steps down, because a side call of a running
# conversation cannot be handed a whole agent of its own (`roles.BACKGROUND_ROLES`). Lite is
# nothing but background jobs, so no guest is ever ranked into it.
GUEST_CLASSES = ("high", "main", "flash")

# What a provider the file classes for nothing stands in with, so it can still run a pane: High
# and Main, never the two chore classes. Spelled apart from GUEST_CLASSES since 2026-09-21, when
# a harness was let into Flash and the two stopped being the same tuple.
_STAND_IN_CLASSES = ("high", "main")


@dataclass(frozen=True)
class _Candidate:
    """One (provider, model) pair the defaults may choose, with what `model-ranking.md` says about
    it.

    ``provider`` is the company rather than the preset — `glm` and `glm-coding` are one z.ai, and
    both serve `glm-5.3` — because "at most one per provider per class" (owner, 2026-09-21: "dont
    pick 2 options from the same provider") is about who is being paid, not about which key is
    stored.
    """
    preset: str
    model: str
    name: str
    provider: str
    order: int
    score: int | None
    classes: tuple[str, ...]
    guest: str = ""                       # the guest id ("codex"), "" for everything else
    default_effort: str | None = None     # a guest model row's own default level
    levels: tuple[str, ...] = ()          # a guest model's own levels, for its High word


def _provider_identity(preset_id: str) -> str:
    """Who is being paid: a built-in preset's `provider` (the company), else the id itself — a
    guest harness is its own provider, and so is each custom endpoint."""
    preset = PRESETS.get(preset_id)
    return (preset.provider or preset.id) if preset is not None else preset_id


def _ranked_classes(rank, name: str, allowed=model_ranking.CLASSES) -> tuple[str, ...]:
    return tuple(cls for cls in rank.classes(name) if cls in allowed)


def provider_model_id(preset_id: str, name: str) -> str | None:
    """The model **id** this provider serves a model name with, or None when it does not serve it.

    A Provider picks cell names a model the way a person reads it, and which id carries it is the
    provider's business: `glm-5.3` is `glm-5.3` on Z.AI and `z-ai/glm-5.3` on OpenRouter. So the
    provider's own catalog rows are searched by name first, and then — for OpenRouter alone, whose
    catalog here is three built-in rows against a live listing of hundreds — the twin map, which
    is the one table that already says which slug serves which model (OPENROUTER_TWINS).
    """
    if not isinstance(preset_id, str) or not isinstance(name, str) or not name:
        return None
    for row in MODEL_CATALOG.get(preset_id) or []:
        if model_name(preset_id, row["id"]) == name:
            return row["id"]
    if preset_id == "openrouter":
        for model_id, slug in OPENROUTER_TWINS.items():
            if model_name("openrouter", slug) == name:
                return slug
    return None


def levels_for_name(name: str) -> tuple[str, ...]:
    """Every reasoning level any built-in provider offers for this model name, weakest first.

    One name, several providers, several vocabularies (design rule 1): `gpt-6-luna` takes `max`
    through codex and stops at `xhigh` through the OpenAI API. Only `model-ranking.md`'s `check()`
    asks this — "is `max` a level this model has anywhere?" — because a Levels cell is keyed by
    name and has no provider to be checked against. Guests are added by the caller, which is the
    module that already lists their level words.
    """
    out: list[str] = []
    for preset_id in PRESETS:
        for row in MODEL_CATALOG.get(preset_id) or []:
            if model_name(preset_id, row["id"]) != name:
                continue
            for level in model_efforts(preset_id, row["id"]) or ():
                if level not in out:
                    out.append(level)
    return tuple(sorted(out, key=lambda word: EFFORT_LADDER.index(word)
                        if word in EFFORT_LADDER else len(EFFORT_LADDER)))


def _builtin_candidates(preset_id: str, rank) -> list[_Candidate]:
    """Every model of a built-in preset's catalog, scored and classed by `model-ranking.md`, with
    that provider's **Provider picks** row applied on top of the shared Models classes.

    A picks cell — today only `openrouter` has a row — replaces this provider's candidate for that
    class outright: OpenRouter's Main is `glm-5.3-flash` whatever the Models table classes it as,
    because the picks table is exactly "a provider whose defaults differ from the shared rows". A
    cell naming a model this provider does not serve is ignored here and reported by `check()`,
    so a typo costs the class its pick rather than the provider its place in the list.
    """
    preset = PRESETS[preset_id]
    identity, order = _provider_identity(preset_id), rank.provider_order(preset_id)
    picked = {cls: provider_model_id(preset_id, rank.pick(preset_id, cls) or "")
              for cls in model_ranking.CLASSES}
    picked = {cls: model for cls, model in picked.items() if model}

    def classes_of(model_id: str) -> tuple[str, ...]:
        if preset_id == "relay-pro":
            # The same GLM model has distinct server routes for high and main. Keep each
            # route in its own class while ranking both by their shared model name.
            return (model_id.removeprefix("relay-pro-"),)
        name = model_name(preset_id, model_id)
        shared = tuple(cls for cls in _ranked_classes(rank, name) if cls not in picked)
        return tuple(cls for cls in model_ranking.CLASSES
                     if cls in shared or picked.get(cls) == model_id)

    out = [_Candidate(preset_id, row["id"], model_name(preset_id, row["id"]), identity, order,
                      rank.score(model_name(preset_id, row["id"])), classes_of(row["id"]))
           for row in MODEL_CATALOG.get(preset_id) or []]
    known = {candidate.model for candidate in out}
    for model_id in dict.fromkeys(picked.values()):
        # A pick the provider serves but its built-in catalog does not name — every OpenRouter one
        # but two, whose ids come out of the twin map.
        if model_id not in known:
            name = model_name(preset_id, model_id)
            out.append(_Candidate(preset_id, model_id, name, identity, order, rank.score(name),
                                  classes_of(model_id)))
    if not any("main" in c.classes for c in out):
        # A provider the file puts in no Main class still has to be able to run a pane, so its
        # built-in Main entry stands in — what this function answered before the file existed.
        entry = tier_default(preset_id, "main") or (preset_id, preset.model, preset.extra)
        name = model_name(preset_id, entry[1])
        out.append(_Candidate(preset_id, entry[1], name, identity, order, rank.score(name),
                              _STAND_IN_CLASSES))
    return out


def _usable_guest(row) -> bool:
    """A guest row of the `presets` event that can take a turn here: its harness runs on this
    machine and the CLI has not said it is signed out (protocol 29.3)."""
    return bool(isinstance(row, dict) and row.get("harness") and row.get("logged_in") is not False
                and isinstance(row.get("id"), str))


def _guest_candidates(row: dict, rank) -> list[_Candidate]:
    """A guest harness's own models, scored by **name** through the same table as everyone else's
    (owner, 2026-09-21): `gpt-6-astra` through codex scores what `gpt-6-astra` through the OpenAI
    API scores, and claude code's `opus` is `claude-opus-5.5`.

    Only the classes a guest may serve (GUEST_CLASSES). A guest whose list the file has never heard
    of — codex's catalogue is whatever the CLI ships — falls back to the first model its own list
    names, as this function always did, so an unscored guest is still the provider it is.
    """
    preset_id = row["id"]
    guest_id = row.get("guest") if isinstance(row.get("guest"), str) else preset_id.split(":", 1)[-1]
    order = rank.provider_order(preset_id)
    row_levels = tuple(level for level in (row.get("efforts") or []) if isinstance(level, str))

    def levels_of(model_row: dict) -> tuple[str, ...]:
        own = model_row.get("efforts") if isinstance(model_row.get("efforts"), list) else None
        return tuple(level for level in (own or row_levels) if isinstance(level, str))

    models = [m for m in row.get("models") or [] if isinstance(m, dict) and m.get("id")]
    out = [_Candidate(preset_id, m["id"], model_name(preset_id, m["id"]), preset_id, order,
                      rank.score(model_name(preset_id, m["id"])),
                      _ranked_classes(rank, model_name(preset_id, m["id"]), GUEST_CLASSES),
                      guest_id, m.get("default_effort") or None, levels_of(m))
           for m in models]
    if not any("main" in c.classes for c in out):
        first = models[0] if models else {}
        model = first.get("id") or ""          # "" is the guest's own default, as `tiers` reads it
        name = model_name(preset_id, model)
        out.append(_Candidate(preset_id, model, name, preset_id, order, rank.score(name),
                              GUEST_CLASSES, guest_id, first.get("default_effort") or None,
                              levels_of(first)))
    return out


def _custom_candidates(preset_id: str, model: str, rank) -> list[_Candidate]:
    """A custom provider (protocol 28.6): the one model the user gave it. Scored and classed by
    name where the file knows it — a hand-added `glm-5.3` is the same model — and High and Main
    otherwise, because a provider the user configured deliberately is a provider."""
    name = model_name(preset_id, model)
    return [_Candidate(preset_id, model or "", name, preset_id, _order_of(rank, preset_id, "custom"),
                       rank.score(name), _ranked_classes(rank, name) or _STAND_IN_CLASSES)]


def _pick(candidates: list[_Candidate], cls: str, limit: int) -> list[_Candidate]:
    """The `limit` models of one class: score descending (blank last), then the provider's `order`,
    then the name; at most one per provider, and never the same model twice."""
    ordered = sorted((c for c in candidates if cls in c.classes),
                     key=lambda c: (-(c.score if isinstance(c.score, int) else -1), c.order, c.name))
    out: list[_Candidate] = []
    providers: set[str] = set()
    names: set[str] = set()
    for candidate in ordered:
        if candidate.provider in providers or (candidate.name and candidate.name in names):
            continue
        providers.add(candidate.provider)
        names.add(candidate.name)
        out.append(candidate)
        if len(out) >= limit:
            break
    return out


def _candidate_levels(candidate: _Candidate) -> list[str]:
    """The levels this (provider, model) pair offers, in the provider's own words."""
    if candidate.guest:
        return list(candidate.levels)
    levels = model_efforts(candidate.preset, candidate.model)
    if levels is not None:
        return levels
    preset = PRESETS.get(candidate.preset)
    return effort_levels(preset.effort_style) if preset is not None else []


def _class_effort(candidate: _Candidate, cls: str) -> str | None:
    """The level a default entry carries: `model-ranking.md`'s **Levels** row for this model's
    name, and otherwise the same three rules `tier_start_efforts` applies to a hand-added row —
    Main the provider's own default, High the model's top level (in a guest's own word), Flash and
    Lite the lowest.

    The file's word is mapped onto this provider's own list (`nearest_effort`): the table is keyed
    by name and one name can be served two ways, so `gpt-6-luna | high = max` is `max` through
    codex and `xhigh` through the OpenAI API, which is the level that model actually has there.
    """
    listed = nearest_effort(model_ranking.load().level(candidate.name, cls), _candidate_levels(candidate))
    if listed:
        return listed
    if cls == "main":
        if candidate.guest:
            return candidate.default_effort
        preset = PRESETS.get(candidate.preset)
        if preset is None or model_efforts(candidate.preset, candidate.model) == []:
            return None
        return infer_effort(preset.effort_style, model_extra(candidate.preset, candidate.model))
    if cls == "high":
        if candidate.guest:
            return _guest_top_level(candidate.guest, list(candidate.levels))
        levels = model_efforts(candidate.preset, candidate.model)
        return _top_level(levels) if levels is not None else None
    return _low_level(candidate.preset, candidate.model)


def tier_list_defaults(usable, *, local=(), custom=(), guests=(), listing=None) -> dict:
    """``{"plain": {tier: [entry…]}, "openrouter": {tier: [entry…]}}``: what the two `defaults`
    buttons fill the five lists with, from `model-ranking.md` and what can take a turn right now.

    ``usable`` is the built-in preset ids that can (a stored key; Relay Free where it works);
    ``local`` the saved endpoints and ``custom`` the keyed custom providers, each as
    ``(id, model)``; ``guests`` the guest rows of the same ``presets`` event
    (guest_harness_provider.preset_rows), of which the usable ones — the harness runs here and the
    CLI has not said it is signed out — count as providers like any other. ``listing`` is
    OpenRouter's live rows (openrouter_catalog.rows(), the default), read for each twin's
    ``price_completion_per_mtok`` and for whether it takes a reasoning level at all. Every entry is
    ``{"preset", "model", "effort"?}``, the shape ``tiers`` takes, with ``effort`` a Relay level and
    absent where the model has no knob or the provider's default is no level at all.

    **How many** (owner, 2026-09-21; design section 5.4). Count the providers that can take a turn:
    a built-in with a key, a usable guest harness, a keyed custom provider — **not** a model server
    on this machine, and **not** Relay Free, which is what "none" means.

    * **none** → Relay Free's three rows (`relay-main` in high and main, `relay-flash`,
      `relay-lite`), and empty lists where Relay Free itself cannot run;
    * **one** → one model per class from that provider, the highest score whose `classes` names
      the class;
    * **two or more** → two per class, by score descending, at most one per provider per class and
      never the same model twice; a blank score sorts last and ties break by the provider's
      `order`, then by name.

    **Lite is the exception** (owner, 2026-09-21): while Relay Free can run, the plain `lite` list
    is `relay-lite` and nothing else, however many keys are stored — "for lite, i am thinking to
    simplify that and just everybody is on relay free by default, or openrouter if they want
    privacy". Without Relay Free it is ranked like any other class.

    **A provider whose defaults differ** from the shared rows says so in `model-ranking.md`'s
    Provider picks table, and that cell replaces its candidate for the class
    (`_builtin_candidates`). Only `openrouter` has a row today.

    Two presets of one company (`glm` and `glm-coding`) are one provider, so a class never holds
    the same model twice over; the plan wins the tie on `order`, so the credit is spent first.
    A guest's models are scored by name through the same table, and a guest is offered for High and
    Main only (GUEST_CLASSES). Relay Free never appears once anything else can take a turn.

    **The levels** are `model-ranking.md`'s Levels table, by the model's name, in the provider's
    own words and mapped onto the levels that provider actually has (`_class_effort`). A blank
    cell is the rule that was there before the table: `main` the provider's own default for that
    model, `high` its top level (a guest's in the CLI's own word, `_guest_top_level`: codex plans
    at `xhigh`), `flash` and `lite` its lowest (owner, 2026-09-20: Lite is "with no reasoning").
    `local` is the saved endpoints in their own order — it belongs to no provider and is not
    ranked.

    openrouter — the plain lists, then, only with an `openrouter` key, the OpenRouter twins of each
    list's models *after all of them*, cost-sensitive ones only
    (OPENROUTER_TWIN_MAX_COMPLETION_USD_PER_MTOK), a Flash or Lite twin at "low" where the listing
    says it takes a level. `lite` starts with OpenRouter's own Lite pick — the Provider picks row
    in `model-ranking.md` — with `relay-lite` behind it: chores are where the owner wants the
    choice between Relay's allowance and his own router key, and the two buttons are where he
    makes it. Without the key the two are equal.
    """
    rank = model_ranking.load()
    usable = [p for p in PRESETS if p in set(usable)]             # PRESETS order, built-ins only
    candidates: list[_Candidate] = []
    for preset_id in usable:
        if preset_id == "relay-free":
            continue                      # Relay Free is what "no providers" means, not a provider
        candidates.extend(_builtin_candidates(preset_id, rank))
    for row in guests:
        if _usable_guest(row):
            candidates.extend(_guest_candidates(row, rank))
    for preset_id, model in custom:
        candidates.extend(_custom_candidates(preset_id, model or "", rank))

    providers = {candidate.provider for candidate in candidates}
    limit = 2 if len(providers) > 1 else 1
    hosted_id = "relay-free" if "relay-free" in usable else ""
    if not providers:
        candidates = _builtin_candidates(hosted_id, rank) if hosted_id else []

    plain = {cls: [_list_entry(c.preset, c.model, _class_effort(c, cls))
                   for c in _pick(candidates, cls, limit)]
             for cls in ("main", "high", "flash", "lite")}
    plain["local"] = [_list_entry(endpoint_id, model or "", None) for endpoint_id, model in local]

    # Lite is Relay Free, whatever else is stored (owner, 2026-09-21: "for lite, i am thinking to
    # simplify that and just everybody is on relay free by default, or openrouter if they want
    # privacy"). The one exception to "Relay Free only with no providers", and deliberate: a Lite
    # call is a title, a label or a duplicate check, and spending a subscription or a metered key
    # on one is what the allowance is there to avoid. The providers' own Lite models are no
    # longer defaults at all — the owner emptied their `lite` cells in the file — so with no
    # Relay Free the list falls back to whatever the Models table still classes for lite, which
    # is the ranking every other class gets.
    hosted_lite = next((c for c in (_builtin_candidates(hosted_id, rank) if hosted_id else [])
                        if "lite" in c.classes), None)
    if hosted_lite is not None:
        plain["lite"] = [_list_entry(hosted_lite.preset, hosted_lite.model,
                                     _class_effort(hosted_lite, "lite"))]

    routed = {tier: [dict(entry) for entry in entries] for tier, entries in plain.items()}
    if "openrouter" in usable:
        if listing is None:
            from . import openrouter_catalog      # here, not at the top: it imports this module
            listing = openrouter_catalog.rows()
        live = {row["id"]: row for row in listing if isinstance(row, dict) and isinstance(row.get("id"), str)}

        def twin_low(slug: str) -> str | None:
            # "low" for a twin that takes a level: the listing's word first, the catalog's otherwise.
            efforts = (live.get(slug) or {}).get("efforts")
            if not isinstance(efforts, list):
                efforts = model_efforts("openrouter", slug)
            return None if efforts == [] else "low"

        # "…with openrouter" is the button for a user who would rather their chores went through
        # the router than through Relay's own allowance (owner, 2026-09-21: "everybody is on
        # relay free by default, or openrouter if they want privacy"). So this list starts with
        # OpenRouter's **own** Lite pick — `model-ranking.md`'s Provider picks row, which is
        # where that choice is written now rather than in a constant here — and Relay Free's
        # `relay-lite` follows it, so a router that is refusing calls does not leave Lite empty.
        lite_pick = provider_model_id("openrouter", rank.pick("openrouter", "lite") or "")
        if lite_pick:
            first = _list_entry("openrouter", lite_pick, twin_low(lite_pick))
            routed["lite"] = [first] + [entry for entry in routed["lite"]
                                        if (entry["preset"], entry["model"]) != ("openrouter", lite_pick)]
        for tier in ("main", "high", "flash", "lite"):
            have = {(entry["preset"], entry["model"]) for entry in routed[tier]}
            for entry in plain[tier]:
                slug = openrouter_twin(entry["model"])
                if slug is None or ("openrouter", slug) in have:
                    continue
                price = (live.get(slug) or {}).get("price_completion_per_mtok")
                price = price if isinstance(price, (int, float)) and not isinstance(price, bool) else None
                if price is None and tier in ("main", "high"):
                    continue
                if price is not None and price > OPENROUTER_TWIN_MAX_COMPLETION_USD_PER_MTOK:
                    continue
                have.add(("openrouter", slug))
                # High runs the twin at max too, unless the listing says it takes no level; a
                # Flash or Lite twin says "low" outright, like every entry of those two lists.
                takes_level = (live.get(slug) or {}).get("efforts") != []
                routed[tier].append(_list_entry("openrouter", slug,
                                                "max" if tier == "high" and takes_level else
                                                twin_low(slug) if tier in ("flash", "lite") else None))
    return {"plain": plain, "openrouter": routed}


def normalize_url(url: str) -> str:
    return url.strip().rstrip("/").lower()


def match_preset(base_url: str, model: str = "") -> Preset | None:
    """Find the preset for an endpoint. Model is only used to break ties."""
    candidates = [p for p in PRESETS.values() if normalize_url(p.base_url) == normalize_url(base_url)]
    for preset in candidates:
        if preset.id == "relay-pro" and model.strip().startswith("relay-pro-"):
            return preset
        if preset.model == model.strip():
            return preset
    return candidates[0] if candidates else None


def _local(preset_id, base_url: str = "", model: str = "") -> Preset | None:
    """A saved model server on this machine, as a Preset. Imported late: localmodels imports this
    module, and only a loopback URL or a `local:` id ever gets as far as reading its file."""
    from . import localmodels
    endpoint = localmodels.resolve(preset_id, base_url, model)
    return endpoint.as_preset() if endpoint is not None else None


def _custom(preset_id, base_url: str = "") -> Preset | None:
    """A saved custom provider (customproviders.py), by id or by its base URL. Imported late for
    the same reason as _local."""
    from . import customproviders
    entry = customproviders.find(preset_id) or customproviders.match(base_url)
    return entry.as_preset() if entry is not None else None


def resolve_preset(preset_id, base_url: str = "", model: str = "") -> Preset | None:
    if isinstance(preset_id, str) and preset_id in PRESETS:
        return PRESETS[preset_id]
    # Built-in endpoints first; match_preset itself stays cloud-only, because the key import and the
    # keyring use the id it returns and a `local:` id is not a keyring name.
    return match_preset(base_url or "", model or "") or _local(preset_id, base_url or "", model or "") \
        or _custom(preset_id, base_url or "")


def effort_levels(style: str) -> list[str]:
    """The levels this provider's endpoint takes, in its own words, weakest first.

    Empty for the "none" style, whose OpenAI-compatible endpoint has no effort knob at all — and
    an empty list is what greys the effort box (`effort_fixed`).

    Before 2026-09-21 this answered in Relay's four words and a second table said what each one
    was *sent* as. Now the word offered is the word sent: "xhigh" on OpenAI and OpenRouter, "high"
    as Gemini's top, "max" on Kimi and GLM, and nothing above "medium" on Relay Free. A style the
    table has never heard of gets Kimi's three, which is what `effort_style` falls back to for an
    endpoint Relay cannot name.
    """
    return list(EFFORT_LEVELS.get(style, EFFORT_LEVELS["kimi"]))


def nearest_effort(level, levels) -> str | None:
    """``level`` expressed in the vocabulary of ``levels``, or None when there is no knob at all.

    This is the whole of the compatibility read. One model name can be served by two providers
    with two vocabularies — `gpt-6-luna` is `max` through codex and has no `max` through the
    OpenAI API — and an older GUI still holds Relay's four words and will ask codex for `max`. So
    a level that the list does not have becomes **the weakest listed level that is at least as
    much work**, and the top of the list when there is none: `max` on the OpenAI API is `xhigh`,
    `max` on Gemini is `high`, `medium` on Kimi is `high`, `high` on Relay Free is `medium`. Those
    are exactly the answers the old {Relay level: provider value} table gave, which is why it
    could be deleted rather than kept beside this.

    A word neither the list nor the ladder knows is out of Relay's hands — a guest CLI may name a
    level nothing here has heard of — so it is handed back as written and the provider decides.
    """
    if not isinstance(level, str) or not level:
        return None
    level = level.strip().lower()
    listed = [word for word in (levels or []) if isinstance(word, str)]
    if not listed:
        return None
    if level in listed:
        return level
    if level not in EFFORT_LADDER:
        return level
    rank = EFFORT_LADDER.index(level)
    above = [word for word in listed if word in EFFORT_LADDER and EFFORT_LADDER.index(word) >= rank]
    if above:
        return min(above, key=EFFORT_LADDER.index)
    ranked = [word for word in listed if word in EFFORT_LADDER]
    return max(ranked, key=EFFORT_LADDER.index) if ranked else listed[-1]


def effort_note(style: str) -> str:
    """One line about this provider's levels, shown under the effort box, or "" when there is
    nothing to say.

    There used to be one per provider, naming the Relay levels it did not have and what each was
    silently sent as. Nothing is silently sent as anything any more — the picker offers the
    provider's own words — so the only note left is the one that is not about vocabulary: Relay
    Free's ceiling is the *gateway's*, applied per role, so even the two levels it offers are a
    request rather than a setting.
    """
    return ("Relay Free caps reasoning per role at the gateway, so a level above the cap is "
            "lowered there." if style == "relay" else "")


def effort_fixed(levels, hosted: bool = False) -> bool:
    """Whether the effort box must be greyed out for this model (owner, 2026-09-21: "for no knob
    models, the effort box should be grayed out. same for relay free").

    Two cases, and they are different kinds of "no". A model with no levels at all has no knob to
    turn — Kimi's high-speed models, Anthropic's compat layer, MiniMax. Relay Free *has* two
    levels and sends them, but the gateway clamps each role to its own ceiling
    (gateway/validate.py), so a box the user can move would only pretend: the number that decides
    is on the server. Either way the GUI shows the level and refuses to change it, rather than
    offering a control with no effect.
    """
    return bool(hosted or not [word for word in (levels or []) if isinstance(word, str)])


def effort_style(preset: Preset | None, extra: dict | None = None, base_url: str = "") -> str:
    if preset is not None:
        return preset.effort_style
    extra = extra or {}
    if "reasoning" in extra or "openrouter.ai" in (base_url or ""):
        return "openrouter"
    if "thinking" in extra:
        return "glm"
    return "kimi"


_EFFORT_WORD = re.compile(r"[a-z][a-z0-9-]{0,31}")


def validate_effort(effort, levels=None) -> str:
    """A reasoning level on its way in, checked against **the model it is for** (owner,
    2026-09-21: "i want the effort options in relay to be determined by the model").

    ``levels`` is that model's own list — a catalog row's ``efforts``, a guest row's, or the
    provider's (`effort_levels`). A level in it is returned as written. One of Relay's own four,
    or any other word on the ladder, is read through `nearest_effort`, which is what makes an
    older GUI's `max` land on codex's `max` and on the OpenAI API's `xhigh` rather than being
    refused. Anything else is a typo, and it raises, naming the levels the model does have.

    With ``levels`` left out — the callers that hold a level before they know which model will run
    it — or empty (a model with no knob at all), only the shape is checked: one short lower-case
    word, which is what every level either guest or any provider here names.

    Relay's own four are no longer the universe, so "effort must be one of low, medium, high, max"
    is gone as an error; what a model does not take, it says with its own list in the message.
    """
    if not isinstance(effort, str) or not _EFFORT_WORD.fullmatch(effort.strip().lower()):
        raise ValueError("effort must be one short lower-case word, such as low or high.")
    word = effort.strip().lower()
    listed = [level for level in (levels or []) if isinstance(level, str)]
    if levels is None or not listed:
        return word
    if word in listed:
        return word
    if word in EFFORT_LADDER:
        return nearest_effort(word, listed)
    raise ValueError(f"effort must be one of {', '.join(listed)}.")


def apply_effort(extra: dict | None, style: str, effort: str | None) -> tuple[dict, dict]:
    """Return (new extra, applied provider params). effort=None leaves extra unchanged.

    The level is sent in the provider's own words, because that is what it now is: the word the
    picker offered came out of `effort_levels`, which is the endpoint's own list. A word this
    endpoint does not take — an older client's, or a level carried over from another provider —
    is mapped onto the nearest one it does (`nearest_effort`) rather than sent and refused.
    """
    result = copy.deepcopy(extra or {})
    if effort is None:
        return result, {}
    effort = validate_effort(effort)
    if style == "none":
        # Anthropic's compat layer ignores reasoning_effort and MiniMax has no such field; sending one
        # would be noise at best and a 400 at worst, so the model's own default stands.
        return result, {}
    levels = effort_levels(style)
    value = nearest_effort(effort, levels)
    if value not in levels:
        # A word this endpoint has no level for and the ladder cannot place — a typo, or a guest
        # CLI's own level on a request-body provider. Relay sends nothing rather than a word it
        # knows the endpoint will refuse; the model's own default stands.
        return result, {}
    if style == "openrouter":
        reasoning = dict(result.get("reasoning") or {})
        reasoning.pop("max_tokens", None)  # OpenRouter accepts effort or max_tokens, not both
        reasoning["effort"] = value
        applied = {"reasoning": reasoning}
    elif style in ("glm", "deepseek"):
        # Both take the switch and the level together, and sending "enabled" is what makes picking a
        # level on a row whose extra says `disabled` (DeepSeek's Lite) mean what it says.
        applied = {"thinking": {"type": "enabled"}, "reasoning_effort": value}
    else:
        applied = {"reasoning_effort": value}
    result.update(copy.deepcopy(applied))
    return result, applied


def infer_effort(style: str, extra: dict | None) -> str | None:
    """The level the provider params already in ``extra`` describe, if any.

    This used to have to work backwards through the {Relay level: provider value} table and pick,
    of the two levels that could have sent this value, the one the pane could be put back on.
    There is nothing to work backwards through now: the value in the request *is* the level, so
    this reads it out and checks it is one the picker offers (`nearest_effort`), which keeps a
    level left behind by another provider — a `max` in a body now pointed at Gemini — from being
    reported as a level this endpoint has.
    """
    extra = extra or {}
    if style == "none":
        return None
    value = (extra.get("reasoning") or {}).get("effort") if style == "openrouter" else extra.get("reasoning_effort")
    if not isinstance(value, str) or not value.strip():
        return None
    return nearest_effort(value.strip().lower(), effort_levels(style))


def context_window_for(preset: Preset | None) -> int:
    return preset.context_window if preset is not None else DEFAULT_CONTEXT_WINDOW


def max_output_for(preset: Preset | None) -> int:
    return preset.max_output if preset is not None else DEFAULT_MAX_OUTPUT


def resolve_max_tokens(requested, preset: Preset | None) -> int:
    """The output budget to send for one call.

    ``requested`` of 0 (or nothing) means *automatic*: ask for what this model documents, which is
    what a new install does. A number the user pinned is kept, but never above the model's own cap
    when Relay knows it — asking Gemini for 131,072 is a refused request, not a longer answer. An
    endpoint Relay cannot name is left alone at whatever the user set: they configured that base URL
    themselves and know what it takes.
    """
    cap = max_output_for(preset)
    if not isinstance(requested, int) or isinstance(requested, bool) or requested <= 0:
        return cap
    return min(requested, cap) if preset is not None else requested

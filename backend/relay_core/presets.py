# SPDX-License-Identifier: GPL-3.0-or-later
"""Built-in provider presets. The GUI dialog mirrors this table in src/main.cpp."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Preset:
    id: str
    label: str
    base_url: str
    model: str
    extra: dict = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {"id": self.id, "label": self.label, "base_url": self.base_url,
                "model": self.model, "extra": dict(self.extra)}


GLM_EXTRA = {"thinking": {"type": "enabled"}, "reasoning_effort": "high"}

PRESETS: dict[str, Preset] = {p.id: p for p in [
    Preset("kimi", "Kimi · K3", "https://api.moonshot.ai/v1", "kimi-k3", {"reasoning_effort": "high"}),
    Preset("glm", "Z.AI · GLM-5.3 · standard API", "https://api.z.ai/api/paas/v4", "glm-5.3", GLM_EXTRA),
    Preset("glm-coding", "Z.AI · GLM-5.3 · Coding Plan", "https://api.z.ai/api/coding/paas/v4", "glm-5.3", GLM_EXTRA),
    Preset("openrouter", "OpenRouter · DeepSeek V4.1 Flash", "https://openrouter.ai/api/v1",
           "deepseek/deepseek-v4.1-flash", {}),
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

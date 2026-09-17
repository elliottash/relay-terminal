# SPDX-License-Identifier: GPL-3.0-or-later
"""API-key lookup: environment variable, then the desktop Secret Service keyring.

Keys are stored with ``secret-tool`` (libsecret), which works with GNOME Keyring
and KWallet's Secret Service provider. Secrets are passed on stdin, never argv,
and are never written to Relay files or returned in protocol events.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .presets import PRESETS, match_preset

SERVICE = "org.relayterminal.Relay"
WARP_SERVICE = "dev.warp.Warp"
TIMEOUT = 15
_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")


class KeystoreError(RuntimeError):
    pass


def env_name(preset_id: str) -> str:
    return "RELAY_" + preset_id.upper().replace("-", "_") + "_API_KEY"


def _secret_tool() -> str:
    tool = shutil.which("secret-tool")
    if not tool:
        raise KeystoreError("secret-tool (libsecret-tools) is not installed; set the key in an environment variable instead.")
    return tool


def _run(args: list[str], stdin: str | None = None) -> subprocess.CompletedProcess:
    try:
        return subprocess.run([_secret_tool(), *args], input=stdin, text=True,
                              capture_output=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        raise KeystoreError("The keyring did not respond. Unlock it and try again.") from None


def _check_id(preset_id: str) -> None:
    if not isinstance(preset_id, str) or not _ID.match(preset_id):
        raise ValueError("Invalid provider identifier.")


def lookup(preset_id: str) -> str:
    """Return the stored key for a provider, or an empty string when none exists."""
    _check_id(preset_id)
    value = os.environ.get(env_name(preset_id), "").strip()
    if value:
        return value
    # RELAY_KEYRING=off skips the desktop keyring entirely (tests and headless runs never prompt a
    # real keyring); environment keys above still work.
    if os.environ.get("RELAY_KEYRING", "").strip().lower() in ("off", "0", "no", "none"):
        return ""
    if not shutil.which("secret-tool"):
        return ""
    result = _run(["lookup", "service", SERVICE, "provider", preset_id])
    return result.stdout.strip() if result.returncode == 0 else ""


def store(preset_id: str, api_key: str) -> None:
    _check_id(preset_id)
    api_key = api_key.strip()
    if not api_key or len(api_key) > 4096 or any(c.isspace() for c in api_key):
        raise ValueError("API key must be non-empty text without whitespace.")
    result = _run(["store", "--label", f"Relay API key ({preset_id})", "service", SERVICE, "provider", preset_id],
                  stdin=api_key)
    if result.returncode != 0:
        raise KeystoreError("Could not save the key to the keyring. Is it unlocked?")


def remove(preset_id: str) -> bool:
    """Delete a provider's keyring entry. True when one was removed."""
    _check_id(preset_id)
    if os.environ.get("RELAY_KEYRING", "").strip().lower() in ("off", "0", "no", "none"):
        return False
    if not shutil.which("secret-tool"):
        return False
    result = _run(["clear", "service", SERVICE, "provider", preset_id])
    if result.returncode != 0:
        raise KeystoreError("Could not remove the key from the keyring. Is it unlocked?")
    return True


def key_source(preset_id: str) -> str:
    """Where a provider's key comes from: "env", "keyring" or "" when there is none.

    Never returns key material. The environment always wins, as in lookup().
    """
    _check_id(preset_id)
    if os.environ.get(env_name(preset_id), "").strip():
        return "env"
    return "keyring" if lookup(preset_id) else ""


def available() -> dict[str, bool]:
    return {preset_id: bool(lookup(preset_id)) for preset_id in PRESETS}


def sources() -> dict[str, str]:
    return {preset_id: key_source(preset_id) for preset_id in PRESETS}


@dataclass
class ImportedEndpoint:
    preset: str
    name: str
    model: str

    def to_dict(self) -> dict:
        return {"preset": self.preset, "name": self.name, "model": self.model}


def warp_settings_path() -> Path:
    base = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    return base / "warp-terminal" / "settings.toml"


def _toml10(text: str) -> str:
    """Rewrite TOML 1.1 multi-line inline tables (as Warp writes them) into TOML 1.0.

    Inside ``{...}`` newlines become spaces and trailing commas are dropped.
    String literals are copied untouched.
    """
    out: list[str] = []
    depth, i, n = 0, 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            quote = text[i:i + 3] if text[i:i + 3] in ('"""', "'''") else c
            end = i + len(quote)
            while end < n and not text.startswith(quote, end):
                end += 2 if quote == '"' and text[end] == "\\" else 1
            out.append(text[i:end + len(quote)])
            i = end + len(quote)
            continue
        if c == "#" and depth == 0:
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(text[i:end]); i = end
            continue
        if c == "{":
            depth += 1
        elif c == "}" and depth:
            depth -= 1
            while out and out[-1].isspace():
                out.pop()
            if out and out[-1] == ",":
                out.pop()
        elif depth and c in "\r\n":
            c = " "
        out.append(c)
        i += 1
    return "".join(out)


def read_warp_endpoints(settings_path: Path | None = None) -> dict[str, dict]:
    import tomllib
    path = settings_path or warp_settings_path()
    try:
        text = Path(path).read_text(encoding="utf-8")
        try:
            data = tomllib.loads(text)
        except tomllib.TOMLDecodeError:
            data = tomllib.loads(_toml10(text))
    except FileNotFoundError:
        raise KeystoreError(f"Warp settings not found at {path}.") from None
    except tomllib.TOMLDecodeError:
        raise KeystoreError("Warp settings are not valid TOML.") from None
    endpoints = data.get("agents", {}).get("custom_endpoints", {})
    return {k: v for k, v in endpoints.items() if isinstance(v, dict)}


def import_from_warp(settings_path: Path | None = None) -> tuple[list[ImportedEndpoint], list[str]]:
    """Copy keys for Warp's OpenAI-compatible custom endpoints into Relay's keyring entries.

    Returns (imported, skipped-endpoint-descriptions). Never returns key material.
    """
    endpoints = read_warp_endpoints(settings_path)
    result = _run(["lookup", "service", WARP_SERVICE, "key", "AiCustomEndpointKeys"])
    if result.returncode != 0 or not result.stdout.strip():
        raise KeystoreError("No Warp custom-endpoint keys were found in the keyring.")
    try:
        keys = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise KeystoreError("Warp's keyring entry has an unexpected format.") from None
    if not isinstance(keys, dict):
        raise KeystoreError("Warp's keyring entry has an unexpected format.")
    imported, skipped = [], []
    for endpoint_id, endpoint in endpoints.items():
        name = str(endpoint.get("name", endpoint_id))
        models = endpoint.get("models") or []
        model = str(models[0].get("name", "")) if models and isinstance(models[0], dict) else ""
        if endpoint.get("schema", "openai_chat_completions") != "openai_chat_completions":
            skipped.append(f"{name}: unsupported schema")
            continue
        preset = match_preset(str(endpoint.get("base_url", "")), model)
        key = keys.get(endpoint_id)
        if preset is None:
            skipped.append(f"{name}: no matching Relay preset")
        elif not isinstance(key, str) or not key.strip():
            skipped.append(f"{name}: no key stored in Warp")
        else:
            store(preset.id, key)
            imported.append(ImportedEndpoint(preset.id, name, model or preset.model))
    return imported, skipped


# --- other agent tools ---------------------------------------------------------------------------
# Claude Code and Codex normally sign in with OAuth, and an OAuth token is not an API key: it does not
# work on the OpenAI-compatible endpoints Relay talks to, so it is never imported. Only a plain API key
# that the user put in the tool's own config is copied, and only into the matching Relay preset.
CLAUDE_SETTINGS = ".claude/settings.json"
CODEX_AUTH = ".codex/auth.json"
_SOURCES = (
    # (label, path under $HOME, json path to the key, Relay preset)
    ("Claude Code", CLAUDE_SETTINGS, ("env", "ANTHROPIC_API_KEY"), "anthropic"),
    ("Codex", CODEX_AUTH, ("OPENAI_API_KEY",), "openai"),
)


def _json_at(path: Path, keys: tuple[str, ...]):
    try:
        if path.stat().st_size > 1024 * 1024:
            return None
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def import_from_agent_tools(home: Path | None = None) -> tuple[list[ImportedEndpoint], list[str]]:
    """Copy API keys out of Claude Code's and Codex's config files. Never returns key material."""
    base = Path(home) if home is not None else Path.home()
    imported, skipped = [], []
    for label, relative, keys, preset_id in _SOURCES:
        path = base / relative
        if not path.exists():
            skipped.append(f"{label}: no {relative} in your home directory")
            continue
        value = _json_at(path, keys)
        if not isinstance(value, str) or not value.strip() or any(c.isspace() for c in value.strip()):
            skipped.append(f"{label}: no API key in {relative} (an OAuth login is not an API key)")
            continue
        store(preset_id, value)
        imported.append(ImportedEndpoint(preset_id, label, PRESETS[preset_id].model))
    return imported, skipped


def warp_default_preset(settings_path: Path | None = None) -> str | None:
    """Return the Relay preset matching Warp's default agent model, if it is a custom endpoint."""
    try:
        import tomllib
        path = settings_path or warp_settings_path()
        text = Path(path).read_text(encoding="utf-8")
        try:
            data = tomllib.loads(text)
        except tomllib.TOMLDecodeError:
            data = tomllib.loads(_toml10(text))
    except (OSError, ValueError):
        return None
    agents = data.get("agents", {})
    profiles = agents.get("execution_profiles", {})
    wanted = profiles.get("default", {}).get("base_model") if isinstance(profiles.get("default"), dict) else None
    if not wanted:
        return None
    for endpoint in agents.get("custom_endpoints", {}).values():
        if not isinstance(endpoint, dict):
            continue
        for model in endpoint.get("models") or []:
            if isinstance(model, dict) and model.get("config_key") == wanted:
                preset = match_preset(str(endpoint.get("base_url", "")), str(model.get("name", "")))
                return preset.id if preset else None
    return None

# SPDX-License-Identifier: AGPL-3.0-or-later
"""The gateway's configuration: roles, upstreams, prices, quotas and ceilings, from one JSON file.

JSON rather than YAML because nothing in Relay's Python is pip-installed; a config the operator
edits by hand is small enough not to need comments (the README carries them). Provider keys are
never in the file: each provider names an environment variable (``key_env``) and the systemd unit
loads those from a mode-0600 ``EnvironmentFile``. A price is required for every model a role can
route to, because the spend ceilings must be able to cost every call; a missing one is a startup
error rather than an uncounted call.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit

LOOPBACK_HOSTS = frozenset({"127.0.0.1", "::1", "localhost"})
# Reasoning effort, lowest first. Relay Free runs at medium and below (owner, 2026-09-18): each
# role has a default for a client that names none and a ceiling the client's own ask is clamped to.
EFFORTS = ("none", "minimal", "low", "medium", "high", "xhigh", "max")
# How a provider takes the effort: OpenRouter nests it, the OpenAI shape (OpenAI, DeepSeek,
# Gemini's compatibility endpoint) takes a flat key, and "none" is for a model with no knob.
EFFORT_STYLES = ("reasoning", "reasoning_effort", "none")
ROLE_NAME_LIMIT = 64
# The largest body the HTTP layer accepts, whatever the roles say: the desktop's own request cap
# (backend/relay_core/provider.py MAX_RESPONSE), so a conversation the client would send fits.
MAX_REQUEST_BYTES = 8 * 1024 * 1024


class ConfigError(ValueError):
    """The file cannot be served from. The message says which key and why."""


@dataclass(frozen=True)
class Upstream:
    provider: str
    model: str
    extra: dict = field(default_factory=dict)


@dataclass(frozen=True)
class Role:
    name: str
    upstreams: tuple[Upstream, ...]
    max_output_tokens: int
    max_input_chars: int
    effort: str = "medium"           # when the client names none
    max_effort: str = "medium"       # the client's own ask is clamped to this


def effort_rank(effort: str) -> int:
    return EFFORTS.index(effort)


def clamp_effort(effort: str, ceiling: str) -> str:
    return effort if effort_rank(effort) <= effort_rank(ceiling) else ceiling


@dataclass(frozen=True)
class Provider:
    name: str
    base_url: str
    key_env: str
    # model -> (input USD per million tokens, output USD per million tokens)
    price_per_mtok: dict[str, tuple[float, float]]
    effort_style: str = "reasoning_effort"

    def key(self) -> str:
        """Read at request time, so a rotated key in the environment of a restarted process is
        the one used, and so the key is never held in the config object that gets logged."""
        return os.environ.get(self.key_env, "")

    def cost_micros(self, model: str, input_tokens: int, output_tokens: int) -> int:
        """The cost of one call in millionths of a dollar; integer so the sums stay exact."""
        price_in, price_out = self.price_per_mtok[model]
        return round(input_tokens * price_in + output_tokens * price_out)


@dataclass(frozen=True)
class Quota:
    tokens_per_day: int
    requests_per_minute: int
    concurrency_per_install: int


@dataclass(frozen=True)
class Limits:
    global_concurrency: int
    spend_per_day_usd: float
    spend_per_month_usd: float
    per_provider_per_day_usd: dict[str, float]
    registrations_per_ip_per_hour: int
    challenges_per_ip_per_hour: int


@dataclass(frozen=True)
class Config:
    roles: dict[str, Role]
    providers: dict[str, Provider]
    quota: Quota
    limits: Limits
    token_ttl_seconds: int
    # How long to wait for an upstream's response head before trying the next upstream, and how
    # long a stream may go silent once it has started before it is cut off.
    upstream_connect_timeout: float = 20.0
    upstream_stall_timeout: float = 120.0
    # A header carrying the client's address when the gateway sits behind cloudflared or another
    # reverse proxy, e.g. "CF-Connecting-IP". Without it every request would look like it came from
    # the proxy and the per-IP limits would throttle everyone together. Set it only when the port
    # is not reachable except through that proxy, or a client could forge it.
    client_ip_header: str | None = None
    # Plain http:// upstreams on loopback are for the tests' fake provider only. Also enabled by
    # GATEWAY_ALLOW_HTTP_LOOPBACK=1 so a test config need not carry the flag.
    allow_insecure_loopback: bool = False

    def provider_for(self, upstream: Upstream) -> Provider:
        return self.providers[upstream.provider]


def _int(section: dict, key: str, where: str, *, minimum: int = 1, default: int | None = None) -> int:
    value = section.get(key, default)
    if value is None:
        raise ConfigError(f"{where}.{key} is required.")
    if isinstance(value, bool) or not isinstance(value, int):
        raise ConfigError(f"{where}.{key} must be an integer.")
    if value < minimum:
        raise ConfigError(f"{where}.{key} must be at least {minimum}.")
    return value


def _number(section: dict, key: str, where: str, *, default: float | None = None) -> float:
    value = section.get(key, default)
    if value is None:
        raise ConfigError(f"{where}.{key} is required.")
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ConfigError(f"{where}.{key} must be a number.")
    if value < 0:
        raise ConfigError(f"{where}.{key} must not be negative.")
    return float(value)


def _positive(section: dict, key: str, default: float) -> float:
    value = _number(section, key, "config", default=default)
    if value <= 0:
        raise ConfigError(f"config.{key} must be greater than zero.")
    return value


def _effort(section: dict, key: str, where: str, default: str) -> str:
    value = section.get(key, default)
    if value not in EFFORTS:
        raise ConfigError(f"{where}.{key} must be one of {', '.join(EFFORTS)}.")
    return value


def _base_url(name: str, url: str, allow_loopback: bool) -> str:
    parts = urlsplit(url.strip())
    if parts.scheme not in ("https", "http") or not parts.hostname or parts.username \
            or parts.password or parts.query or parts.fragment:
        raise ConfigError(f"providers.{name}.base_url must be an https URL without credentials, "
                          "query or fragment.")
    if parts.scheme == "http" and not (allow_loopback and parts.hostname in LOOPBACK_HOSTS):
        raise ConfigError(f"providers.{name}.base_url must use https (plain http is allowed to "
                          "loopback only, and only with allow_insecure_loopback).")
    return url.strip().rstrip("/")


def _providers(section, allow_loopback: bool) -> dict[str, Provider]:
    if not isinstance(section, dict) or not section:
        raise ConfigError("providers must be a non-empty object.")
    out: dict[str, Provider] = {}
    for name, raw in section.items():
        where = f"providers.{name}"
        if not isinstance(raw, dict):
            raise ConfigError(f"{where} must be an object.")
        key_env = raw.get("key_env")
        if not isinstance(key_env, str) or not key_env.strip():
            raise ConfigError(f"{where}.key_env must name an environment variable.")
        prices_raw = raw.get("price_per_mtok", {})
        if not isinstance(prices_raw, dict):
            raise ConfigError(f"{where}.price_per_mtok must be an object of model -> [in, out].")
        prices: dict[str, tuple[float, float]] = {}
        for model, pair in prices_raw.items():
            if (not isinstance(pair, list) or len(pair) != 2
                    or any(isinstance(p, bool) or not isinstance(p, (int, float)) or p < 0
                           for p in pair)):
                raise ConfigError(f"{where}.price_per_mtok[{model!r}] must be "
                                  "[input_usd_per_mtok, output_usd_per_mtok].")
            prices[model] = (float(pair[0]), float(pair[1]))
        style = raw.get("effort_style", "reasoning_effort")
        if style not in EFFORT_STYLES:
            raise ConfigError(f"{where}.effort_style must be one of {', '.join(EFFORT_STYLES)}.")
        out[name] = Provider(name=name, key_env=key_env.strip(), price_per_mtok=prices,
                             base_url=_base_url(name, str(raw.get("base_url", "")), allow_loopback),
                             effort_style=style)
    return out


def _roles(section, providers: dict[str, Provider]) -> dict[str, Role]:
    if not isinstance(section, dict) or not section:
        raise ConfigError("roles must be a non-empty object.")
    out: dict[str, Role] = {}
    for name, raw in section.items():
        where = f"roles.{name}"
        if not name or len(name) > ROLE_NAME_LIMIT or not name.replace("-", "").replace("_", "").isalnum():
            raise ConfigError(f"{where}: a role name is letters, digits, '-' and '_'.")
        if not isinstance(raw, dict):
            raise ConfigError(f"{where} must be an object.")
        upstreams_raw = raw.get("upstreams")
        if not isinstance(upstreams_raw, list) or not upstreams_raw:
            raise ConfigError(f"{where}.upstreams must be a non-empty list.")
        upstreams = []
        for index, entry in enumerate(upstreams_raw):
            here = f"{where}.upstreams[{index}]"
            if not isinstance(entry, dict):
                raise ConfigError(f"{here} must be an object.")
            provider = entry.get("provider")
            model = entry.get("model")
            if not isinstance(provider, str) or provider not in providers:
                raise ConfigError(f"{here}.provider must name a configured provider.")
            if not isinstance(model, str) or not model.strip():
                raise ConfigError(f"{here}.model is required.")
            if model not in providers[provider].price_per_mtok:
                raise ConfigError(f"{here}: no price for {model!r} under providers.{provider}."
                                  "price_per_mtok; every served model must be priced.")
            extra = entry.get("extra", {})
            if not isinstance(extra, dict):
                raise ConfigError(f"{here}.extra must be an object.")
            for reserved in ("model", "messages", "stream", "tools", "max_tokens", "reasoning",
                             "reasoning_effort"):
                if reserved in extra:
                    raise ConfigError(f"{here}.extra may not set {reserved!r}"
                                      + (" (use effort / max_effort on the role)."
                                         if reserved.startswith("reasoning") else "."))
            upstreams.append(Upstream(provider=provider, model=model, extra=dict(extra)))
        effort = _effort(raw, "effort", where, "medium")
        max_effort = _effort(raw, "max_effort", where, "medium")
        if effort_rank(effort) > effort_rank(max_effort):
            raise ConfigError(f"{where}.effort ({effort}) is above {where}.max_effort ({max_effort}).")
        out[name] = Role(name=name, upstreams=tuple(upstreams),
                         max_output_tokens=_int(raw, "max_output_tokens", where),
                         max_input_chars=min(_int(raw, "max_input_chars", where), MAX_REQUEST_BYTES),
                         effort=effort, max_effort=max_effort)
    return out


def parse(data: dict, environ: dict | None = None) -> Config:
    """Validate a decoded JSON object. ``environ`` is checked for every key a role can use."""
    environ = os.environ if environ is None else environ
    if not isinstance(data, dict):
        raise ConfigError("the config must be a JSON object.")
    allow_loopback = bool(data.get("allow_insecure_loopback", False)) \
        or environ.get("GATEWAY_ALLOW_HTTP_LOOPBACK") == "1"
    providers = _providers(data.get("providers"), allow_loopback)
    roles = _roles(data.get("roles"), providers)
    for role in roles.values():
        for upstream in role.upstreams:
            provider = providers[upstream.provider]
            if not environ.get(provider.key_env, "").strip():
                raise ConfigError(f"providers.{provider.name}: environment variable "
                                  f"{provider.key_env} is not set (roles.{role.name} uses it).")

    quota_raw = data.get("quota", {})
    if not isinstance(quota_raw, dict):
        raise ConfigError("quota must be an object.")
    quota = Quota(tokens_per_day=_int(quota_raw, "tokens_per_day", "quota"),
                  requests_per_minute=_int(quota_raw, "requests_per_minute", "quota"),
                  concurrency_per_install=_int(quota_raw, "concurrency_per_install", "quota"))

    limits_raw = data.get("limits", {})
    if not isinstance(limits_raw, dict):
        raise ConfigError("limits must be an object.")
    per_provider_raw = limits_raw.get("per_provider_per_day_usd", {})
    if not isinstance(per_provider_raw, dict):
        raise ConfigError("limits.per_provider_per_day_usd must be an object.")
    per_provider = {}
    for name in per_provider_raw:
        if name not in providers:
            raise ConfigError(f"limits.per_provider_per_day_usd names unknown provider {name!r}.")
        per_provider[name] = _number(per_provider_raw, name, "limits.per_provider_per_day_usd")
    limits = Limits(global_concurrency=_int(limits_raw, "global_concurrency", "limits"),
                    spend_per_day_usd=_number(limits_raw, "spend_per_day_usd", "limits"),
                    spend_per_month_usd=_number(limits_raw, "spend_per_month_usd", "limits"),
                    per_provider_per_day_usd=per_provider,
                    registrations_per_ip_per_hour=_int(limits_raw, "registrations_per_ip_per_hour",
                                                       "limits"),
                    challenges_per_ip_per_hour=_int(limits_raw, "challenges_per_ip_per_hour",
                                                    "limits"))

    header = data.get("client_ip_header")
    if header is not None and (not isinstance(header, str) or not header.strip()):
        raise ConfigError("client_ip_header must be a header name or absent.")
    return Config(roles=roles, providers=providers, quota=quota, limits=limits,
                  token_ttl_seconds=_int(data, "token_ttl_seconds", "config", minimum=60),
                  upstream_connect_timeout=_positive(data, "upstream_connect_timeout", 20.0),
                  upstream_stall_timeout=_positive(data, "upstream_stall_timeout", 120.0),
                  client_ip_header=header.strip().lower() if header else None,
                  allow_insecure_loopback=allow_loopback)


def load(path: str | Path, environ: dict | None = None) -> Config:
    try:
        text = Path(path).read_text()
    except OSError as exc:
        raise ConfigError(f"cannot read {path}: {exc}") from None
    try:
        data = json.loads(text)
    except ValueError as exc:
        raise ConfigError(f"{path} is not valid JSON: {exc}") from None
    return parse(data, environ)

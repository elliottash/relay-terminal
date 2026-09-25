# SPDX-License-Identifier: AGPL-3.0-or-later
"""What a provider's refusal actually says, in Relay's words (#QK2Q).

A 401 or a 429 on its own does not tell the user what to do: a 429 from Z.AI's Coding Plan may be
"too many requests, try again in a second" (code 1302) or "your 5-hour allowance is spent until
17:53" (code 1308), and a 401 from Kimi Code may be a wrong key or a fifteen-minute login token
that simply ran out. The providers say which in the response body and headers, so this module
reads them and returns a :class:`Refusal` — a kind, the vendor's code, a reset instant when one
can be pinned down — plus one sentence for the user and a hint for fixing it.

The body is matched, never shown and never logged: providers quote the request they were sent,
key material and prompts with it. Only the vendor's code and error type survive, and only when
they look like identifiers.
"""
from __future__ import annotations

import base64
import email.utils
import json
import re
import time
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone

BODY_LIMIT = 8192

# Kinds a refusal is sorted into. The quota kinds, `balance` and `plan_expired` are *not*
# transient: asking again in a second cannot change the answer, so they are never retried.
QUOTA_KINDS = frozenset({"quota_5h", "quota_daily", "quota_weekly", "quota_monthly",
                         "quota_period", "quota"})
FINAL_KINDS = QUOTA_KINDS | {"balance", "plan_expired"}

LABELS = {
    "quota_5h": "5-hour usage limit reached",
    "quota_daily": "daily usage limit reached",
    "quota_weekly": "weekly usage limit reached",
    "quota_monthly": "monthly usage limit reached",
    "quota_period": "weekly/monthly usage limit reached",
    "quota": "usage limit reached",
    "rate_limit": "rate limited (too many requests at once)",
    "overloaded": "provider overloaded",
    "balance": "out of credit or balance",
    "plan_expired": "subscription plan expired",
    "account": "account disabled or locked",
    "auth": "API key rejected",
    "token_expired": "login token expired",
}

# Z.AI's error-code table (docs.z.ai, "Error codes"). Codes it does not list here fall through to
# the message and type matching below, so a renumbering degrades to a less precise sentence.
ZAI_CODES = {
    "1000": "auth", "1001": "auth", "1002": "auth", "1003": "token_expired", "1004": "auth",
    "1110": "account", "1111": "account", "1112": "account",
    "1113": "balance", "1120": "balance",
    "1302": "rate_limit", "1303": "rate_limit", "1305": "overloaded",
    "1304": "quota_daily",
    "1308": "quota",          # "Usage limit reached for 5 hour" — the window is in the message
    "1309": "plan_expired",
    "1310": "quota_period",   # "Weekly/Monthly Limit Exhausted"
}

# OpenAI-shaped `error.type` / `error.code` words, as Moonshot, OpenAI, Anthropic and OpenRouter
# send them.
TYPE_WORDS = (
    ("exceeded_current_quota", "balance"), ("insufficient_quota", "balance"),
    ("insufficient_balance", "balance"), ("billing", "balance"),
    ("rate_limit", "rate_limit"), ("overloaded", "overloaded"),
    ("invalid_authentication", "auth"), ("authentication", "auth"), ("invalid_api_key", "auth"),
    ("permission", "auth"),
)

# Phrases matched in the message (lowercased). Order matters: a window beats a generic "limit".
MESSAGE_WORDS = (
    (re.compile(r"\b5[\s-]?h(ou)?r?s?\b|five[\s-]hour"), "quota_5h"),
    (re.compile(r"weekly/monthly"), "quota_period"),
    (re.compile(r"\bweek(ly)?\b|\b7[\s-]?d(ay)?s?\b"), "quota_weekly"),
    (re.compile(r"\bmonth(ly)?\b"), "quota_monthly"),
    (re.compile(r"\bdaily\b|per day"), "quota_daily"),
    (re.compile(r"insufficient (account )?balance|arrears|top up|余额不足"), "balance"),
    (re.compile(r"(plan|package|subscription).{0,24}expired|expired.{0,24}(plan|package|subscription)"),
     "plan_expired"),
    (re.compile(r"token.{0,16}expired|expired.{0,16}token"), "token_expired"),
    (re.compile(r"too many requests|rate limit|frequency|concurren"), "rate_limit"),
    (re.compile(r"usage limit|limit (reached|exhausted)|quota"), "quota"),
)

_RESET = re.compile(r"reset(?:s)? (?:at|on) (\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2})")
_IDENT = re.compile(r"^[A-Za-z0-9_.:-]{1,48}$")


@dataclass
class Refusal:
    """What was recognised in one HTTP refusal. Nothing in it is the provider's own prose."""
    status: int
    kind: str = ""                       # a LABELS key, or "" when nothing was recognised
    vendor_code: str = ""                # the body's error.code, when it is identifier-shaped
    vendor_type: str = ""                # the body's error.type, likewise
    resets_at: int | None = None         # a Unix instant, only when the provider's zone is known
    provider_reset: str = ""             # "2026-09-26 05:53:25" in provider time, zone unknown
    source: str = ""                     # what decided the kind: code / type / message / poll / header
    token_expired_at: int | None = None  # a JWT key's own expiry, when the key is one

    @property
    def label(self) -> str:
        return LABELS.get(self.kind, "")

    @property
    def final(self) -> bool:
        """Whether asking again soon is pointless. A bare "quota" matched only in the message,
        with no window and no reset, stays retryable: Gemini says "Resource has been exhausted
        (e.g. check quota)" for a per-minute limit that clears in seconds."""
        if self.kind == "quota" and self.source == "message" and not (self.resets_at or self.provider_reset):
            return False
        return self.kind in FINAL_KINDS

    def log_fields(self) -> dict:
        """What may go in a log line: codes, the kind, the reset. Never the body or the key."""
        return {"kind": self.kind or "unrecognised", "vendor_code": self.vendor_code,
                "vendor_type": self.vendor_type, "resets_at": self.resets_at or "",
                "provider_reset": self.provider_reset, "source": self.source}


@dataclass
class Issue:
    """The structured form a failure travels in (protocol: `issue` on provider_retry / error)."""
    kind: str
    label: str
    preset: str = ""
    model: str = ""
    host: str = ""
    status: int = 0
    resets_at: int | None = None
    hint: str = ""
    extra: dict = field(default_factory=dict)

    def as_event(self) -> dict:
        out = {"kind": self.kind, "label": self.label, "preset": self.preset, "model": self.model,
               "host": self.host, "status": self.status, "hint": self.hint}
        if self.resets_at:
            out["resets_at"] = int(self.resets_at)
        out.update(self.extra)
        return out


# ----- reading one refusal ----------------------------------------------------------------------

def read_body(exc) -> bytes:
    """The refusal's body, read once and remembered on the exception (several readers ask)."""
    cached = getattr(exc, "_relay_body", None)
    if cached is not None:
        return cached
    try:
        body = exc.read(BODY_LIMIT) or b""
    except (OSError, ValueError, AttributeError):
        body = b""
    try:
        exc._relay_body = body
    except AttributeError:
        pass
    return body


def _error_object(body: bytes) -> dict:
    try:
        payload = json.loads(body)
    except (ValueError, UnicodeDecodeError):
        return {}
    if not isinstance(payload, dict):
        return {}
    error = payload.get("error")
    if isinstance(error, dict):
        return error
    if isinstance(error, str):
        return {"message": error}
    # Some gateways answer {"code": 1308, "msg": "..."} at the top level.
    return {k: payload[k] for k in ("code", "msg", "message", "type") if k in payload}


def _ident(value) -> str:
    text = str(value) if isinstance(value, (str, int)) and not isinstance(value, bool) else ""
    return text if _IDENT.match(text) else ""


def jwt_expiry(key: str) -> tuple[int, str] | None:
    """(exp, iss) of a key that is a JWT, e.g. Kimi Code's 15-minute OAuth access token.

    Not verified — it only answers "is this a login token, and when does it stop working", which
    is the difference between "your key is wrong" and "your login ran out".
    """
    parts = (key or "").split(".")
    if len(parts) != 3 or len(key) < 64:
        return None
    try:
        claims = json.loads(base64.urlsafe_b64decode(parts[1] + "=" * (-len(parts[1]) % 4)))
    except (ValueError, UnicodeDecodeError):
        return None
    if not isinstance(claims, dict) or type(claims.get("exp")) not in (int, float):
        return None
    return int(claims["exp"]), str(claims.get("iss") or "")


def _header(exc, name: str) -> str:
    headers = getattr(exc, "headers", None)
    try:
        value = headers.get(name) if headers is not None else None
    except AttributeError:
        return ""
    return value.strip() if isinstance(value, str) else ""


def provider_utc_offset(exc, now: float | None = None) -> timedelta | None:
    """The provider's clock offset from UTC, read off the response headers, or None.

    Z.AI stamps every response with ``X-LOG-ID: YYYYMMDDHHMMSS…`` in its own local time and a
    ``Date`` header in GMT; the difference, rounded to a quarter hour, is the zone its "reset at"
    times are written in (UTC+8 when measured, 2026-09-25). Anything that does not round cleanly
    to a real zone is refused rather than guessed.
    """
    log_id = _header(exc, "X-LOG-ID")
    if len(log_id) < 14 or not log_id[:14].isdigit():
        return None
    try:
        local = datetime.strptime(log_id[:14], "%Y%m%d%H%M%S").replace(tzinfo=timezone.utc)
    except ValueError:
        return None
    date = _header(exc, "Date")
    parsed = email.utils.parsedate_tz(date) if date else None
    utc = email.utils.mktime_tz(parsed) if parsed else (now if now is not None else time.time())
    return _quarter_hour(local.timestamp() - utc)


def _quarter_hour(seconds: float) -> timedelta | None:
    quarters = round(seconds / 900.0)
    if abs(seconds - quarters * 900) > 120 or not -48 <= quarters <= 56:   # UTC-12 … UTC+14
        return None
    return timedelta(minutes=15 * quarters)


def _reset_instant(naive_text: str, exc, poll_windows, now: float) -> int | None:
    """A provider-local "reset at" as a Unix instant, when its zone can be established."""
    try:
        naive = datetime.strptime(naive_text.replace("T", " "), "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return None
    as_utc = naive.replace(tzinfo=timezone.utc).timestamp()
    offset = provider_utc_offset(exc, now)
    if offset is not None:
        return int(as_utc - offset.total_seconds())
    # The quota poller's report carries the same resets as Unix milliseconds: a window whose
    # instant differs from the naive text by a whole zone is the same reset.
    for window in poll_windows or ():
        reset = window.get("resets_at") if isinstance(window, dict) else None
        if type(reset) in (int, float) and _quarter_hour(as_utc - reset) is not None:
            return int(reset)
    return None


def _poll_exhausted(poll_windows, now: float) -> tuple[str, int] | None:
    """(kind, resets_at) for the tightest spent window in a fresh quota poll, or None."""
    spent = [w for w in poll_windows or () if isinstance(w, dict)
             and type(w.get("used_percent")) in (int, float) and w["used_percent"] >= 100
             and type(w.get("resets_at")) in (int, float) and w["resets_at"] > now]
    if not spent:
        return None
    window = max(spent, key=lambda w: w["resets_at"])
    kind = {"5h": "quota_5h", "weekly": "quota_weekly", "monthly": "quota_monthly",
            "daily": "quota_daily"}.get(window.get("kind"), "quota")
    return kind, int(window["resets_at"])


def classify(exc, *, host: str = "", api_key: str = "", poll: dict | None = None,
             now: float | None = None) -> Refusal:
    """Sort one ``urllib.error.HTTPError`` into a :class:`Refusal`.

    ``poll`` is the provider's last quota report (`provider_limits.last`), when Relay polls it;
    it names the window and its reset when the body does not, and pins a provider-local reset
    time to an instant.
    """
    now = time.time() if now is None else now
    status = int(getattr(exc, "code", 0) or 0)
    refusal = Refusal(status=status)
    error = _error_object(read_body(exc))
    refusal.vendor_code = _ident(error.get("code"))
    refusal.vendor_type = _ident(error.get("type"))
    message = str(error.get("message") or error.get("msg") or "").lower()[:1000]
    poll_windows = (poll or {}).get("windows") if isinstance(poll, dict) else None
    fresh_poll = isinstance(poll, dict) and 0 <= now - (poll.get("updated_at") or 0) <= 1800

    kind, source = "", ""
    if host == "api.z.ai" and refusal.vendor_code in ZAI_CODES:
        kind, source = ZAI_CODES[refusal.vendor_code], "code"
    words = f"{refusal.vendor_type} {refusal.vendor_code}".lower()
    if not kind:
        for word, found in TYPE_WORDS:
            if word in words:
                kind, source = found, "type"
                break
    # The message refines a generic quota ("Usage limit reached for 5 hour") and names a kind
    # when neither code nor type did.
    for pattern, found in MESSAGE_WORDS:
        if pattern.search(message):
            if not kind or (kind in ("quota", "quota_period") and found in QUOTA_KINDS
                            and found not in ("quota", "quota_period")):
                kind, source = found, source or "message"
            break
    if status == 402 and not kind:
        kind, source = "balance", "status"
    # Kimi Code types its spent-window refusal `access_terminated_error` over HTTP 403
    # (2026-09-25): the message names the 5-hour limit, a quota kind was recognised above, and the
    # status does not outvote that. Only an unexplained 401/403 is an auth failure.
    if status in (401, 403) and kind not in ("token_expired", "account", "balance", "plan_expired") \
            and kind not in QUOTA_KINDS:
        kind, source = "auth", source or "status"
    if status in (401, 403) and api_key:
        expiry = jwt_expiry(api_key)
        if expiry is not None:
            refusal.token_expired_at = expiry[0]
            if expiry[0] <= now + 5:
                kind, source = "token_expired", "token"

    reset = _RESET.search(message)
    if reset:
        text = reset.group(1).replace("T", " ")
        try:
            datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
        except ValueError:
            text = ""                       # never repeat a malformed date back to the user
        if text:
            refusal.provider_reset = text
            refusal.resets_at = _reset_instant(text, exc, poll_windows, now)
    # A 429 the body did not explain, on a plan whose own quota report says a window is spent:
    # that report is the explanation. (Kimi Code's 429 body has not been captured yet.)
    if status == 429 and fresh_poll and kind in ("", "quota", "rate_limit"):
        spent = _poll_exhausted(poll_windows, now)
        if spent is not None:
            kind, source = spent[0], "poll"
            refusal.resets_at = refusal.resets_at or spent[1]
    elif fresh_poll and kind in QUOTA_KINDS and refusal.resets_at is None:
        spent = _poll_exhausted(poll_windows, now)
        if spent is not None:
            refusal.resets_at = spent[1]
    # A Retry-After longer than the transport would wait is the provider saying "not today".
    if status == 429 and kind in ("", "quota") and not refusal.resets_at:
        after = _header(exc, "Retry-After")
        if after.isdigit() and int(after) > 600:
            kind, source = kind or "quota", source or "header"
            refusal.resets_at = int(now + int(after))
    refusal.kind, refusal.source = kind, source
    return refusal


# ----- saying it --------------------------------------------------------------------------------

def when_text(instant: float, now: float | None = None) -> str:
    """A reset as the user reads it: "17:53 (in 1.1 h)", "Sat 05:53 (in 2.4 d)"."""
    now = time.time() if now is None else now
    local, today = time.localtime(instant), time.localtime(now)
    clock = time.strftime("%H:%M", local)
    if (local.tm_year, local.tm_yday) != (today.tm_year, today.tm_yday):
        clock = time.strftime("%a ", local) + clock
    remaining = max(0.0, instant - now)
    span = f"{remaining / 3600:.1f} h" if remaining < 48 * 3600 else f"{remaining / 86400:.1f} d"
    return f"{clock} (in {span})"


def spent_text(limits, now: float | None = None) -> str:
    """ "5-hour usage limit reached; resets 17:53 (in 1.1 h)" for a quota report with a spent
    window, "" for one without (or a report that is not one)."""
    now = time.time() if now is None else now
    if not isinstance(limits, dict):
        return ""
    if limits.get("status") == "rejected":
        return LABELS["quota"]
    spent = _poll_exhausted(limits.get("windows"), now)
    if spent is None:
        return ""
    return f"{LABELS[spent[0]]}; resets {when_text(spent[1], now)}"


def reset_phrase(refusal: Refusal, now: float | None = None) -> str:
    if refusal.resets_at:
        return f"resets {when_text(refusal.resets_at, now)}"
    if refusal.provider_reset:
        return f"resets {refusal.provider_reset} (provider time)"
    return ""


def hint(kind: str, *, preset_label: str = "", token_expired_at: int | None = None,
         now: float | None = None) -> str:
    """What the user can do about it, in one sentence."""
    where = preset_label or "this provider"
    if kind in QUOTA_KINDS:
        return (f"Relay skips {where} until the reset; pick another model now, or wait.")
    if kind == "rate_limit":
        return "Relay asks again with backoff; if it keeps happening, run fewer panes on this key."
    if kind == "overloaded":
        return "The provider is busy; Relay asks again, and fails over if it stays busy."
    if kind == "balance":
        return f"Top up the {where} account, or rank another provider first in Options › Models."
    if kind == "plan_expired":
        return f"Renew the {where} subscription, or rank another provider first in Options › Models."
    if kind == "account":
        return f"Check the {where} account's status on the provider's site."
    if kind == "token_expired":
        at = f" at {time.strftime('%H:%M', time.localtime(token_expired_at))}" if token_expired_at else ""
        return (f"The stored key is a login token that expired{at}. Sign in again with the provider's "
                "CLI (or keep its refresh running), or store a long-lived API key in Options › Models "
                "› Providers.")
    if kind == "auth":
        return ("Check the key in Options › Models › Providers: it may be missing, mistyped, for a "
                "different endpoint of the same provider, or not allowed this model.")
    return ""


def sentence(refusal: Refusal, where: str, *, preset_label: str = "",
             now: float | None = None) -> str:
    """The one line the user reads: what failed, and when it clears. "" when unrecognised."""
    if not refusal.kind:
        return ""
    label = LABELS[refusal.kind]
    reset = reset_phrase(refusal, now) if refusal.kind in FINAL_KINDS or refusal.resets_at else ""
    text = f"Provider HTTP {refusal.status} for {where}: {label}"
    if refusal.kind == "token_expired" and refusal.token_expired_at:
        text += f" at {time.strftime('%H:%M', time.localtime(refusal.token_expired_at))}"
    if reset:
        text += f"; {reset}"
    tip = hint(refusal.kind, preset_label=preset_label, token_expired_at=refusal.token_expired_at,
               now=now)
    return f"{text}. {tip}".strip()

# SPDX-License-Identifier: AGPL-3.0-or-later
"""Relay Free: the installation identity and the bearer token for Relay's hosted service.

Relay Free is the ``relay-free`` preset: an included, quota-limited allowance so a fresh install's
first ask works with no key stored (owner decision, 2026-09-18). The gateway (``gateway/``) speaks
the OpenAI shape the transport already talks, so the only thing this module adds is "get a token
instead of a key":

* **An installation identity.** An X25519 key of its own, made on first use and stored the way the
  remote identity is (``remote/identity.py``: the Secret Service keyring when there is one, else a
  0600 file). It is a *separate* key from the remote identity: phones pin that one, and
  regenerating either must never break the other.
* **A short-lived bearer token.** ``POST /v1/challenge`` → an HMAC over the challenge keyed by the
  X25519 shared secret with the gateway's ephemeral key → ``POST /v1/register``. That is the
  client half of ``remote/host.py::Hub.register``, made synchronous. The token lives in memory
  only and is refreshed when it is missing or within ``REFRESH_MARGIN`` of expiry.
* **The last quota seen**, from the register reply or a response's ``X-Relay-Quota-*`` headers,
  for the keys modal and the pane's chip.

Nothing here is needed for BYOK. ``cryptography`` is imported lazily, so a build without it still
runs every other provider and only Relay Free reports ``HostedUnavailable``.
"""
from __future__ import annotations

import base64
import hmac
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from hashlib import sha256
from pathlib import Path

from . import logs
from .presets import PRESETS

PRESET_ID = "relay-free"
ENV_URL = "RELAY_HOSTED_URL"          # tests point it at an in-process gateway over loopback HTTP
ATTRIBUTE = "relay-free-identity"     # the keyring entry; distinct from "remote-identity"
FILENAME = "identity.key"
LABEL = "Relay Free installation key"
# A token is refreshed this long before it expires, so a turn that starts just before expiry does
# not spend its 401 retry on something the clock could have told us.
REFRESH_MARGIN = 300.0
TIMEOUT = 20.0
MAX_BODY = 64 * 1024
QUOTA_HEADERS = {"limit": "X-Relay-Quota-Limit", "used": "X-Relay-Quota-Used",
                 "resets_at": "X-Relay-Quota-Resets-At"}
# The gateway's error codes and how each reads in a pane. The server's own `message` is Relay's
# text, not a model provider's, but it still only ever stands in for an unknown code: the wording
# the user sees is decided here, where it can name the way out (a key of their own).
ERROR_CODES = ("quota_exhausted", "rate_limited", "free_unavailable", "token_expired", "bad_request")
MAX_MESSAGE = 200

_log = logs.get("hosted")


class HostedUnavailable(RuntimeError):
    """Relay Free cannot be used right now: no ``cryptography``, no gateway, or a refusal.

    ``code`` is one of ``ERROR_CODES`` when the gateway said why, else ``""``; ``resets_at`` is the
    unix time a quota refusal lifts, when known.
    """

    def __init__(self, text: str, code: str = "", resets_at: int | None = None):
        super().__init__(text)
        self.code = code
        self.resets_at = resets_at


def base_url() -> str:
    """The gateway's base URL: the preset's, unless ``RELAY_HOSTED_URL`` points elsewhere."""
    override = os.environ.get(ENV_URL, "").strip()
    return override.rstrip("/") if override else PRESETS[PRESET_ID].base_url


def endpoint_for(configured: str) -> str:
    """Where a hosted config's calls go: the override, when the config names the preset's address.

    A config pointed at the canonical gateway follows ``RELAY_HOSTED_URL`` so a test or a local
    gateway catches every call (the pane's, each tier's, the key test's) without each caller
    knowing about the variable; a config that already names somewhere else is left alone.
    """
    canonical = PRESETS[PRESET_ID].base_url
    return base_url() if configured.rstrip("/") == canonical else configured


def state_dir() -> Path:
    """Where the installation key falls back to on a machine without a keyring."""
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    path = Path(base) / "relay" / "hosted"
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


# ----- the remote package, imported late -------------------------------------------------------
# ``remote/`` sits beside ``backend/`` in the source tree and in the installed share directory, and
# it needs ``cryptography``. Both facts are handled here rather than at import time, so that a
# worker with neither still serves every BYOK provider.
#
# The GUI starts the worker with ``python3 -S`` (src/Pane.h startWorker), which leaves the system
# site-packages off sys.path, and that is where the distribution's python3-cryptography lives. The
# remote sidecar runs without -S and never meets this; the worker meets it on the first Relay Free
# call, so the system site directories (never the user's) are added then, and only then.

MISSING = "Relay Free needs python3-cryptography (the cryptography module)."


def _system_site_dirs() -> list[str]:
    """The distribution's site-packages, as ``site`` would add them; nothing from ``~/.local``."""
    try:
        import site
        return [path for path in site.getsitepackages() if os.path.isdir(path) and path not in sys.path]
    except (ImportError, AttributeError, OSError):
        return []


def _import_remote():
    """The ``remote`` modules this needs, or ``HostedUnavailable`` saying what is missing."""
    root = str(Path(__file__).resolve().parents[2])
    tried_site = False
    for _ in range(3):        # at most one retry per remedy below
        try:
            from remote import identity, noise
            return identity, noise
        except ModuleNotFoundError as exc:
            if exc.name == "remote" and root not in sys.path:
                sys.path.append(root)
                continue
            if exc.name != "remote" and sys.flags.no_site and not tried_site:
                tried_site = True
                extra = _system_site_dirs()
                if extra:
                    sys.path.extend(extra)
                    continue
            raise HostedUnavailable(MISSING) from None
        except ImportError:
            raise HostedUnavailable(MISSING) from None
    raise HostedUnavailable(MISSING)


def available() -> bool:
    """Whether Relay Free can run here at all: ``cryptography`` imports. Never touches the network."""
    try:
        _import_remote()
    except HostedUnavailable:
        return False
    return True


_identity_class = None


def identity_class():
    """``remote.identity.Identity`` with Relay Free's keyring attribute, file and directory."""
    global _identity_class
    if _identity_class is None:
        identity, _ = _import_remote()

        class Identity(identity.Identity):
            attribute = ATTRIBUTE
            filename = FILENAME
            label = LABEL

            @staticmethod
            def default_dir() -> Path:
                return state_dir()

            @property
            def installation_id(self) -> str:
                """Derived from the key, as the gateway derives it, so the reply can be checked."""
                return sha256(self.public).hexdigest()[:32]

        _identity_class = Identity
    return _identity_class


def load_identity(directory: Path | None = None):
    """The installation key, made on first use. Unlike the remote identity, creating one is the
    normal path: nothing has pinned it yet, so a missing key is not a broken pairing."""
    return identity_class().load_or_create(directory)


# ----- errors ----------------------------------------------------------------------------------

def _when(resets_at) -> str:
    if not isinstance(resets_at, (int, float)) or resets_at <= 0:
        return ""
    return time.strftime("%H:%M", time.localtime(resets_at))


def describe_error(status: int, body: bytes | None) -> tuple[str, str, int | None]:
    """(text, code, resets_at) for a gateway refusal.

    The body is the gateway's own JSON, so its ``code`` is trusted to pick the wording; its
    ``message`` is shown only for a code this module has no sentence for, truncated. A body that is
    not the gateway's shape (a proxy page, say) is dropped like any provider's.
    """
    code, message, resets_at = "", "", None
    if body:
        try:
            error = json.loads(body[:MAX_BODY].decode("utf-8", "replace")).get("error")
        except (ValueError, AttributeError):
            error = None
        if isinstance(error, dict):
            if isinstance(error.get("code"), str):
                code = error["code"]
            if isinstance(error.get("message"), str):
                message = error["message"]
            if isinstance(error.get("resets_at"), (int, float)) and not isinstance(error["resets_at"], bool):
                resets_at = int(error["resets_at"])
    if status == 401 and not code:
        code = "token_expired"
    at = _when(resets_at)
    if code == "quota_exhausted":
        text = ("Relay Free: today's included allowance is used up"
                + (f"; it resets at {at}" if at else "") + ". "
                "Use one of your own providers, or add a key under Options › Models › API keys….")
    elif code == "rate_limited":
        text = ("Relay Free: too many requests in a short time"
                + (f"; try again at {at}" if at else "; try again in a moment") + ".")
    elif code == "free_unavailable":
        text = ("Relay Free is not available right now. Use one of your own providers "
                "(Options › Models › API keys…), or try again later.")
    elif code == "token_expired":
        text = "Relay Free: the session token was refused; Relay will register again."
    elif message:
        text = f"Relay Free refused the request (HTTP {status}): {message[:MAX_MESSAGE]}"
    else:
        text = f"Relay Free: the hosted service answered HTTP {status}."
    return text, code if code in ERROR_CODES else "", resets_at


def upstream_retried(body: bytes | None) -> bool:
    """Whether the gateway already retried this request across its own upstreams (owner, 2026-09-19).

    The gateway fails a request over from one upstream to the next before the first byte, and marks
    a refusal it returns after more than one attempt with ``error.retried`` (``gateway/proxy.py``).
    The transport's own retry of a 429 or a 5xx would then re-run that whole chain, so one refused
    turn cost the retries twice over: the client stops instead and the gateway owns them.
    """
    if not body:
        return False
    try:
        error = json.loads(body[:MAX_BODY].decode("utf-8", "replace")).get("error")
    except (ValueError, AttributeError):
        return False
    retried = error.get("retried") if isinstance(error, dict) else None
    return isinstance(retried, int) and not isinstance(retried, bool) and retried > 0


def quota_from_headers(headers) -> dict | None:
    """``{limit, used, resets_at}`` from a response's ``X-Relay-Quota-*`` headers, or None."""
    if headers is None:
        return None
    out = {}
    for field, name in QUOTA_HEADERS.items():
        value = headers.get(name)
        if value is None:
            return None
        try:
            out[field] = int(str(value).strip())
        except ValueError:
            return None
    return out


def _quota_from_body(value) -> dict | None:
    if not isinstance(value, dict):
        return None
    out = {}
    for field in ("limit", "used", "resets_at"):
        number = value.get(field)
        if not isinstance(number, (int, float)) or isinstance(number, bool):
            return None
        out[field] = int(number)
    return out


# ----- the token cache -------------------------------------------------------------------------

class Session:
    """One installation's registration with the gateway: its identity, token and last quota.

    Thread-safe: every pane's provider and the worker's ``hosted_quota`` request share the one
    module-level session, and a refresh under the lock means two turns that both find the token
    stale register once, not twice. The token never leaves this object except as the
    ``Authorization`` header, and it is never logged.
    """

    def __init__(self, base: str | None = None, identity=None, opener=None, clock=time.time,
                 directory: Path | None = None):
        self._base = base
        self._identity = identity
        self._directory = directory
        self._opener = opener
        self._clock = clock
        self._lock = threading.RLock()
        self._token = ""
        self._expires = 0.0
        self._quota: dict | None = None
        self.plan = ""
        self.installation_id = ""

    # ----- what the caller sees ------------------------------------------------------------
    @property
    def base(self) -> str:
        return self._base or base_url()

    def identity(self):
        with self._lock:
            if self._identity is None:
                self._identity = load_identity(self._directory)
            return self._identity

    def token(self, force: bool = False) -> str:
        """A bearer token good for at least ``REFRESH_MARGIN`` seconds, registering when needed.

        ``force`` throws the cached token away first: the gateway answered 401 to it, so the clock
        was wrong about it (a restart rotated it, say) and the cache must not be trusted.
        """
        with self._lock:
            if force or not self._token or self._expires - self._clock() <= REFRESH_MARGIN:
                self._register()
            return self._token

    def quota(self) -> dict | None:
        """The last ``{limit, used, resets_at}`` seen, or None before any exchange."""
        with self._lock:
            return dict(self._quota) if self._quota else None

    def note_quota(self, headers) -> dict | None:
        """Record the quota a response's headers carry; returns it for the ``hosted_quota`` event."""
        quota = quota_from_headers(headers)
        if quota is not None:
            with self._lock:
                self._quota = quota
        return quota

    def fetch_quota(self) -> dict:
        """``GET /v1/quota`` (relative to the base, like ``/chat/completions``): the live allowance, for the keys modal and the chip on demand."""
        reply = self._get("/quota")
        quota = _quota_from_body(reply)
        if quota is None:
            raise HostedUnavailable("Relay Free: the hosted service sent a quota it could not read.")
        with self._lock:
            self._quota = quota
            if isinstance(reply.get("plan"), str):
                self.plan = reply["plan"]
        return quota

    def forget(self) -> None:
        """Drop the cached token (tests, and a gateway that says the token is gone)."""
        with self._lock:
            self._token, self._expires = "", 0.0

    # ----- the challenge / proof / register exchange ----------------------------------------
    def _register(self) -> None:
        """Hub.register, synchronous: prove the key, take a token. Called under the lock."""
        _, noise = _import_remote()
        identity = self.identity()
        challenge = self._post("/challenge", {})
        try:
            ephemeral = base64.b64decode(challenge["ephemeral_public"])
            value = challenge["challenge"]
            if not isinstance(value, str):
                raise TypeError
        except (KeyError, TypeError, ValueError):
            raise HostedUnavailable("Relay Free: the hosted service sent a challenge it could not read.") from None
        try:
            shared = noise.dh(identity.private, ephemeral)
        except Exception:
            raise HostedUnavailable("Relay Free: the hosted service's key could not be used.") from None
        proof = hmac.new(shared, value.encode(), sha256).hexdigest()
        from . import __version__
        reply = self._post("/register", {
            "static_pubkey": base64.b64encode(identity.public).decode(),
            "challenge": value, "proof": proof, "client": {"version": __version__}})
        token, expires_in = reply.get("token"), reply.get("expires_in")
        if not isinstance(token, str) or not token or not isinstance(expires_in, (int, float)) \
                or isinstance(expires_in, bool):
            raise HostedUnavailable("Relay Free: the hosted service sent a registration it could not read.")
        if reply.get("installation_id") != identity.installation_id:
            # The gateway derives the id from the public key, exactly as Hub.register checks the
            # rendezvous did; anything else is a gateway that is not ours.
            raise HostedUnavailable("Relay Free: the hosted service derived a different installation id.")
        self._token = token
        self._expires = self._clock() + float(expires_in)
        self.installation_id = identity.installation_id
        if isinstance(reply.get("plan"), str):
            self.plan = reply["plan"]
        quota = _quota_from_body(reply.get("quota"))
        if quota is not None:
            self._quota = quota
        logs.event(_log, "hosted_registered", installation=identity.installation_id[:12],
                   expires_in=int(expires_in), plan=self.plan)

    # ----- HTTP -------------------------------------------------------------------------
    def _opener_(self):
        if self._opener is None:
            from .provider import NoRedirect
            self._opener = urllib.request.build_opener(NoRedirect())
        return self._opener

    def _post(self, path: str, payload: dict) -> dict:
        request = urllib.request.Request(
            self.base + path, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json", "User-Agent": "Relay/0.1"}, method="POST")
        return self._exchange(request, path)

    def _get(self, path: str) -> dict:
        headers = {"User-Agent": "Relay/0.1", "Authorization": "Bearer " + self.token()}
        request = urllib.request.Request(self.base + path, headers=headers, method="GET")
        try:
            return self._exchange(request, path)
        except HostedUnavailable as exc:
            if exc.code != "token_expired":
                raise
        # One retry with a fresh token, as the chat transport does (provider.HostedChatProvider).
        headers["Authorization"] = "Bearer " + self.token(force=True)
        request = urllib.request.Request(self.base + path, headers=headers, method="GET")
        return self._exchange(request, path)

    def _exchange(self, request, path: str) -> dict:
        try:
            with self._opener_().open(request, timeout=TIMEOUT) as response:
                raw = response.read(MAX_BODY + 1)
        except urllib.error.HTTPError as error:
            body = error.read(MAX_BODY) if getattr(error, "fp", None) is not None else b""
            text, code, resets_at = describe_error(error.code, body)
            logs.event(_log, "hosted_refused", level_name="error", path=path, status=error.code, code=code)
            raise HostedUnavailable(text, code, resets_at) from None
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            # The provider module's redirect refusal arrives as its own error; keep its text.
            reason = getattr(error, "reason", error)
            text = str(reason) if type(reason).__name__ == "ProviderError" else type(reason).__name__
            logs.event(_log, "hosted_unreachable", level_name="error", path=path, reason=text)
            raise HostedUnavailable(f"Relay Free: could not reach the hosted service ({text}). "
                                    "Check connectivity, or use one of your own providers.") from None
        if len(raw) > MAX_BODY:
            raise HostedUnavailable("Relay Free: the hosted service's reply is too large.")
        try:
            reply = json.loads(raw)
        except ValueError:
            raise HostedUnavailable("Relay Free: the hosted service sent a reply it could not read.") from None
        if not isinstance(reply, dict):
            raise HostedUnavailable("Relay Free: the hosted service sent a reply it could not read.")
        return reply


_session: Session | None = None
_session_lock = threading.Lock()


def session() -> Session:
    """The worker's one session, made on first use."""
    global _session
    with _session_lock:
        if _session is None:
            _session = Session()
        return _session


def reset(new: Session | None = None) -> None:
    """Replace the shared session (tests point it at a fake gateway)."""
    global _session
    with _session_lock:
        _session = new


def status() -> dict:
    """What the ``presets`` event says about the Relay Free row: usable here, and the last quota."""
    ready = available()
    return {"available": ready, "quota": session().quota() if ready else None}

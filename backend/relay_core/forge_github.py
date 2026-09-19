# SPDX-License-Identifier: AGPL-3.0-or-later
"""The GitHub half of the Switchboard ↔ issues sync (card `#GDQN`).

REST v3 over `urllib`, like `relay_core.provider` and `relay_core.hosted`: no new dependency, one
opener, no redirects followed blindly.  Everything GitHub-shaped lives here — pagination through
`Link` headers, `ETag`/`If-None-Match` so an idle sync costs no rate limit, the primary and
secondary rate limits, pull requests filtered out of issue listings, a fork whose issues are
disabled following its parent, and GitHub Enterprise base URLs.  `relay_core.forge_sync` knows
none of it.

**The token.** Resolved at sync time, never at import: an explicit argument, then `GH_TOKEN` /
`GITHUB_TOKEN`, then `gh auth token`, then git's credential helper.  It is held in one attribute,
sent only as the `Authorization` header, and scrubbed out of every message this module raises or
logs; it is never written to the sync state (the state file has no field for it).
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Callable, Sequence

from . import logs
from .forge_sync import (Comment, ForgeAuthError, ForgeProvider, ForgeRateLimited, ForgeUnavailable,
                         Issue, RepoInfo)

_log = logs.get("forge")

DEFAULT_BASE = "https://api.github.com"
API_VERSION = "2022-11-28"
USER_AGENT = "Relay/0.1"
TIMEOUT = 30
PER_PAGE = 100
MAX_PAGES = 40
MAX_BODY = 8 << 20

#: How long the provider is willing to sit and wait for a secondary rate limit before it gives
#: up and hands the caller a "try again at HH:MM" instead of hanging.
MAX_WAIT = 60.0
MAX_ATTEMPTS = 4

#: Methods a transport failure may be retried on.  A write is not repeated: see `_send`.
IDEMPOTENT = frozenset({"GET", "HEAD"})

REPO_RE = re.compile(r"^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$")


def normalize_base(base: str | None) -> str:
    """`https://api.github.com`, or an Enterprise host's `/api/v3` root."""
    text = (base or DEFAULT_BASE).strip().rstrip("/")
    if not text:
        return DEFAULT_BASE
    parts = urllib.parse.urlsplit(text if "://" in text else "https://" + text)
    if parts.scheme not in ("https", "http"):
        raise ForgeUnavailable(f"{text}: a GitHub base URL must be http(s).")
    path = parts.path.rstrip("/")
    host = (parts.hostname or "").lower()
    named = bool(host) and not _IP_RE.match(host) and host not in ("localhost",)
    if not path and host != "api.github.com" and named:
        # GitHub Enterprise Server: `https://ghe.example.com` means its REST root.  A bare host
        # or an address (a test server, a tunnel) is taken exactly as given.
        path = "/api/v3"
    return urllib.parse.urlunsplit((parts.scheme, parts.netloc, path, "", ""))


_IP_RE = re.compile(r"^(\d{1,3}(\.\d{1,3}){3}|\[?[0-9a-f:]+\]?)$", re.I)


# ------------------------------------------------------------------ token resolution

def _run(argv: Sequence[str], stdin: str | None = None, cwd: str | None = None) -> str:
    # `git credential fill` is the last resort: when nothing is stored, git must fail fast
    # rather than put a username prompt on the user's terminal (same call as skill_manage's
    # _git_env).  A failed lookup is an empty string here, exactly like a timeout.
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_ASKPASS="/bin/false")
    try:
        done = subprocess.run(list(argv), input=stdin, capture_output=True, text=True,
                              timeout=15, cwd=cwd, env=env)
    except (OSError, subprocess.SubprocessError):
        return ""
    return done.stdout if done.returncode == 0 else ""


def resolve_token(explicit: str | None = None, *, env: dict | None = None, host: str = "github.com",
                  cwd: str | None = None, runner: Callable[..., str] = _run) -> tuple[str, str]:
    """(token, where it came from).  Called at sync time only; `runner` is injected by tests.

    Order: the explicit argument, `GH_TOKEN`, `GITHUB_TOKEN`, `gh auth token`, `git credential
    fill`.  An empty string means nothing was found — the caller reports that, never a token.
    """
    if explicit:
        return explicit.strip(), "argument"
    environ = os.environ if env is None else env
    for name in ("GH_TOKEN", "GITHUB_TOKEN"):
        value = (environ.get(name) or "").strip()
        if value:
            return value, name
    value = runner(["gh", "auth", "token", "--hostname", host]).strip()
    if value and "\n" not in value:
        return value, "gh auth token"
    filled = runner(["git", "credential", "fill"],
                    stdin=f"protocol=https\nhost={host}\n\n", cwd=cwd)
    for line in filled.splitlines():
        if line.startswith("password="):
            password = line[len("password="):].strip()
            if password:
                return password, "git credential"
    return "", ""


# ------------------------------------------------------------------------- responses

@dataclass
class _Reply:
    status: int
    headers: dict
    body: object = None
    etag: str = ""
    link: str = ""


def _parse_next(link: str) -> str:
    for part in (link or "").split(","):
        bits = part.split(";")
        if len(bits) < 2:
            continue
        url = bits[0].strip()
        if url.startswith("<") and url.endswith(">") and 'rel="next"' in part.replace("'", '"'):
            return url[1:-1]
    return ""


class GitHubProvider(ForgeProvider):
    """One repository, one credential.  Every method may raise a `forge_sync` error."""

    def __init__(self, repo: str, token: str | None = None, *, base_url: str | None = None,
                 opener=None, sleep: Callable[[float], None] = time.sleep,
                 clock: Callable[[], float] = time.time, env: dict | None = None,
                 timeout: float = TIMEOUT, max_wait: float = MAX_WAIT,
                 max_attempts: int = MAX_ATTEMPTS, runner: Callable[..., str] = _run,
                 cwd: str | None = None):
        if not REPO_RE.match(str(repo or "")):
            raise ForgeUnavailable(f"{repo!r} is not an owner/repo.")
        self.repo = str(repo)
        self.base = normalize_base(base_url)
        self._explicit = token
        self._token = ""
        self.token_source = ""
        self._opener = opener
        self._sleep = sleep
        self._clock = clock
        self._env = env
        self._timeout = timeout
        self._max_wait = float(max_wait)
        self._max_attempts = max(1, int(max_attempts))
        self._runner = runner
        self._cwd = cwd
        self._labels: set[str] | None = None
        #: The ETag of the last conditional listing, for the caller to store.
        self.list_etag = ""
        self.comments_etag = ""
        self.requests = 0

    def __repr__(self) -> str:                      # never prints the credential
        return f"<GitHubProvider {self.repo} at {self.base}>"

    # ---- credential -----------------------------------------------------------
    @property
    def host(self) -> str:
        return urllib.parse.urlsplit(self.base).hostname or "github.com"

    def token(self) -> str:
        if not self._token:
            token, source = resolve_token(self._explicit, env=self._env, host=self.host,
                                          cwd=self._cwd, runner=self._runner)
            if not token:
                raise ForgeAuthError(
                    "No GitHub credential. Set GH_TOKEN (or GITHUB_TOKEN), run `gh auth login`, "
                    "or store one with git's credential helper.")
            self._token, self.token_source = token, source
            logs.event(_log, "forge_token", host=self.host, source=source)
        return self._token

    def _safe(self, text) -> str:
        """Never let the credential out through a message, however it got in there."""
        out = str(text)
        if self._token:
            out = out.replace(self._token, "<token>")
        return logs.scrub(out)

    # ---- transport ------------------------------------------------------------
    def _opener_(self):
        if self._opener is None:
            from .provider import NoRedirect
            self._opener = urllib.request.build_opener(NoRedirect())
        return self._opener

    def _url(self, path: str, query: dict | None = None) -> str:
        url = path if path.startswith("http") else self.base + path
        if query:
            joiner = "&" if "?" in url else "?"
            url += joiner + urllib.parse.urlencode({k: v for k, v in query.items() if v is not None})
        return url

    def _send(self, method: str, url: str, payload: dict | None = None,
              etag: str | None = None) -> _Reply:
        headers = {"Accept": "application/vnd.github+json", "User-Agent": USER_AGENT,
                   "X-GitHub-Api-Version": API_VERSION,
                   "Authorization": "Bearer " + self.token()}
        if etag:
            headers["If-None-Match"] = etag
        data = None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        attempt = 0
        while True:
            attempt += 1
            self.requests += 1
            request = urllib.request.Request(url, data=data, headers=headers, method=method)
            try:
                with self._opener_().open(request, timeout=self._timeout) as response:
                    raw = response.read(MAX_BODY)
                    head = {k.lower(): v for k, v in response.headers.items()}
                    return _Reply(response.status, head, _json(raw),
                                  head.get("etag", ""), head.get("link", ""))
            except urllib.error.HTTPError as error:
                head = {k.lower(): v for k, v in (error.headers or {}).items()}
                body = b""
                if getattr(error, "fp", None) is not None:
                    try:
                        body = error.read(MAX_BODY)
                    except OSError:                     # pragma: no cover - socket gone
                        body = b""
                if error.code == 304:
                    return _Reply(304, head, None, etag or head.get("etag", ""), "")
                wait = self._rate_wait(error.code, head, body)
                if wait is not None:
                    if wait <= self._max_wait and attempt < self._max_attempts:
                        logs.event(_log, "forge_rate_backoff", host=self.host, seconds=int(wait))
                        self._sleep(wait)
                        continue
                    raise ForgeRateLimited(
                        self._rate_message(head, wait), self._clock() + wait) from None
                raise self._http_error(error.code, head, body) from None
            except (urllib.error.URLError, TimeoutError, OSError) as error:
                # Only a read is retried.  A `POST` or `PATCH` that dies on the socket may
                # already have been carried out — a timeout on "create issue" that GitHub
                # honoured comes back as a second issue, and on "create comment" as a double
                # post — and the sync has no idempotency key to offer, so a transport failure
                # on a write is reported rather than repeated.  A rate limit is different: the
                # request was *refused*, and `_rate_wait` above still backs off for any method.
                if attempt < self._max_attempts and method in IDEMPOTENT:
                    self._sleep(min(2.0 ** attempt, self._max_wait))
                    continue
                reason = getattr(error, "reason", error)
                raise ForgeUnavailable(
                    self._safe(f"could not reach {self.host}: {type(reason).__name__}")) from None

    # ---- errors ---------------------------------------------------------------
    def _rate_wait(self, status: int, head: dict, body: bytes) -> float | None:
        """Seconds to wait when this is a rate limit, else None."""
        if status not in (403, 429):
            return None
        retry_after = head.get("retry-after")
        if retry_after:
            try:
                return max(0.0, float(str(retry_after).strip()))
            except ValueError:
                pass
        remaining = head.get("x-ratelimit-remaining")
        if remaining is not None and str(remaining).strip() == "0":
            try:
                reset = float(str(head.get("x-ratelimit-reset") or 0).strip())
            except ValueError:
                reset = 0.0
            return max(0.0, reset - self._clock()) if reset else 60.0
        text = (body or b"").decode("utf-8", "replace").lower()
        if "secondary rate limit" in text or "abuse detection" in text:
            return 60.0
        return None

    def _rate_message(self, head: dict, wait: float) -> str:
        at = time.strftime("%H:%M", time.localtime(self._clock() + wait))
        return f"GitHub rate limit reached; try again at {at}."

    def _http_error(self, status: int, head: dict, body: bytes) -> Exception:
        message = ""
        try:
            parsed = json.loads((body or b"").decode("utf-8", "replace"))
            if isinstance(parsed, dict) and isinstance(parsed.get("message"), str):
                message = parsed["message"]
        except ValueError:
            message = ""
        if status in (401, 403) and ("bad credentials" in message.lower()
                                     or "requires authentication" in message.lower()
                                     or status == 401):
            return ForgeAuthError(self._safe(
                f"{self.host} refused the credential (HTTP {status}). Check GH_TOKEN, or run "
                "`gh auth login`."))
        if status == 404:
            return ForgeUnavailable(self._safe(
                f"{self.repo}: not found on {self.host} (HTTP 404). Check the name, and that the "
                "credential can see it."))
        return ForgeUnavailable(self._safe(
            f"{self.host} answered HTTP {status}" + (f": {message[:200]}" if message else ".")))

    # ---- paging ---------------------------------------------------------------
    def _get_all(self, path: str, query: dict | None = None, etag: str | None = None, *,
                 whole: bool = False):
        """Every page of a listing, or None when the first page answered 304.

        `whole=True` means a short listing would be *wrong*, not merely incomplete: the engine
        reads "this issue is not in the listing" as "this issue has not changed", so a listing cut
        off at `MAX_PAGES` would make it push the local card over a remote edit, and on a first
        sync file a second copy of every issue past the cut.  There is no safe half of that, so it
        is refused with the number to look at rather than half-done.
        """
        url = self._url(path, {**(query or {}), "per_page": PER_PAGE})
        reply = self._send("GET", url, etag=etag)
        if reply.status == 304:
            return None, etag or ""
        first_etag = reply.etag
        out = list(reply.body or [])
        pages = 1
        nxt = _parse_next(reply.link)
        while nxt and pages < MAX_PAGES:
            reply = self._send("GET", nxt)
            out.extend(reply.body or [])
            nxt = _parse_next(reply.link)
            pages += 1
        if nxt and whole:
            raise ForgeUnavailable(
                f"{self.repo}: that listing is longer than {MAX_PAGES * PER_PAGE} rows, which is "
                "more than one sync can read safely. Narrow what the sync covers and try again.")
        return out, first_etag

    # ---- the ForgeProvider interface ------------------------------------------
    def repo_info(self) -> RepoInfo:
        reply = self._send("GET", self._url(f"/repos/{self.repo}"))
        data = reply.body if isinstance(reply.body, dict) else {}
        parent = data.get("parent")
        return RepoInfo(full_name=str(data.get("full_name") or self.repo),
                        has_issues=bool(data.get("has_issues", True)),
                        parent=str(parent.get("full_name")) if isinstance(parent, dict) and
                        parent.get("full_name") else None,
                        private=bool(data.get("private")),
                        url=str(data.get("html_url") or ""))

    def list_issues(self, since: str | None = None, etag: str | None = None):
        rows, tag = self._get_all(f"/repos/{self.repo}/issues",
                                  {"state": "all", "sort": "updated", "direction": "desc",
                                   "since": since}, etag, whole=True)
        if rows is None:
            self.list_etag = etag or ""
            return None
        self.list_etag = tag
        return [issue_from(row) for row in rows if isinstance(row, dict)
                and "pull_request" not in row]

    def get_issue(self, number: int, etag: str | None = None):
        reply = self._send("GET", self._url(f"/repos/{self.repo}/issues/{int(number)}"), etag=etag)
        if reply.status == 304:
            return None
        if not isinstance(reply.body, dict):
            raise ForgeUnavailable(f"{self.repo}#{number}: unreadable reply.")
        return issue_from(reply.body, reply.etag)

    def create_issue(self, title, body, labels=(), assignees=()):
        payload = {"title": title, "body": body}
        if labels:
            payload["labels"] = list(labels)
        if assignees:
            payload["assignees"] = list(assignees)
        reply = self._send("POST", self._url(f"/repos/{self.repo}/issues"), payload)
        if not isinstance(reply.body, dict):
            raise ForgeUnavailable(f"{self.repo}: the new issue came back unreadable.")
        return issue_from(reply.body, reply.etag)

    def update_issue(self, number, **fields):
        payload = {k: (list(v) if isinstance(v, (list, tuple)) else v)
                   for k, v in fields.items() if v is not None}
        if not payload:
            return self.get_issue(number)
        reply = self._send("PATCH", self._url(f"/repos/{self.repo}/issues/{int(number)}"), payload)
        if not isinstance(reply.body, dict):
            raise ForgeUnavailable(f"{self.repo}#{number}: the update came back unreadable.")
        return issue_from(reply.body, reply.etag)

    def list_comments(self, number: int, since: str | None = None, etag: str | None = None):
        rows, tag = self._get_all(f"/repos/{self.repo}/issues/{int(number)}/comments",
                                  {"since": since}, etag, whole=True)
        if rows is None:
            self.comments_etag = etag or ""
            return None
        self.comments_etag = tag
        return [comment_from(row) for row in rows if isinstance(row, dict)]

    def create_comment(self, number: int, body: str) -> Comment:
        reply = self._send("POST", self._url(f"/repos/{self.repo}/issues/{int(number)}/comments"),
                           {"body": body})
        if not isinstance(reply.body, dict):
            raise ForgeUnavailable(f"{self.repo}#{number}: the comment came back unreadable.")
        return comment_from(reply.body)

    def update_comment(self, comment_id: int, body: str) -> Comment:
        reply = self._send("PATCH", self._url(f"/repos/{self.repo}/issues/comments/{int(comment_id)}"),
                           {"body": body})
        if not isinstance(reply.body, dict):
            raise ForgeUnavailable(f"{self.repo}: the comment edit came back unreadable.")
        return comment_from(reply.body)

    def ensure_labels(self, names: Sequence[str]) -> list[str]:
        wanted = [str(n) for n in names if str(n).strip()]
        if not wanted:
            return []
        if self._labels is None:
            rows, _ = self._get_all(f"/repos/{self.repo}/labels")
            self._labels = {str(r.get("name")) for r in (rows or []) if isinstance(r, dict)}
        made = []
        for name in wanted:
            if name in self._labels:
                continue
            try:
                self._send("POST", self._url(f"/repos/{self.repo}/labels"),
                           {"name": name, "color": _label_colour(name)})
                made.append(name)
            except ForgeUnavailable:
                pass                       # already there, or the token may not make labels
            self._labels.add(name)
        return made


# --------------------------------------------------------------------- translation

def issue_from(row: dict, etag: str = "") -> Issue:
    labels = []
    for label in row.get("labels") or []:
        if isinstance(label, dict) and label.get("name"):
            labels.append(str(label["name"]))
        elif isinstance(label, str):
            labels.append(label)
    return Issue(number=int(row.get("number") or 0),
                 title=str(row.get("title") or ""),
                 body=str(row.get("body") or ""),
                 state=str(row.get("state") or "open"),
                 state_reason=str(row.get("state_reason") or ""),
                 labels=labels,
                 assignees=[str(a.get("login")) for a in (row.get("assignees") or [])
                            if isinstance(a, dict) and a.get("login")],
                 updated_at=str(row.get("updated_at") or ""),
                 etag=etag,
                 url=str(row.get("html_url") or ""),
                 is_pull_request="pull_request" in row)


def comment_from(row: dict) -> Comment:
    user = row.get("user") if isinstance(row.get("user"), dict) else {}
    return Comment(comment_id=int(row.get("id") or 0),
                   body=str(row.get("body") or ""),
                   author=str((user or {}).get("login") or ""),
                   created_at=str(row.get("created_at") or ""),
                   updated_at=str(row.get("updated_at") or ""),
                   url=str(row.get("html_url") or ""))


def _json(raw: bytes):
    if not raw:
        return None
    try:
        return json.loads(raw.decode("utf-8", "replace"))
    except ValueError:
        return None


#: Stable colours for the labels the sync owns, so a board's tabs and lanes read at a glance.
_COLOURS = {"tab:": "0e8a16", "status:": "1d76db"}


def _label_colour(name: str) -> str:
    for prefix, colour in _COLOURS.items():
        if name.startswith(prefix):
            return colour
    return "ededed"


# -------------------------------------------------------------------- fork following

def provider_for(repo: str, token: str | None = None, *, base_url: str | None = None,
                 follow_fork: bool = True, **kwargs) -> GitHubProvider:
    """A provider for `repo`, or for its upstream when `repo` is a fork with issues disabled.

    A fork's issue tab is off by default on GitHub, and the work really lives on the parent, so
    following `parent` is what "sync this checkout's issues" means there.  The parent is used only
    when it actually has issues; otherwise the caller gets the fork and a clear refusal from the
    engine.
    """
    provider = GitHubProvider(repo, token, base_url=base_url, **kwargs)
    if not follow_fork:
        return provider
    info = provider.repo_info()
    if info.has_issues or not info.parent:
        return provider
    upstream = GitHubProvider(info.parent, token, base_url=base_url, **kwargs)
    upstream._token, upstream.token_source = provider._token, provider.token_source
    return upstream if upstream.repo_info().has_issues else provider

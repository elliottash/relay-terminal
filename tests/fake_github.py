# SPDX-License-Identifier: GPL-3.0-or-later
"""An in-process GitHub REST v3 stand-in for the Switchboard sync tests (`#GDQN`).

Enough of the API for `relay_core.forge_github`: repositories (including a fork whose issues are
disabled and whose `parent` has them), issues, comments, labels, `ETag`/`If-None-Match` with 304s,
`Link`-header pagination, pull requests mixed into the issue listing, and both rate limits.  It
listens on 127.0.0.1 on an ephemeral port, so nothing in the test suite ever reaches
api.github.com.

It also records every request (`server.requests`) and every write (`server.writes`), which is how
the dry-run and idle-sync tests prove that nothing was touched.
"""
from __future__ import annotations

import hashlib
import json
import re
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOKEN = "gho_testtoken_never_real"


class Repo:
    def __init__(self, full_name: str, has_issues: bool = True, parent: str | None = None):
        self.full_name = full_name
        self.has_issues = has_issues
        self.parent = parent
        self.issues: dict[int, dict] = {}
        self.comments: dict[int, dict] = {}
        self.labels: dict[str, dict] = {}
        self.next_issue = 1
        self.next_comment = 1000


class FakeGitHub:
    """The server plus the knobs a test turns."""

    def __init__(self, repo: str = "relay/terminal"):
        self.repos: dict[str, Repo] = {repo: Repo(repo)}
        self.main = self.repos[repo]
        self.tick = 0
        self.lock = threading.RLock()
        self.requests: list[tuple[str, str]] = []
        self.writes: list[tuple[str, str]] = []
        self.auth_seen: list[str] = []
        #: Knobs.
        self.page_size = 100                 # force pagination by lowering this
        self.fail_next: list[tuple[int, dict, dict]] = []   # (status, headers, body) queue
        self.require_token = True
        outer = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):            # keep the test output clean
                pass

            def _reply(self, status, body=None, headers=None):
                raw = b"" if body is None else json.dumps(body).encode()
                self.send_response(status)
                for key, value in (headers or {}).items():
                    self.send_header(key, str(value))
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                if raw:
                    self.wfile.write(raw)

            def _read(self):
                length = int(self.headers.get("Content-Length") or 0)
                if not length:
                    return {}
                try:
                    return json.loads(self.rfile.read(length).decode())
                except ValueError:
                    return {}

            def do_GET(self):
                outer._handle(self, "GET", None)

            def do_POST(self):
                outer._handle(self, "POST", self._read())

            def do_PATCH(self):
                outer._handle(self, "PATCH", self._read())

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        # A short poll interval: `shutdown()` waits for the next tick, and a test suite that
        # starts one of these per test should not spend half a second on each teardown.
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.02},
                                       daemon=True)
        self.thread.start()

    # ---- lifecycle
    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.server.server_port}"

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)

    # ---- helpers the tests use directly (these are *not* API calls)
    def now(self) -> str:
        self.tick += 1
        return f"2026-09-18T12:{self.tick // 60:02d}:{self.tick % 60:02d}Z"

    def repo(self, name: str | None = None) -> Repo:
        return self.repos[name] if name else self.main

    def add_repo(self, full_name: str, has_issues: bool = True, parent: str | None = None) -> Repo:
        self.repos[full_name] = Repo(full_name, has_issues, parent)
        return self.repos[full_name]

    def make_issue(self, title: str, body: str = "", labels=(), state: str = "open",
                   pull_request: bool = False, repo: str | None = None) -> dict:
        target = self.repo(repo)
        with self.lock:
            number = target.next_issue
            target.next_issue += 1
            issue = {"number": number, "title": title, "body": body, "state": state,
                     "state_reason": None, "labels": [{"name": l} for l in labels],
                     "assignees": [], "updated_at": self.now(), "created_at": self.now(),
                     "html_url": f"https://github.test/{target.full_name}/issues/{number}"}
            if pull_request:
                issue["pull_request"] = {"url": "https://github.test/pull"}
            target.issues[number] = issue
            return issue

    def web_edit(self, number: int, *, title=None, body=None, labels=None, state=None,
                 state_reason=None, repo: str | None = None) -> dict:
        """A human editing the issue in the browser: no Relay request, but `updated_at` moves."""
        target = self.repo(repo)
        with self.lock:
            issue = target.issues[number]
            if title is not None:
                issue["title"] = title
            if body is not None:
                issue["body"] = body
            if labels is not None:
                issue["labels"] = [{"name": l} for l in labels]
            if state is not None:
                issue["state"] = state
                issue["state_reason"] = state_reason
            issue["updated_at"] = self.now()
            return issue

    def web_comment(self, number: int, body: str, author: str = "octocat",
                    repo: str | None = None) -> dict:
        target = self.repo(repo)
        with self.lock:
            cid = target.next_comment
            target.next_comment += 1
            comment = {"id": cid, "issue": number, "body": body, "user": {"login": author},
                       "created_at": self.now(), "updated_at": self.now(),
                       "html_url": f"https://github.test/c/{cid}"}
            target.comments[cid] = comment
            return comment

    def comments_of(self, number: int, repo: str | None = None) -> list[dict]:
        target = self.repo(repo)
        return [c for c in target.comments.values() if c["issue"] == number]

    def refuse_next(self, status: int, headers: dict | None = None, body: dict | None = None) -> None:
        """Queue one refusal (a rate limit, say) for the next request of any kind."""
        self.fail_next.append((status, headers or {}, body or {"message": "refused"}))

    # ---- routing
    def _handle(self, handler, method: str, payload) -> None:
        path = urllib.parse.urlsplit(handler.path)
        query = urllib.parse.parse_qs(path.query)
        with self.lock:
            self.requests.append((method, handler.path))
            if method != "GET":
                self.writes.append((method, handler.path))
            self.auth_seen.append(handler.headers.get("Authorization") or "")
            if self.fail_next:
                status, headers, body = self.fail_next.pop(0)
                handler._reply(status, body, headers)
                return
            if self.require_token and handler.headers.get("Authorization") != f"Bearer {TOKEN}":
                handler._reply(401, {"message": "Bad credentials"})
                return
            try:
                self._route(handler, method, path.path, query, payload)
            except KeyError:
                handler._reply(404, {"message": "Not Found"})

    def _route(self, handler, method, path, query, payload) -> None:
        match = re.match(r"^/repos/([^/]+/[^/]+)(?P<rest>/.*)?$", path)
        if not match:
            handler._reply(404, {"message": "Not Found"})
            return
        name, rest = match.group(1), match.group("rest") or ""
        if name not in self.repos:
            handler._reply(404, {"message": "Not Found"})
            return
        repo = self.repos[name]

        if rest == "":
            body = {"full_name": repo.full_name, "has_issues": repo.has_issues,
                    "private": False, "html_url": f"https://github.test/{repo.full_name}"}
            if repo.parent:
                body["fork"] = True
                body["parent"] = {"full_name": repo.parent}
            self._conditional(handler, body)
            return

        if rest == "/issues" and method == "GET":
            rows = sorted(repo.issues.values(), key=lambda i: i["updated_at"], reverse=True)
            since = (query.get("since") or [None])[0]
            if since:
                rows = [r for r in rows if r["updated_at"] >= since]
            self._paged(handler, rows, query)
            return
        if rest == "/issues" and method == "POST":
            issue = self.make_issue(payload.get("title", ""), payload.get("body", ""),
                                    [l for l in payload.get("labels") or []], repo=name)
            issue["assignees"] = [{"login": a} for a in payload.get("assignees") or []]
            handler._reply(201, issue)
            return
        if rest == "/labels" and method == "GET":
            self._paged(handler, list(repo.labels.values()), query)
            return
        if rest == "/labels" and method == "POST":
            name_ = str(payload.get("name") or "")
            if name_ in repo.labels:
                handler._reply(422, {"message": "already_exists"})
                return
            repo.labels[name_] = {"name": name_, "color": payload.get("color") or "ededed"}
            handler._reply(201, repo.labels[name_])
            return

        comment_edit = re.match(r"^/issues/comments/(\d+)$", rest)
        if comment_edit and method == "PATCH":
            comment = repo.comments[int(comment_edit.group(1))]
            comment["body"] = payload.get("body", comment["body"])
            comment["updated_at"] = self.now()
            handler._reply(200, comment)
            return

        issue_path = re.match(r"^/issues/(\d+)(/comments)?$", rest)
        if issue_path:
            number = int(issue_path.group(1))
            if number not in repo.issues:
                handler._reply(404, {"message": "Not Found"})
                return
            if issue_path.group(2):
                if method == "GET":
                    rows = sorted(self.comments_of(number, name), key=lambda c: c["id"])
                    self._paged(handler, rows, query)
                    return
                if method == "POST":
                    handler._reply(201, self.web_comment(number, payload.get("body", ""),
                                                         "relay-bot", name))
                    return
            else:
                if method == "GET":
                    self._conditional(handler, repo.issues[number])
                    return
                if method == "PATCH":
                    issue = repo.issues[number]
                    for key in ("title", "body", "state", "state_reason"):
                        if payload.get(key) is not None:
                            issue[key] = payload[key]
                    if payload.get("labels") is not None:
                        issue["labels"] = [{"name": l} for l in payload["labels"]]
                    if payload.get("assignees") is not None:
                        issue["assignees"] = [{"login": a} for a in payload["assignees"]]
                    issue["updated_at"] = self.now()
                    handler._reply(200, issue)
                    return
        handler._reply(404, {"message": "Not Found"})

    # ---- conditional requests and paging
    def _etag(self, body) -> str:
        return '"' + hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest()[:16] + '"'

    def _conditional(self, handler, body) -> None:
        etag = self._etag(body)
        if handler.headers.get("If-None-Match") == etag:
            handler._reply(304, None, {"ETag": etag})
            return
        handler._reply(200, body, {"ETag": etag})

    def _paged(self, handler, rows, query) -> None:
        size = min(int((query.get("per_page") or [self.page_size])[0]), self.page_size)
        page = int((query.get("page") or [1])[0])
        etag = self._etag(rows)
        if page == 1 and handler.headers.get("If-None-Match") == etag:
            handler._reply(304, None, {"ETag": etag})
            return
        start = (page - 1) * size
        chunk = rows[start:start + size]
        headers = {"ETag": etag} if page == 1 else {}
        if start + size < len(rows):
            path = urllib.parse.urlsplit(handler.path)
            params = dict(urllib.parse.parse_qsl(path.query))
            params["page"] = str(page + 1)
            nxt = f"{self.base_url}{path.path}?{urllib.parse.urlencode(params)}"
            headers["Link"] = f'<{nxt}>; rel="next"'
        handler._reply(200, chunk, headers)

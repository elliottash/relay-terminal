# SPDX-License-Identifier: AGPL-3.0-or-later
"""User-configured MCP servers as tool groups a pane's agent can load (card #SSRQ).

Each enabled server (`mcp_config.load`) is one group, `mcp_<server>`, offered through
`load_tools` exactly as a task plugin's group is (protocol 36): the prompt's one line names the
group and its tools, the schemas arrive when the model loads it, and nothing above them in the
tool list moves. A guest (Claude Code, Codex) gets the same tools through its `relay_board`
bridge, which dispatches through the same `Agent._prepare/_execute` — so the trust rule below
holds for guests too, instead of each guest launching the servers itself with its own policy.

Trust (owner, 2026-09-17): a `trusted` server's tools run freely; an `untrusted` one's put an
approval ask naming the server up before each call (protocol 27.6, capability `mcp:<server>`).
"Allow for this turn" covers that server for the turn; "Always allow" marks it trusted in its
config. A server that is down, slow or misbehaving fails the one call with a readable error.

Servers are listed in the background when the agent is configured; one that answers later joins
at the start of the next turn (`take_change`). One client per server and config is kept for the
worker's life and reused across reconfigures, and every server process is stopped at exit.
"""
from __future__ import annotations

import atexit
import hashlib
import json
import re
import threading
import time

from . import approvals, mcp_config
from .mcp_client import Cancelled, McpClient, McpError

GROUP_PREFIX = "mcp_"
MAX_NAME = 64
MAX_DESCRIPTION = 1024
MAX_RESULT_TEXT = 50_000
RETRY_SECONDS = 60.0
DEFAULT_WAIT = 5.0

_HUB_LOCK = threading.Lock()
_HUB: dict[tuple, McpClient] = {}


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9_]", "_", text.lower()).strip("_") or "x"


def group_name(server: str) -> str:
    return GROUP_PREFIX + _slug(server)


def tool_name(group: str, tool: str) -> str:
    """`<group>_<tool>` in the characters every provider accepts, at most 64 of them."""
    name = f"{group}_{re.sub(r'[^A-Za-z0-9_-]', '_', tool)}"
    if len(name) > MAX_NAME:
        name = name[:MAX_NAME - 9] + "_" + hashlib.sha256(tool.encode()).hexdigest()[:8]
    return name


def _client(spec: mcp_config.ServerSpec, workspace: str | None) -> McpClient:
    key = (spec.name, spec.fingerprint(), spec.cwd or workspace or "")
    with _HUB_LOCK:
        client = _HUB.get(key)
        if client is None:
            for old in [k for k in _HUB if k[0] == spec.name]:
                _HUB.pop(old).close()          # the entry changed: a new process for the new one
            client = _HUB[key] = McpClient(spec, workspace)
        return client


@atexit.register
def close_all() -> None:
    with _HUB_LOCK:
        clients = list(_HUB.values())
        _HUB.clear()
    for client in clients:
        try:
            client.close()
        except Exception:
            pass


class _Server:
    def __init__(self, spec: mcp_config.ServerSpec, client: McpClient):
        self.spec = spec
        self.client = client
        self.group = group_name(spec.name)
        self.tools: dict[str, dict] = {}       # our name -> the server's tool object
        self.specs: list[dict] = []
        self.error = ""
        self.listed_at = 0.0
        self.listing = False


def _schema(tool: dict) -> dict:
    schema = tool.get("inputSchema")
    if not isinstance(schema, dict) or schema.get("type") != "object":
        return {"type": "object", "properties": {}}
    schema = dict(schema)
    schema.pop("$schema", None)
    schema.setdefault("properties", {})
    return schema


def result_text(result: dict) -> str:
    """An MCP `tools/call` result as the text the model reads."""
    parts: list[str] = []
    for block in result.get("content") or []:
        if not isinstance(block, dict):
            continue
        kind = block.get("type")
        if kind == "text":
            parts.append(str(block.get("text", "")))
        elif kind in ("image", "audio"):
            size = len(str(block.get("data", ""))) * 3 // 4
            parts.append(f"[{kind}: {block.get('mimeType', 'unknown type')}, about {size} bytes, not shown]")
        elif kind == "resource":
            resource = block.get("resource") if isinstance(block.get("resource"), dict) else {}
            parts.append(str(resource.get("text") or f"[resource {resource.get('uri', '')}]"))
        elif kind == "resource_link":
            parts.append(f"[resource link {block.get('uri', '')}: {block.get('name', '')}]")
    if not parts and result.get("structuredContent") is not None:
        parts.append(json.dumps(result["structuredContent"], ensure_ascii=False))
    text = "\n".join(parts)
    if len(text) > MAX_RESULT_TEXT:
        text = text[:MAX_RESULT_TEXT] + f"\n[… {len(text) - MAX_RESULT_TEXT} more characters cut]"
    return text


class McpTools:
    """What one agent may call from its configured MCP servers."""

    def __init__(self, workspace: str | None = None, *, config: mcp_config.Config | None = None,
                 client_factory=None):
        self.workspace = workspace
        self.config = config if config is not None else mcp_config.load(workspace)
        factory = client_factory or _client
        self._lock = threading.Lock()
        self._servers: dict[str, _Server] = {}
        seen: set[str] = set()
        for spec in self.config.servers:
            group = group_name(spec.name)
            if group in seen:
                self.config.problems.append(f"MCP server {spec.name!r} has the same tool prefix as "
                                            "another server and was skipped; rename one of them.")
                continue
            seen.add(group)
            self._servers[group] = _Server(spec, factory(spec, workspace))
        self._granted: set[str] = set()        # servers allowed for this agent's session
        self._taken = self._signature()

    # ----- discovery -------------------------------------------------------------------
    def discover(self, wait: float = DEFAULT_WAIT) -> None:
        """List every server's tools in the background; wait up to `wait` seconds for them."""
        threads = [self._list_async(server) for server in self._servers.values()]
        deadline = time.monotonic() + wait
        for thread in threads:
            if thread is not None:
                thread.join(max(0.0, deadline - time.monotonic()))

    def _list_async(self, server: _Server, force: bool = False):
        with self._lock:
            if server.listing or (server.specs and not force):
                return None
            if server.error and time.monotonic() - server.listed_at < RETRY_SECONDS and not force:
                return None
            server.listing = True
        thread = threading.Thread(target=self._list, args=(server,), daemon=True,
                                  name=f"mcp-list-{server.spec.name}")
        thread.start()
        return thread

    def _list(self, server: _Server) -> None:
        tools: dict[str, dict] = {}
        specs: list[dict] = []
        error = ""
        try:
            for tool in server.client.list_tools():
                name = tool_name(server.group, tool["name"])
                if name in tools:
                    continue
                tools[name] = tool
                trust = "" if server.spec.trusted else " Asks the user before each call."
                description = str(tool.get("description") or tool.get("title") or tool["name"])
                specs.append({"type": "function", "function": {
                    "name": name,
                    "description": (f"[{server.spec.name} MCP server]{trust} "
                                    + description)[:MAX_DESCRIPTION],
                    "parameters": _schema(tool)}})
        except McpError as exc:
            error = str(exc)
        except Exception as exc:                  # a misbehaving server must not kill the worker
            error = f"MCP server {server.spec.name!r} could not be listed ({type(exc).__name__})."
        with self._lock:
            server.tools, server.specs, server.error = tools, specs, error
            server.listed_at = time.monotonic()
            server.listing = False

    def refresh(self) -> None:
        """Retry servers that failed a while ago (at most every RETRY_SECONDS); never blocks."""
        for server in self._servers.values():
            if not server.specs:
                self._list_async(server)

    def _signature(self) -> tuple:
        with self._lock:
            return tuple((g, tuple(s.tools)) for g, s in sorted(self._servers.items()))

    def take_change(self) -> bool:
        """True once after the offered groups changed (a server answered, or died and was relisted)."""
        self.refresh()
        now = self._signature()
        changed, self._taken = now != self._taken, now
        return changed

    # ----- what the agent reads --------------------------------------------------------
    def groups(self) -> dict[str, tuple[tuple[str, ...], str, list[dict]]]:
        out = {}
        with self._lock:
            for group, server in self._servers.items():
                if server.specs:
                    what = f"use the {server.spec.name} MCP server"
                    if not server.spec.trusted:
                        what += " (untrusted: each call asks the user first)"
                    out[group] = (tuple(server.tools), what, list(server.specs))
        return out

    def specs(self) -> list[dict]:
        return [spec for _n, _w, specs in self.groups().values() for spec in specs]

    def _find(self, name: str) -> tuple[_Server, dict] | None:
        with self._lock:
            for server in self._servers.values():
                tool = server.tools.get(name)
                if tool is not None:
                    return server, tool
        return None

    def handles(self, name: str) -> bool:
        return self._find(name) is not None

    def group_of(self, name: str) -> str | None:
        found = self._find(name)
        return found[0].group if found else None

    def unavailable(self, name: str) -> str | None:
        """A sentence for an `mcp_*` name no connected server offers now; None otherwise."""
        if not name.startswith(GROUP_PREFIX) or self.handles(name):
            return None
        for group, server in self._servers.items():
            if name.startswith(group + "_"):
                if server.error:
                    return f"{name} is unavailable: {server.error}"
                return f"{server.spec.name} MCP server offers no tool called {name} now."
        return None

    def writes(self, name: str) -> bool:
        """False only for a tool its server marks read-only; an MCP tool may do anything."""
        found = self._find(name)
        annotations = found[1].get("annotations") if found else None
        return not (isinstance(annotations, dict) and annotations.get("readOnlyHint") is True)

    def preview(self, name: str, args: dict) -> str:
        found = self._find(name)
        server, tool = found if found else (None, {"name": name})
        body = json.dumps(args, ensure_ascii=False)
        if len(body) > 2000:
            body = body[:2000] + "…"
        head = f"MCP {server.spec.name} · {tool['name']}" if server else f"MCP {name}"
        return f"{head}\n\n{body}"

    # ----- trust -----------------------------------------------------------------------
    def approve(self, name: str, args: dict, executor) -> None:
        """Put the approval ask up for an untrusted server's call; raise the refusal on deny.

        Trusted servers and servers already allowed this session pass. An executor that may not
        draw an ask refuses the call: an untrusted server never runs unasked.
        """
        found = self._find(name)
        if found is None:
            return
        server, tool = found
        if server.spec.trusted or server.spec.name in self._granted:
            return
        capability = approvals.mcp_capability(server.spec.name)
        questions = getattr(executor, "questions", None)
        if questions is not None and questions.turn_allows(capability):
            return
        if questions is None or not getattr(executor, "may_approve", True):
            raise ValueError(approvals.refusal(capability))
        subject = f"{tool['name']} on {server.spec.name}\n{json.dumps(args, ensure_ascii=False)[:500]}"
        decision = questions.ask_approval(capability, subject)
        if decision == "deny":
            raise ValueError(approvals.refusal(capability))
        if decision == "always":
            self._granted.add(server.spec.name)
            try:
                mcp_config.set_trust(server.spec.name, mcp_config.TRUSTED, self.workspace)
            except (mcp_config.ConfigError, OSError, KeyError):
                pass                              # the session grant still holds

    # ----- running ---------------------------------------------------------------------
    def run(self, name: str, args: dict, cancel: threading.Event | None = None) -> dict:
        found = self._find(name)
        if found is None:
            raise ValueError(self.unavailable(name) or f"Unknown tool {name}.")
        server, tool = found
        try:
            result = server.client.call_tool(tool["name"], args, cancel)
        except Cancelled as exc:
            raise ValueError(str(exc)) from exc
        except McpError as exc:
            raise ValueError(str(exc)) from exc
        text = result_text(result)
        if result.get("isError"):
            raise ValueError(f"{server.spec.name} MCP server: {tool['name']} failed: {text or 'no detail'}")
        return {"server": server.spec.name, "tool": tool["name"], "content": text}

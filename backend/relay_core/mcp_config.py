# SPDX-License-Identifier: AGPL-3.0-or-later
"""Which MCP servers a pane's agent may use, and how far each is trusted (card #SSRQ).

Two sources, merged, the project winning on a name both define:

  global   $XDG_CONFIG_HOME/relay/mcp-servers.json   (RELAY_MCP_CONFIG overrides the path)
  project  <git root>/.mcp.json                      (the file Claude Code and others read)

Both use the `mcpServers` shape the other tools write — `{name: {command, args, env, cwd} |
{url, headers}}` — and the global file adds two Relay keys per server: `trust` (`trusted` runs
freely; `untrusted`, the default, puts an approval ask naming the server up before each call) and
`enabled` (default true).

**A project server does not launch until it is enabled** in the global file's `projects` block,
keyed by the project's resolved git root and pinned to a digest of that server's entry — the rule
task plugins follow (docs/TASK-PLUGINS.md). A cloned repository cannot start a command by being
opened, and a pull that changes the entry disables it until it is enabled again. Its trust lives
in that enablement too, never in the repository: a `.mcp.json` cannot declare itself trusted.

Nothing here logs or returns a server's `env` or `headers` values: they are where tokens live.

    PYTHONPATH=backend python3 -m relay_core.mcp_config list --workspace .
    PYTHONPATH=backend python3 -m relay_core.mcp_config enable <server> --workspace . [--trust trusted]
    PYTHONPATH=backend python3 -m relay_core.mcp_config add <server> --command npx --arg=-y --arg=pkg
    PYTHONPATH=backend python3 -m relay_core.mcp_config add <server> --url https://host/mcp
    PYTHONPATH=backend python3 -m relay_core.mcp_config remove|trust|enable --global|disable --global ...
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from .filelock import LOCK_EX, LOCK_UN, flock
from .instructions import git_root
from .task_plugins import relay_config_dir

TRUSTED = "trusted"
UNTRUSTED = "untrusted"
TRUST_LEVELS = (TRUSTED, UNTRUSTED)
PROJECT_FILE = ".mcp.json"
DEFAULT_TIMEOUT = 120.0
MAX_TIMEOUT = 3600.0
MAX_CONFIG_BYTES = 1024 * 1024
MAX_SERVERS = 50
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,39}$")
_VAR_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")
#: The wrapper keys other tools put the server map under, in the order they are tried (Warp's list).
WRAPPERS = (("mcpServers",), ("mcp_servers",), ("servers",), ("mcp", "servers"))
#: Keys a server entry may carry; anything else is reported, not guessed at.
_KNOWN = {"command", "args", "env", "cwd", "working_directory", "url", "headers", "type",
          "transport", "trust", "enabled", "timeout", "description"}


class ConfigError(ValueError):
    """A config that cannot be read. The message names the file and the server, never a value."""


@dataclass(frozen=True)
class ServerSpec:
    name: str
    origin: str                    # "global" or "project"
    source: str                    # the file it came from
    transport: str                 # "stdio" or "http"
    command: str = ""
    args: tuple[str, ...] = ()
    env: tuple[tuple[str, str], ...] = ()
    cwd: str = ""
    url: str = ""
    headers: tuple[tuple[str, str], ...] = ()
    trust: str = UNTRUSTED
    timeout: float = DEFAULT_TIMEOUT
    digest: str = ""

    @property
    def trusted(self) -> bool:
        return self.trust == TRUSTED

    def fingerprint(self) -> str:
        """What a running client was started from: a changed entry means a new process."""
        return self.digest

    def summary(self) -> dict:
        """A row safe to print: the command and URL, the *names* of env keys and headers only."""
        return {"name": self.name, "origin": self.origin, "transport": self.transport,
                "command": " ".join([self.command, *self.args]).strip() if self.command else "",
                "url": _redact_url(self.url), "env": sorted(k for k, _ in self.env),
                "headers": sorted(k for k, _ in self.headers), "trust": self.trust}


@dataclass
class Config:
    servers: list[ServerSpec] = field(default_factory=list)
    #: Project servers that exist but are not enabled (or changed since): name -> digest.
    pending: dict[str, str] = field(default_factory=dict)
    disabled: list[str] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)

    def get(self, name: str) -> ServerSpec | None:
        return next((s for s in self.servers if s.name == name), None)


def global_path() -> Path:
    override = os.environ.get("RELAY_MCP_CONFIG")
    return Path(override) if override else relay_config_dir() / "mcp-servers.json"


def project_key(workspace: str | Path) -> str:
    return str(git_root(Path(workspace).expanduser().resolve()))


def project_path(workspace: str | Path) -> Path:
    return Path(project_key(workspace)) / PROJECT_FILE


def entry_digest(entry: dict) -> str:
    return hashlib.sha256(json.dumps(entry, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:16]


def _redact_url(url: str) -> str:
    """A URL without its userinfo or query, which is where a token would sit."""
    if not url:
        return ""
    from urllib.parse import urlsplit
    parts = urlsplit(url)
    host = parts.hostname or ""
    if parts.port:
        host += f":{parts.port}"
    return f"{parts.scheme}://{host}{parts.path}" + ("?…" if parts.query else "")


def expand(value: str) -> str:
    """`${VAR}` from the worker's environment, the substitution Warp and Claude Code both do.
    An unset variable becomes empty rather than the literal, so it cannot leak as a fake token."""
    return _VAR_RE.sub(lambda m: os.environ.get(m.group(1), ""), value)


def read_json(path: Path) -> dict:
    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        return {}
    except OSError as exc:
        raise ConfigError(f"{path}: cannot be read ({exc.strerror}).") from exc
    if len(raw) > MAX_CONFIG_BYTES:
        raise ConfigError(f"{path}: larger than {MAX_CONFIG_BYTES} bytes.")
    try:
        data = json.loads(raw.decode("utf-8") or "{}")
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ConfigError(f"{path}: not valid JSON ({getattr(exc, 'msg', exc)}).") from exc
    if not isinstance(data, dict):
        raise ConfigError(f"{path}: the top level must be an object.")
    return data


def server_map(document: dict) -> dict:
    """The {name: entry} map under whichever wrapper key the file uses; {} for none."""
    for path in WRAPPERS:
        node = document
        for key in path:
            node = node.get(key) if isinstance(node, dict) else None
        if isinstance(node, dict):
            return node
    return {}


def _strings(value, what: str, where: str) -> tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise ConfigError(f"{where}: {what} must be a list of strings.")
    return tuple(value)


def _pairs(value, what: str, where: str) -> tuple[tuple[str, str], ...]:
    if value is None:
        return ()
    if not isinstance(value, dict) or not all(isinstance(k, str) and isinstance(v, (str, int, float))
                                              and not isinstance(v, bool) for k, v in value.items()):
        raise ConfigError(f"{where}: {what} must be an object of strings.")
    return tuple((k, str(v)) for k, v in value.items())


def parse_server(name: str, entry, *, origin: str, source: str, trust: str | None = None) -> ServerSpec:
    """One entry, validated. `trust` overrides the entry's own (a project's comes from Relay)."""
    where = f"{source}: server {name!r}"
    if not isinstance(name, str) or not NAME_RE.match(name):
        raise ConfigError(f"{source}: server name {name!r} must be 1-40 letters, digits, '_', '-' or '.'.")
    if not isinstance(entry, dict):
        raise ConfigError(f"{where} must be an object.")
    kind = str(entry.get("type") or entry.get("transport") or "").lower()
    command, url = entry.get("command"), entry.get("url")
    if command and url:
        raise ConfigError(f"{where} has both command and url; it needs one.")
    if url or kind in ("http", "streamable-http", "streamable_http", "sse"):
        if not isinstance(url, str) or not re.match(r"^https?://", url):
            raise ConfigError(f"{where}: url must be an http(s) URL.")
        if kind == "sse":
            raise ConfigError(f"{where} uses the legacy SSE transport, which Relay does not speak; "
                              "use the server's streamable HTTP endpoint or its stdio command.")
        transport = "http"
    elif isinstance(command, str) and command.strip():
        transport = "stdio"
    else:
        raise ConfigError(f"{where} needs a command (stdio) or a url (HTTP).")
    own_trust = entry.get("trust", UNTRUSTED) if trust is None else trust
    if own_trust not in TRUST_LEVELS:
        raise ConfigError(f"{where}: trust must be 'trusted' or 'untrusted'.")
    timeout = entry.get("timeout", DEFAULT_TIMEOUT)
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not 0 < timeout <= MAX_TIMEOUT:
        raise ConfigError(f"{where}: timeout must be seconds between 0 and {int(MAX_TIMEOUT)}.")
    cwd = entry.get("cwd", entry.get("working_directory", ""))
    if not isinstance(cwd, str):
        raise ConfigError(f"{where}: cwd must be a string.")
    return ServerSpec(name=name, origin=origin, source=source, transport=transport,
                      command=command if transport == "stdio" else "",
                      args=_strings(entry.get("args"), "args", where),
                      env=_pairs(entry.get("env"), "env", where), cwd=cwd,
                      url=url if transport == "http" else "",
                      headers=_pairs(entry.get("headers"), "headers", where),
                      trust=own_trust, timeout=float(timeout), digest=entry_digest(entry))


def load(workspace: str | Path | None = None) -> Config:
    """The merged view. A bad entry is a problem line and is skipped; the rest still load."""
    config = Config()
    gpath = global_path()
    try:
        document = read_json(gpath)
    except ConfigError as exc:
        config.problems.append(str(exc))
        document = {}
    merged: dict[str, ServerSpec] = {}
    for name, entry in server_map(document).items():
        try:
            spec = parse_server(name, entry, origin="global", source=str(gpath))
        except ConfigError as exc:
            config.problems.append(str(exc))
            continue
        if isinstance(entry, dict) and entry.get("enabled") is False:
            config.disabled.append(name)
            continue
        merged[name] = spec
    if workspace:
        ppath = project_path(workspace)
        grants = (document.get("projects") or {}).get(project_key(workspace)) or {}
        try:
            project = server_map(read_json(ppath))
        except ConfigError as exc:
            config.problems.append(str(exc))
            project = {}
        for name, entry in project.items():
            grant = grants.get(name) if isinstance(grants, dict) else None
            digest = entry_digest(entry) if isinstance(entry, dict) else ""
            if not isinstance(grant, dict) or grant.get("digest") != digest or grant.get("enabled") is False:
                config.pending[name] = digest
                continue
            try:
                merged[name] = parse_server(name, entry, origin="project", source=str(ppath),
                                            trust=grant.get("trust", UNTRUSTED))
            except ConfigError as exc:
                config.problems.append(str(exc))
    config.servers = list(merged.values())[:MAX_SERVERS]
    if len(merged) > MAX_SERVERS:
        config.problems.append(f"Only the first {MAX_SERVERS} MCP servers are used.")
    return config


def update_global(mutate) -> dict:
    """Read-modify-write the global file under a lock, atomically, keeping its mode 0600."""
    path = global_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    lock_path = path.with_name(path.name + ".lock")
    with open(lock_path, "a+") as lock:
        flock(lock.fileno(), LOCK_EX)
        try:
            document = read_json(path)
            mutate(document)
            fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name + ".")
            try:
                with os.fdopen(fd, "w") as out:
                    json.dump(document, out, indent=2, sort_keys=True)
                    out.write("\n")
                os.chmod(tmp, 0o600)
                os.replace(tmp, path)
            except BaseException:
                Path(tmp).unlink(missing_ok=True)
                raise
            return document
        finally:
            flock(lock.fileno(), LOCK_UN)


def _servers_of(document: dict) -> dict:
    """The global file's server map, created under `mcpServers` when it has none."""
    for path in WRAPPERS:
        node = document
        for key in path:
            node = node.get(key) if isinstance(node, dict) else None
        if isinstance(node, dict):
            return node
    return document.setdefault("mcpServers", {})


def enable_project(workspace: str | Path, name: str, trust: str = UNTRUSTED) -> str:
    """Enable one project server as it is now; returns the digest it was pinned to."""
    if trust not in TRUST_LEVELS:
        raise ConfigError("trust must be 'trusted' or 'untrusted'.")
    ppath = project_path(workspace)
    entry = server_map(read_json(ppath)).get(name)
    if entry is None:
        raise ConfigError(f"{ppath} has no server {name!r}.")
    parse_server(name, entry, origin="project", source=str(ppath), trust=trust)
    digest = entry_digest(entry)
    key = project_key(workspace)
    update_global(lambda d: d.setdefault("projects", {}).setdefault(key, {}).__setitem__(
        name, {"digest": digest, "trust": trust, "enabled": True}))
    return digest


def disable_project(workspace: str | Path, name: str) -> None:
    key = project_key(workspace)
    update_global(lambda d: (d.get("projects") or {}).get(key, {}).pop(name, None))


def set_trust(name: str, trust: str, workspace: str | Path | None = None) -> str:
    """Set a server's trust where it is defined: the project grant if the project's server of
    that name is the one in force, otherwise the global entry. Returns the origin it changed."""
    if trust not in TRUST_LEVELS:
        raise ConfigError("trust must be 'trusted' or 'untrusted'.")
    spec = load(workspace).get(name)
    if spec is None:
        raise ConfigError(f"No enabled MCP server named {name!r}.")
    if spec.origin == "project":
        key = project_key(workspace)
        update_global(lambda d: d["projects"][key][name].__setitem__("trust", trust))
    else:
        update_global(lambda d: _servers_of(d)[name].__setitem__("trust", trust))
    return spec.origin


def _print_list(workspace: str | None, as_json: bool = False) -> int:
    config = load(workspace)
    if as_json:
        # Card #9M96: the Options pane reads this; keep it the same redacted shape as the table.
        rows = []
        for spec in config.servers:
            rows.append({**spec.summary(), "enabled": True, "source": spec.source})
        ppath = project_path(workspace or ".")
        try:
            project = server_map(read_json(ppath)) if config.pending else {}
        except ConfigError:
            project = {}
        for name in config.pending:
            # What enabling it would launch, so the pane can show it before anyone says yes.
            try:
                shown = parse_server(name, project.get(name), origin="project", source=str(ppath)).summary()
            except ConfigError:
                shown = {"name": name, "origin": "project", "transport": "", "command": "", "url": "",
                         "env": [], "headers": []}
            rows.append({**shown, "trust": "", "enabled": False, "pending": True, "source": str(ppath)})
        rows += [{"name": name, "origin": "global", "transport": "", "command": "", "url": "",
                  "env": [], "headers": [], "trust": "", "enabled": False, "pending": False,
                  "source": str(global_path())} for name in config.disabled]
        print(json.dumps({"servers": rows, "problems": config.problems,
                          "global_path": str(global_path()),
                          "project_path": str(project_path(workspace)) if workspace else ""}))
        return 0
    for spec in config.servers:
        row = spec.summary()
        where = row["command"] or row["url"]
        print(f"{spec.name:<24} {spec.origin:<8} {spec.trust:<10} {spec.transport:<6} {where}")
    for name in config.pending:
        print(f"{name:<24} project  (not enabled: `enable {name}` to review and allow it)")
    for name in config.disabled:
        print(f"{name:<24} global   (disabled)")
    for line in config.problems:
        print(f"problem: {line}", file=sys.stderr)
    if not (config.servers or config.pending or config.disabled):
        print(f"No MCP servers configured ({global_path()}"
              + (f", {project_path(workspace)}" if workspace else "") + ").")
    return 0


def add_global(name: str, entry: dict) -> ServerSpec:
    """Validate `entry` and write it into the global file; refuses to overwrite. (#9M96)"""
    spec = parse_server(name, entry, origin="global", source=str(global_path()))

    def mutate(document: dict) -> None:
        servers = _servers_of(document)
        if name in servers:
            raise ConfigError(f"{global_path()}: server {name!r} already exists "
                              f"(remove it first, or `trust {name}` to change its trust).")
        servers[name] = entry

    update_global(mutate)
    return spec


def remove_global(name: str) -> None:
    def mutate(document: dict) -> None:
        servers = _servers_of(document)
        if name not in servers:
            raise ConfigError(f"{global_path()} has no server {name!r}.")
        del servers[name]

    update_global(mutate)


def set_enabled(name: str, enabled: bool) -> None:
    """Flip a global server's `enabled` flag (disabled servers stay configured but never load)."""
    def mutate(document: dict) -> None:
        servers = _servers_of(document)
        if name not in servers:
            raise ConfigError(f"{global_path()} has no server {name!r}.")
        if enabled:
            servers[name].pop("enabled", None)
        else:
            servers[name]["enabled"] = False

    update_global(mutate)


def _kv_pairs(items: list[str], what: str) -> dict:
    """CLI `KEY=VALUE` pairs into the dict the schema wants."""
    out: dict[str, str] = {}
    for item in items:
        key, sep, value = item.partition("=")
        if not sep or not key.strip():
            raise ConfigError(f"--{what} wants KEY=VALUE, got {item!r}.")
        out[key.strip()] = value
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="relay_core.mcp_config", description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    for cmd in ("list", "enable", "disable", "trust"):
        p = sub.add_parser(cmd)
        p.add_argument("--workspace", default=None)
        if cmd == "list":
            p.add_argument("--json", action="store_true", help="machine-readable rows (the Options pane reads this)")
        else:
            p.add_argument("server")
        if cmd in ("enable", "disable"):
            p.add_argument("--global", dest="is_global", action="store_true",
                           help="switch a global server on or off instead of a project one")
        if cmd in ("enable", "trust"):
            p.add_argument("--trust" if cmd == "enable" else "level", choices=TRUST_LEVELS,
                           default=UNTRUSTED if cmd == "enable" else None)
    add = sub.add_parser("add", help="add a global server")
    add.add_argument("server")
    where = add.add_mutually_exclusive_group(required=True)
    where.add_argument("--command", help="stdio: the executable to launch")
    where.add_argument("--url", help="streamable HTTP endpoint")
    add.add_argument("--arg", action="append", default=[], help="one argument, as --arg=VALUE (repeatable)")
    add.add_argument("--env", action="append", default=[], help="KEY=VALUE for the server (repeatable)")
    add.add_argument("--header", action="append", default=[], help="KEY=VALUE HTTP header (repeatable)")
    add.add_argument("--trust", choices=TRUST_LEVELS, default=UNTRUSTED)
    add.add_argument("--timeout", type=float, default=None)
    add.add_argument("--secrets-stdin", action="store_true",
                     help='read {"env": {...}, "headers": {...}} as JSON on stdin, so values stay out of argv')
    remove = sub.add_parser("remove", help="remove a global server")
    remove.add_argument("server")
    args = parser.parse_args(argv)
    try:
        if args.cmd == "list":
            return _print_list(args.workspace, args.json)
        if args.cmd == "add":
            entry: dict = {"command": args.command} if args.command else {"url": args.url}
            if args.arg:
                entry["args"] = args.arg
            if args.env:
                entry["env"] = _kv_pairs(args.env, "env")
            if args.header:
                entry["headers"] = _kv_pairs(args.header, "header")
            if args.secrets_stdin:
                try:
                    secrets = json.loads(sys.stdin.read() or "{}")
                except ValueError as exc:
                    raise ConfigError(f"--secrets-stdin: not JSON ({exc.msg}).") from None
                if not isinstance(secrets, dict):
                    raise ConfigError("--secrets-stdin wants a JSON object.")
                for key in ("env", "headers"):
                    if secrets.get(key):
                        if not isinstance(secrets[key], dict):
                            raise ConfigError(f"--secrets-stdin: {key} must be an object.")
                        entry[key] = {**entry.get(key, {}), **secrets[key]}
            if args.timeout is not None:
                entry["timeout"] = args.timeout
            entry["trust"] = args.trust
            spec = add_global(args.server, entry)
            print(f"Added {args.server} ({spec.transport}, {spec.trust}) to {global_path()}.")
        elif args.cmd == "remove":
            remove_global(args.server)
            print(f"Removed {args.server} from {global_path()}.")
        elif args.cmd == "enable" and args.is_global:
            set_enabled(args.server, True)
            print(f"Enabled global server {args.server}.")
        elif args.cmd == "disable" and args.is_global:
            set_enabled(args.server, False)
            print(f"Disabled global server {args.server}.")
        elif args.cmd == "enable":
            workspace = args.workspace or "."
            entry = server_map(read_json(project_path(workspace))).get(args.server)
            if entry is not None:
                spec = parse_server(args.server, entry, origin="project", source=str(project_path(workspace)),
                                    trust=args.trust)
                print(json.dumps(spec.summary(), indent=2))
            enable_project(workspace, args.server, args.trust)
            print(f"Enabled {args.server} for {project_key(workspace)} ({args.trust}).")
        elif args.cmd == "disable":
            disable_project(args.workspace or ".", args.server)
            print(f"Disabled {args.server} for {project_key(args.workspace or '.')}.")
        else:
            origin = set_trust(args.server, args.level, args.workspace)
            print(f"{args.server} is now {args.level} ({origin}).")
    except ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

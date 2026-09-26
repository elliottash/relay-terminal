# SPDX-License-Identifier: AGPL-3.0-or-later
"""Import MCP servers from Claude Code, Codex and Warp into Relay's global config (card #SSRQ).

    PYTHONPATH=backend python3 -m relay_core.mcp_import                 # preview only
    PYTHONPATH=backend python3 -m relay_core.mcp_import --add github,fs  # add those rows
    PYTHONPATH=backend python3 -m relay_core.mcp_import --all --trust trusted
    PYTHONPATH=backend python3 -m relay_core.mcp_import --interactive    # ask row by row

Sources, each read-only:

  claude  ~/.claude.json — `mcpServers` (user scope), and `projects[<workspace>].mcpServers`
          (local scope) when --workspace names a project
  codex   ~/.codex/config.toml — `[mcp_servers.<name>]`; `bearer_token_env_var` and `http_headers`
          become headers, `${VAR}` references kept as references
  warp    ~/.warp/.mcp.json — Warp's file-based servers (gallery installs live in Warp's own
          database and must be added again by hand)

The preview is a table: the name, where it came from, the command or URL, the *names* of its env
keys — never their values — and what adding it would do (`new`, `same` as a server already there,
`conflict` with a different server of that name, or `invalid` with the reason). Only rows named
with --add, --all or confirmed with --interactive are written, and a conflict is never overwritten.
Imported servers are `untrusted` unless --trust says otherwise.
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from . import mcp_config

try:
    import tomllib
except ModuleNotFoundError:                      # pragma: no cover - Python < 3.11
    tomllib = None


@dataclass
class Row:
    name: str
    source: str
    entry: dict
    status: str = "new"
    detail: str = ""

    def where(self) -> str:
        if self.entry.get("url"):
            return mcp_config._redact_url(str(self.entry["url"]))
        return " ".join([str(self.entry.get("command", "")), *map(str, self.entry.get("args") or [])]).strip()


def _home() -> Path:
    return Path.home()


def read_claude(path: Path, workspace: str | None = None) -> list[Row]:
    document = mcp_config.read_json(path)
    rows = [Row(n, "claude", e) for n, e in (document.get("mcpServers") or {}).items()]
    if workspace:
        wanted = str(Path(workspace).expanduser().resolve())
        for key, project in (document.get("projects") or {}).items():
            if isinstance(project, dict) and str(Path(key).expanduser().resolve()) == wanted:
                rows += [Row(n, "claude (project)", e) for n, e in (project.get("mcpServers") or {}).items()]
    return rows


def read_codex(path: Path) -> list[Row]:
    if not path.is_file():
        return []
    if tomllib is None:
        raise mcp_config.ConfigError(f"{path}: reading TOML needs Python 3.11 or newer.")
    try:
        document = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise mcp_config.ConfigError(f"{path}: not readable TOML ({exc}).") from exc
    rows = []
    for name, raw in (document.get("mcp_servers") or {}).items():
        if not isinstance(raw, dict):
            rows.append(Row(name, "codex", {}, "invalid", "not a table"))
            continue
        entry = {k: raw[k] for k in ("command", "args", "env", "cwd", "url") if k in raw}
        headers = dict(raw.get("http_headers") or {})
        if raw.get("bearer_token_env_var"):
            headers["Authorization"] = "Bearer ${" + str(raw["bearer_token_env_var"]) + "}"
        for header, var in (raw.get("env_http_headers") or {}).items():
            headers[header] = "${" + str(var) + "}"
        if headers:
            entry["headers"] = headers
        if raw.get("tool_timeout_sec"):
            entry["timeout"] = raw["tool_timeout_sec"]
        if raw.get("enabled") is False:
            entry["enabled"] = False
        rows.append(Row(name, "codex", entry))
    return rows


def read_warp(path: Path) -> list[Row]:
    document = mcp_config.read_json(path)
    return [Row(n, "warp", e) for n, e in mcp_config.server_map(document).items()]


def _clean(entry: dict) -> dict:
    """The keys Relay's global file keeps; `working_directory` becomes `cwd`, a `type` other
    tools write for stdio (`stdio`) is dropped as redundant."""
    out = {k: entry[k] for k in ("command", "args", "env", "cwd", "url", "headers", "timeout", "enabled")
           if k in entry}
    if "working_directory" in entry and "cwd" not in out:
        out["cwd"] = entry["working_directory"]
    kind = str(entry.get("type") or "").lower()
    if kind and kind != "stdio":
        out["type"] = kind
    return out


def collect(*, workspace: str | None = None, claude: Path | None = None, codex: Path | None = None,
            warp: Path | None = None) -> tuple[list[Row], list[str]]:
    """Every row from every source, with its status against the global file; and the problems."""
    home = _home()
    sources = (("claude", lambda: read_claude(claude or home / ".claude.json", workspace)),
               ("codex", lambda: read_codex(codex or home / ".codex" / "config.toml")),
               ("warp", lambda: read_warp(warp or home / ".warp" / ".mcp.json")))
    rows, problems = [], []
    for label, reader in sources:
        try:
            rows += reader()
        except mcp_config.ConfigError as exc:
            problems.append(f"{label}: {exc}")
    existing = mcp_config.server_map(mcp_config.read_json(mcp_config.global_path()))
    seen: dict[str, dict] = {}
    for row in rows:
        if row.status == "invalid":
            continue
        row.entry = _clean(row.entry) if isinstance(row.entry, dict) else row.entry
        try:
            mcp_config.parse_server(row.name, row.entry, origin="global", source=row.source)
        except mcp_config.ConfigError as exc:
            row.status, row.detail = "invalid", str(exc).split(": ", 1)[-1]
            continue
        current = existing.get(row.name)
        if current is not None:
            mine = {k: v for k, v in current.items() if k not in ("trust",)} if isinstance(current, dict) else current
            row.status = "same" if mine == row.entry else "conflict"
        elif row.name in seen:
            row.status = "same" if seen[row.name] == row.entry else "conflict"
            row.detail = "also in an earlier source"
        else:
            seen[row.name] = row.entry
    return rows, problems


def preview(rows: list[Row]) -> str:
    if not rows:
        return "No MCP servers found in Claude Code, Codex or Warp."
    lines = [f"{'#':>2}  {'name':<22} {'source':<16} {'status':<9} {'env keys':<20} command / url"]
    for i, row in enumerate(rows, 1):
        keys = ",".join(sorted((row.entry.get("env") or {}) if isinstance(row.entry, dict) else {}))
        lines.append(f"{i:>2}  {row.name:<22} {row.source:<16} {row.status:<9} {keys[:20]:<20} "
                     f"{row.where()[:70]}" + (f"  ({row.detail})" if row.detail else ""))
    return "\n".join(lines)


def apply(rows: list[Row], chosen: set[str], trust: str) -> list[str]:
    """Write the chosen `new` rows into the global file; returns the names added."""
    if trust not in mcp_config.TRUST_LEVELS:
        raise mcp_config.ConfigError("trust must be 'trusted' or 'untrusted'.")
    take = [r for r in rows if r.status == "new" and r.name in chosen]
    added: list[str] = []

    def mutate(document: dict) -> None:
        servers = mcp_config._servers_of(document)
        for row in take:
            if row.name not in servers:            # re-checked under the lock
                servers[row.name] = {**row.entry, "trust": trust}
                added.append(row.name)

    if take:
        mcp_config.update_global(mutate)
    return added


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="relay_core.mcp_import",
                                     description="Preview and import MCP servers from Claude Code, Codex and Warp.")
    parser.add_argument("--workspace", help="also read Claude Code's local-scope servers for this project")
    pick = parser.add_mutually_exclusive_group()
    pick.add_argument("--add", default="", help="comma-separated names to add")
    pick.add_argument("--all", action="store_true", help="add every row whose status is new")
    pick.add_argument("--interactive", action="store_true", help="ask about each new row")
    parser.add_argument("--trust", choices=mcp_config.TRUST_LEVELS, default=mcp_config.UNTRUSTED)
    parser.add_argument("--claude-config", type=Path)
    parser.add_argument("--codex-config", type=Path)
    parser.add_argument("--warp-config", type=Path)
    args = parser.parse_args(argv)
    try:
        rows, problems = collect(workspace=args.workspace, claude=args.claude_config,
                                 codex=args.codex_config, warp=args.warp_config)
    except mcp_config.ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(preview(rows))
    for line in problems:
        print(f"problem: {line}", file=sys.stderr)
    fresh = [r for r in rows if r.status == "new"]
    if args.all:
        chosen = {r.name for r in fresh}
    elif args.add:
        chosen = {n.strip() for n in args.add.split(",") if n.strip()}
        unknown = chosen - {r.name for r in fresh}
        for name in sorted(unknown):
            print(f"skipped {name}: not a new row in the preview", file=sys.stderr)
    elif args.interactive:
        chosen = set()
        for row in fresh:
            answer = input(f"Add {row.name} ({row.source}: {row.where()[:60]}) as {args.trust}? [y/N] ")
            if answer.strip().lower() in ("y", "yes"):
                chosen.add(row.name)
    else:
        if fresh:
            print(f"\nNothing written. Add rows with --add NAME[,NAME], --all or --interactive "
                  f"(to {mcp_config.global_path()}).")
        return 0
    try:
        added = apply(rows, chosen, args.trust)
    except (mcp_config.ConfigError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"\nAdded {len(added)} server(s) as {args.trust}: {', '.join(added) or 'none'} "
          f"({mcp_config.global_path()}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())

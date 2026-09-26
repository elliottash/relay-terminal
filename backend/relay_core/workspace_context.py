# SPDX-License-Identifier: AGPL-3.0-or-later
"""Canonical project identity and pre-launch execution workspace for queue mode."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from . import trees


class WorkspacePreparationError(RuntimeError):
    pass


def prepare(project: str, session: str, *, card: str | None = None,
            state_root=None, workspace_id: str | None = None,
            planning_only: bool = False) -> dict:
    """Resolve once before launch. Never convert queue allocation failure to shared cwd."""
    root = str(Path(project).expanduser().resolve())
    repo = trees.resolve_project(root, state_root=state_root)
    if repo is None or repo["mode"] == "legacy":
        return dict(project_root=repo["project_root"] if repo else root,
                    board_root=repo["board_root"] if repo else "",
                    repo_id=repo["id"] if repo else "", workspace_id="",
                    execution_cwd=root, branch="", base_sha="", state="legacy",
                    recoverable=False, reason="")
    if repo["mode"] != "queue":
        raise WorkspacePreparationError(f"project is {repo['mode']}; development launch refused")
    if not session:
        raise WorkspacePreparationError("queue development requires a session token")
    try:
        from .integration_service import IntegrationService, ServiceError
    except ImportError as exc:
        raise WorkspacePreparationError("integration service is unavailable; development launch refused") from exc
    service = IntegrationService(repo["project_root"], state_root=state_root, register=False)
    try:
        if service.mode() != "queue":
            raise WorkspacePreparationError("publication transition is paused; development launch refused")
        if workspace_id:
            row = service.tree_status(workspace_id)
            if row["session"] != session or row["state"] != "active":
                raise WorkspacePreparationError("workspace lease does not belong to this active session")
        elif planning_only:
            return dict(project_root=repo["project_root"], board_root=repo["board_root"],
                        repo_id=repo["id"], workspace_id="", execution_cwd=repo["project_root"],
                        branch="", base_sha="", state="planning", recoverable=False, reason="")
        else:
            row = service.allocate_workspace(session, card=card)
        if row["state"] != "active":
            raise WorkspacePreparationError(row.get("reason") or f"workspace is {row['state']}")
    except (trees.TreeError, ServiceError) as exc:
        raise WorkspacePreparationError(str(exc)) from exc
    return dict(project_root=repo["project_root"], board_root=repo["board_root"],
                repo_id=repo["id"], workspace_id=row["workspace_id"],
                execution_cwd=row["execution_cwd"], branch=row["branch"],
                base_sha=row["base_sha"], state="active", recoverable=True, reason="")


def validate_prepared(status: dict, project: str, session: str, *, state_root=None) -> dict:
    """Check a GUI-provided allocation without allocating a second workspace."""
    if not isinstance(status, dict):
        raise WorkspacePreparationError("tree_status must be an object")
    verified = prepare(project, session, workspace_id=status.get("workspace_id") or None,
                       state_root=state_root)
    for key in ("repo_id", "workspace_id", "execution_cwd", "board_root"):
        if status.get(key, "") != verified[key]:
            raise WorkspacePreparationError(f"tree_status {key} does not match the lease")
    return verified


def environment(status: dict) -> dict[str, str]:
    if status.get("state") != "active":
        return {}
    return {"RELAY_PROJECT_ROOT": status["project_root"],
            "RELAY_BOARD_ROOT": status["board_root"],
            "RELAY_WORKSPACE_ID": status["workspace_id"]}


def submission_instructions(status: dict) -> str:
    if status.get("state") != "active":
        return ""
    return (f"Canonical project: {status['project_root']}\n"
            f"Canonical Board: {status['board_root']}\n"
            f"Private development branch: {status['branch']} "
            f"(workspace {status['workspace_id']}). "
            "Commit normally with git in this workspace, run the project tests, then "
            "submit the commit with relay-land submit HEAD --request-id <unique-id>. "
            "Do not use scripts/land.py begin, commit or try here, even if the "
            "project instructions describe the old shared checkout workflow.")


def queue_status(project: str, *, state_root=None) -> dict:
    repo = trees.resolve_project(project, state_root=state_root)
    if repo is None:
        raise WorkspacePreparationError("project is not registered")
    if repo["mode"] != "queue":
        return {"repo_id": repo["id"], "jobs": []}
    try:
        from .integration_service import IntegrationService
    except ImportError as exc:
        raise WorkspacePreparationError("integration service is unavailable") from exc
    return IntegrationService(repo["project_root"], state_root=state_root,
                              register=False).queue_status()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="python -m relay_core.workspace_context")
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare")
    p.add_argument("--project", required=True)
    p.add_argument("--session", required=True)
    p.add_argument("--card")
    p.add_argument("--state-root")
    p.add_argument("--workspace-id")
    p.add_argument("--planning-only", action="store_true")
    q = commands.add_parser("queue-status")
    q.add_argument("--project", required=True)
    q.add_argument("--state-root")
    args = parser.parse_args(argv)
    try:
        result = (queue_status(args.project, state_root=args.state_root)
                  if args.command == "queue-status" else
                  prepare(args.project, args.session, card=args.card,
                          state_root=args.state_root, workspace_id=args.workspace_id,
                          planning_only=args.planning_only))
    except WorkspacePreparationError as exc:
        print(json.dumps({"state": "refused", "recoverable": True, "reason": str(exc)}))
        return 2
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

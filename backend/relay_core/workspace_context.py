# SPDX-License-Identifier: AGPL-3.0-or-later
"""Canonical project identity and pre-launch execution workspace for queue mode."""
from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from . import projectconf, trees


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
    manager = trees.TreeManager(repo["project_root"], state_root=state_root)
    try:
        if workspace_id:
            row = manager.get(workspace_id)
            if row["session"] != session or row["status"] != "active":
                raise WorkspacePreparationError("workspace lease does not belong to this active session")
        elif planning_only:
            return dict(project_root=repo["project_root"], board_root=repo["board_root"],
                        repo_id=repo["id"], workspace_id="", execution_cwd=repo["project_root"],
                        branch="", base_sha="", state="planning", recoverable=False, reason="")
        else:
            # The registered target's version is the accepted launch policy. The publisher
            # separately stores and enforces the accepted verification policy.
            config = projectconf.load(repo["project_root"], revision=repo["target"], require_file=True)
            settings = config["workspace"]
            row = manager.create(session, card=card, excludes=settings["exclude"],
                                 init=settings["init"], max_workspaces=settings["max_workspaces"])
        if row["status"] != "active":
            raise WorkspacePreparationError(row.get("init_error") or f"workspace is {row['status']}")
    except (trees.TreeError, projectconf.ProjectConfigError) as exc:
        raise WorkspacePreparationError(str(exc)) from exc
    return dict(project_root=repo["project_root"], board_root=repo["board_root"],
                repo_id=repo["id"], workspace_id=row["id"],
                execution_cwd=row["path"], branch=row["branch"],
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
    from .landq import Queue
    jobs = []
    queue = Queue(repo["project_root"], state_root=state_root)
    for job in queue.status():
        try:
            created = datetime.fromisoformat(job["created_at"].replace("Z", "+00:00"))
            age = max(0, int((datetime.now(timezone.utc) - created).total_seconds()))
        except (KeyError, ValueError, TypeError):
            age = 0
        receipt = queue.receipt(job["id"]) if job["status"] == "landed" else None
        jobs.append({"id": job["id"], "card": job.get("card"),
                     "workspace_id": job.get("workspace_id"), "status": job["status"],
                     "reason": job.get("reason") or "", "age_seconds": age,
                     "candidate_sha": job.get("candidate_sha") or "",
                     "published_sha": (receipt or {}).get("published_sha") or ""})
    result = {"repo_id": repo["id"], "jobs": jobs}
    try:
        from .main_release import MainRelease
        result["main_release"] = MainRelease(repo["project_root"], state_root=state_root,
                                              repo_id=repo["id"]).status()
    except (OSError, ValueError):
        pass
    return result


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

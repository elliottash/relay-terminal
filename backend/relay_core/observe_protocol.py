# SPDX-License-Identifier: GPL-3.0-or-later
"""Worker handlers for protocol section 11: routing assist, stored tool outputs and turn transcripts,
and skill management (docs/AGENT-SESSIONS-PROTOCOL.md).

Network and git work runs on background threads so the protocol loop never blocks.
"""
from __future__ import annotations

import os
import threading
from pathlib import Path

from . import route_assist, skill_manage
from .provider import ProviderError
from .skills import SkillError, SkillIndex, refined_dir

TYPES = {"route_assist", "tool_output_get", "turn_transcript_get", "skills_list", "refine_skills",
         "import_skills_preview", "import_skills_confirm", "skills_check_updates"}


class ObserveCommands:
    def __init__(self, turns, emit, imports: skill_manage.SkillImports | None = None):
        self.turns = turns
        self.emit = emit
        self.imports = imports or skill_manage.SkillImports()

    @staticmethod
    def handles(kind) -> bool:
        return kind in TYPES

    def handle(self, kind: str, request: dict) -> None:
        getattr(self, "_" + kind)(request)

    def shutdown(self) -> None:
        self.imports.shutdown()

    # ----- helpers --------------------------------------------------------------------------
    def _agent(self):
        agent = self.turns.agent
        if agent is None:
            raise ValueError("Configure a provider and workspace first.")
        return agent

    def _background(self, name: str, request_id, work) -> None:
        def run():
            try:
                event = work()
                if event is not None:
                    event.setdefault("id", request_id)
                    self.emit(event)
            except Exception as exc:
                text = (str(exc)[:2000] if isinstance(exc, (ValueError, OSError, ProviderError))
                        else f"{name} failed ({type(exc).__name__}).")
                self.emit({"event": "error", "id": request_id, "source": name, "text": text})
        threading.Thread(target=run, name=f"relay-{name}", daemon=True).start()

    # ----- routing assist ---------------------------------------------------------------------
    def _route_assist(self, request):
        agent = self.turns.agent
        if agent is None:
            route_assist.validate(request)
            self.emit({"event": "route_assisted", "id": request.get("id"), "route": None,
                       "error": "not_configured", "elapsed_ms": 0})
            return
        provider = agent.side_provider(cheap=True, max_tokens=route_assist.MAX_TOKENS)
        route_assist.run(provider, request, self.emit)

    # ----- turns -------------------------------------------------------------------------------
    def _tool_output_get(self, request):
        event = self._agent().tool_output(request.get("turn_id"), request.get("call_id"))
        event["id"] = request.get("id")
        self.emit(event)

    def _turn_transcript_get(self, request):
        event = self._agent().turn_transcript(request.get("turn_id"))
        event["id"] = request.get("id")
        self.emit(event)

    # ----- skills --------------------------------------------------------------------------------
    def _index(self):
        agent = self.turns.agent
        return agent.executor.skills if agent is not None else None

    def _settings(self, request):
        workspace = request.get("workspace")
        if workspace is not None and (not isinstance(workspace, str) or not os.path.isdir(workspace)):
            raise ValueError("workspace must be an existing directory.")
        agent = self.turns.agent
        if workspace is None and agent is not None:
            workspace = str(agent.executor.workspace.root)
        return skill_manage.index_settings(self._index(), workspace)

    def _skills_list(self, request):
        directories, exclude, workspace = self._settings(request)
        items = skill_manage.list_skills(directories, exclude, workspace)
        index = self._index()
        self.emit({"event": "skills", "id": request.get("id"), "items": items,
                   "skipped": index.skipped[:100] if index is not None else []})

    def _reload_skills(self) -> bool:
        """Pick up refined or imported skills in the current agent when it is idle. Returns True if reloaded."""
        agent = self.turns.agent
        index = self._index()
        if agent is None or index is None or self.turns.busy:
            return False
        directories, exclude, _ = skill_manage.index_settings(index)
        fresh = SkillIndex.load(directories, exclude, defaults=index.defaults)
        fresh.project, fresh.workspace = index.project, index.workspace
        agent.executor.skills = fresh
        agent.refresh_system_prompt()
        return True

    def _refine_skills(self, request):
        agent = self._agent()
        names = request.get("names")
        if (not isinstance(names, list) or not names or len(names) > skill_manage.MAX_NAMES
                or not all(isinstance(n, str) and n for n in names)):
            raise ValueError("names must be a non-empty list of skill names.")
        target = request.get("target_dir")
        if target is not None and (not isinstance(target, str) or not os.path.isabs(os.path.expanduser(target))):
            raise ValueError("target_dir must be an absolute path.")
        target_dir = Path(os.path.expanduser(target)) if target else refined_dir()
        directories, exclude, workspace = self._settings(request)
        listing = skill_manage.list_skills(directories, exclude, workspace)
        provider = agent.side_provider()

        def work():
            items, errors = [], []
            for name in dict.fromkeys(names):
                try:
                    items.append(skill_manage.refine_one(provider, listing, name, target_dir))
                except (SkillError, ValueError, OSError, UnicodeError, ProviderError) as exc:
                    errors.append({"name": name, "error": str(exc)[:500]})
            reloaded = self._reload_skills() if items and target is None else False
            return {"event": "skills_refined", "items": items, "errors": errors, "reloaded": reloaded}
        self._background("refine_skills", request.get("id"), work)

    def _import_skills_preview(self, request):
        url, ref = skill_manage.validate_url(request.get("url")), skill_manage.validate_ref(request.get("ref"))
        self._background("import_skills_preview", request.get("id"), lambda: self.imports.preview(url, ref))

    def _import_skills_confirm(self, request):
        url = skill_manage.validate_url(request.get("url"))

        def work():
            event = self.imports.confirm(url, request.get("commit"), request.get("names"))
            event["reloaded"] = self._reload_skills()
            return event
        self._background("import_skills_confirm", request.get("id"), work)

    def _skills_check_updates(self, request):
        url, ref = skill_manage.validate_url(request.get("url")), skill_manage.validate_ref(request.get("ref"))
        self._background("skills_check_updates", request.get("id"), lambda: self.imports.check_updates(url, ref))

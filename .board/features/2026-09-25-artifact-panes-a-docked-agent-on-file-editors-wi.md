---
id: PBZ4
type: work
status: planned
labels: [feature, panes, files, agent-ui, plugins]
rank: zzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (model row 2)'
links: {plans: [], commits: [], evidence: [], related: [P2W8, F8R7, E85D, C0Q8, WYGY], github: null}
---
# Artifact panes: a docked agent on file editors, with the plugin's actions and slash commands

## Issue
then there are artifact panes (perhaps that term is not user-facing), where the content is an editable object you are interacting with, rather than a console. a central one would be cards, which have a relay-specific structured format. but this is also more general, like scripts, jupyter notebooks, documents, charts, artwork, etc. in an artifact pane, you have the agent system prompt docked at the bottom, same as a console pane. it would have special tools in it, for example the planner we currently have for cards. it could also have commands that can be slashed or auto-detected, and there would be a plug-in spec defining that.

## Plan
Slice 7 of #P2W8 (model row 2; decisions D1, D2, U5). **Wave 2: waits for the hold on #F8R7/#E85D to lift** (`src/FilePanes.cpp` carries 722 uncommitted lines of the open-buffer path; `backend/relay_core/open_buffers.py` is untracked).

**Goal.** A file open in an editor pane (`FilePreview`, `PlanEditor`) gets the same docked agent a card page has: a Context that says what file and plugin it is about, the plugin's actions as the action row, its slash commands in the `/` popup, the cursor/selection/cell as `screen`; the agent's edits land in the buffer as undo steps (D1) with three-way merge on conflict (D2), and a per-turn change list.

**Findings.** `ContextSpec`/`Context` (`src/AgentContext.h:153-322`) fit this now; no `file`/`plugin` field is on the wire. `wireConsoleHost` (`src/RelayWindow.h:1180` and siblings) is the one template that adds a console to a `ToolPane`; `FilePreview` has none. A Context cannot add slash commands (`src/PaneRuntime.cpp:470-525` merges static, aliases, worker skills and the guest catalog). The agent's open-file patch path exists (`src/PaneEvents.cpp:145`, `open_buffers.py`).

**Steps.**
1. `relay::agent::ArtifactContext` in `src/ArtifactContext.{h,cpp}` (library `relay-agentcontext`): `spec()` with `name: "artifact"`, `surface: "file:<path>"`, `workspace`, `persistKey: <tab>/file:<path>`, `screen` = path, plugin id, cursor line, selection, dirty state; `actions()` from the plugin's `commands` (#6FDD) plus Save/Revert; `links` resolve `file:line`; `turnFinished` refreshes the change list. Wire `file` and `plugin` into `configure.context` (protocol section 33, additive).
2. `Context::slashCommands()` (default empty) merged into the `/` popup with a `✦ <plugin>` group; collisions go to the plugin's namespaced form (`/py:restart`).
3. `wireConsoleHost` for `ToolPane::Kind::Preview` (text/Markdown) and `Kind::Plan`; console built on first expand as Options does; the composer frame sits under the editor.
4. Edits: the agent's `edit_file` on the open path goes through `open_buffers` into the buffer as a marked undo step; the change list (turn → hunks) is a collapsible row over the composer; "review before apply" is a per-workspace option (D1).
5. Cards: none needed — `CardContext` already is this; #Y2BA lifts the page.
6. Docs: `docs/ARCHITECTURE.md` section 10 and "Agents are consoles"; protocol 33.

**Files.** `src/ArtifactContext.{h,cpp}` (new), `src/AgentContext.h` (slash hook), `src/FilePanes.{h,cpp}` (after the hold), `src/RelayWindow.h` (`wireConsoleHost` call sites), `src/PaneRuntime.cpp` (`/` popup merge), `backend/relay_core/agent_context.py` (fields), tests `tests/agentcontext_test.cpp`, `tests/filesync_test.cpp`.

**Verify.** Live: open `README.md`, ask the docked agent to add a sentence while typing in the buffer; the sentence appears as one undo step, the typing survives; `/` shows the Markdown plugin's actions; the change list names the turn.

## Tasks

- [ ] ArtifactContext and the file/plugin wire fields <!-- t:5j -->
- [ ] Context::slashCommands merged into the / popup <!-- t:ar blocked_by=#6FDD -->
- [ ] Docked console on Preview and Plan panes (after the hold lifts) <!-- t:m5 blocked_by=5j -->
- [ ] Agent edits as buffer undo steps with change list and review toggle <!-- t:j2 blocked_by=m5 -->
- [ ] Docs and live evidence <!-- t:ry blocked_by=ar,j2 -->

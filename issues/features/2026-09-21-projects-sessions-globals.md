---
id: P7SJ
type: work
status: needs-verification
labels: [feature, projects, sessions, switchboard]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex conversation, 2026-09-21'
links: {plans: [], commits: [237c37f0267e2a3328cf57b61fbbb33511a39494, 60ffc6b2250b, ece82b268bc9, 32721d28, a6319d5e, 7273f7b5, 6e5b2a33, be42269896bc3e0beecff69b28da487e40cfee63, f24b12a728eed750c8bf7031fb7655f162bf29da, 188b5931c9f7f60d3f96487c7cebfcd9a279ae28, 56d671cf5a914099f0a3d28c424e396e46ba681c], evidence: [docs/qa_evidence/2026-09-21-projects-sessions-globals/], related: [916B, TVE1, Y2MP], github: null}
---
# One pane for Projects, Sessions, and Globals

## Issue
Owner, initial request:

> rather than listing projects in agent options
>
> [Image #1]
>
> add them as a tab in the sessions manager
>
> actually, how about we change that to projects and sessions and move it to  ctrl shift p? or is a separate projects pane valuable? i think now we need a separate projects pane, ctrl shift p. how similar is this to the switchboard HQ idea i have a card about. analyze what we have and give me options

Owner, research request:

> before we decide this, can you research what warp, vscode, etc other systems / harnesses do for something like this to see what we can learn

Owner, after comparing separate panes, one pane with views, and a persistent sidebar:

> i am liking the middle option. can you include a "globals" tab that has our proposed "swithcboard hq" functionality

> and ctrl shift g can open globals

> i dont need the pane screenshot key, not sure why i have that

Owner followups:

> sessions doesnt work, there's nothing in there

> and the "recently closed" move to be a button in the sessions tab

## Decisions
- Owner: "fix it, remove that old project page and move ctrl shift s back to switchboard" — remove the legacy picker; Ctrl+Shift+S always opens the Switchboard for the tab/project or current directory, including its empty state.
- Owner: "lets go with subagents for efficiency" — proceed with implementation, dividing independent areas between subagents.
- Owner: "i am liking the middle option. can you include a \"globals\" tab that has our proposed \"swithcboard hq\" functionality" — develop the shared-pane option with three tabs: Projects, Sessions, Globals.
- Owner: "and ctrl shift g can open globals" — Ctrl+Shift+G selects Globals.
- Owner: "i dont need the pane screenshot key, not sure why i have that" — remove the default Ctrl+Shift+G binding from pane screenshot capture; no replacement screenshot shortcut needed.

## Discussion points
Owner authorized implementation with subagents: "lets go with subagents for efficiency".
The Globals tab is the home for the proposed HQ functionality in card #Y2MP. Implementation uses the bounded pinned/path-matched memory behavior in the plan below;
team distribution and a global loose-work inbox are outside this delivery.

## Planning notes
Proposed interaction:
- Ctrl+Shift+P opens the shared pane on Projects; Ctrl+Shift+Y opens Sessions;
  Ctrl+Shift+G opens Globals. If the pane is already open, select and focus the requested tab
  rather than create another instance. Preserve each tab's search and selection.
- Projects replaces the known-project management list in Options. Show pinned/recent projects,
  active sessions, and explicit actions for opening a project, attaching this tab, opening its
  Switchboard, and starting a session. Put discovery provenance in details and Forget in a
  secondary action. Browsing a project must not silently attach the current tab.
- Sessions keeps the existing conversation/terminal-history search and project filters, including
  work outside projects. Reuse the same session data for active-session rows in Projects.
- Globals hosts HQ's global memories and aliases, with global instructions visible through their
  existing source files. Provide search, creation/editing of supported global records, and clear
  scope/source information; show when project-specific content overrides a global entry.
  Keep project memories and work on their project's Switchboard. Global instruction files can be
  inspected/edited in place without migrating them into the HQ store.
- A Globals view must be backed by the memory loading and global-board work in card #Y2MP;
  an empty placeholder or a list of unused memory records does not deliver HQ functionality.
- Recommended boundary: Globals does not silently become a default inbox for loose work cards;
  retain card #916B's default-project/picker routing unless the owner changes that decision.
- The default keymap currently gives Ctrl+Shift+G to `agent.screenshotPane`; remove that default
  when adding Globals, keeping the action available in Actions. Ctrl+Shift+P currently belongs
  to Actions in the Warp and VS Code presets: resolve that preset collision explicitly during
  shortcut design. Add shortcut hints using live bindings.

Current reuse points: `src/Conversations.{h,cpp}` (`SessionManager`, tab support),
`src/ProjectPicker.{h,cpp}`, `src/Projects.{h,cpp}` (registry), `src/RelayWindow.h`
(pane hosting and Options project rows), and `src/Keymap.h`. Card #TVE1 tracks consistent
project identity for grouping. These are findings, not a finalized execution plan.

Research behind the proposal (official documentation reviewed 2026-09-21):
- [VS Code session management](https://code.visualstudio.com/docs/agents/run/sessions/manage-sessions): shared sessions across workspace-scoped Chat and the cross-workspace Agents window; the latter is Preview.
- [Zed parallel agents](https://zed.dev/docs/ai/parallel-agents): terminal and agent threads grouped beneath projects, with a separate history view.
- [Warp Drive](https://docs.warp.dev/knowledge-and-collaboration/warp-drive/ai-objects): reusable knowledge and global/project rules provide the closest HQ comparison.
- [Warp Tab Configs](https://docs.warp.dev/terminal/windows/tab-configs/): launching a saved working setup is distinct from resuming a conversation.
- [Claude Code sessions](https://code.claude.com/docs/en/sessions): searchable history can widen from the current repository to all projects.


## Plan
Session-loading followup: refresh the list when the Sessions tab is activated by mouse, preserving its query and filters. Move Recently closed under a button within Sessions. Verify with a saved session fixture and mouse tab navigation, plus targeted widget tests.

Followup: remove the legacy picker host, route project selection and held `/card` text through the new Projects tab, and make Ctrl+Shift+S open the existing Switchboard or a side-effect-free empty board for the current directory. Verify under isolated Xvfb from a loose folder.

**Goal:** one Projects / Sessions / Globals pane with direct shortcuts, usable project management,
and working global memories, aliases, and instruction-file access.

**Findings:** `src/Conversations.{h,cpp}` already hosts tabs; `src/RelayWindow.h` owns the registry,
worker routing, and Options rows. `backend/worker.py` dispatches protocol modules.
`backend/relay_core/aliases.py` has global storage; memory runtime consumption is missing.

**Steps:**
1. Add a tested HQ backend for global record list/read/save/retire and instruction-file access,
   and bounded memory loading for local/global context. Use pinned memories plus matching path
   globs, project precedence, and existing instruction locations. Leave team scope inactive.
2. Build Projects and Globals widgets behind callback interfaces, reuse SessionManager tab
   hosting and filters, and preserve state across tab switches.
3. Wire the three entry points, project actions and HQ requests in the window. Move registry
   management out of Options; preserve explicit project attachment and loose-card routing.
4. Give Projects Ctrl+Shift+P, Sessions Ctrl+Shift+Y, Globals Ctrl+Shift+G. Remove the screenshot
   default and resolve preset collisions, keeping Actions accessible. Add live shortcut hints.
5. Run targeted backend/widget tests and an isolated Xvfb integration check, then land through
   land.py with evidence and a manual QA checklist.

**Risks:** shared checkout has unrelated edits; each agent owns named paths and snapshots them
before editing. Global data must not become a fallback work-card inbox. The Globals page must
show real runtime-backed records. Settings/preset custom bindings must retain user intent.

**Verify:** targeted HQ/memory protocol tests, conversations/projects widget tests, keymap checks,
application build, and isolated GUI screenshots showing tab selection and CRUD persistence.

## Tasks
- [x] HQ storage/protocol and global/project memory consumption <!-- t:bk -->
- [x] Projects management and shared SessionManager tabs <!-- t:pj -->
- [x] Globals editor and record management <!-- t:g1 -->
- [x] Window integration, shortcuts and hints <!-- t:w1 -->
- [x] Targeted tests, GUI evidence and landing <!-- t:qa -->

## Execution Summary
Session followup `56d671cf`: selecting Sessions now refreshes its saved query and filters, fixing the blank list after mouse entry from Projects. Recently closed is a button inside Sessions with Back to sessions; the top-level manager has Projects, Sessions and Globals. A saved fixture visibly loads with its preview in isolated GUI verification.

Followup `188b5931`: removed the legacy project picker. Ctrl+Shift+S opens Switchboard directly, including an empty board for a loose directory; the helper receives explicit uninitialized state. Project selection and pending `/card` text use the new Projects tab. Opening the board writes no board/git files.

One shared Projects / Sessions / Globals pane is implemented. Projects owns registry management,
active sessions, pin/forget, declined entries, explicit open/attach/Switchboard actions and session
filter links. Options now links to Projects. Ctrl+Shift+P/Y/G select the corresponding tab;
screenshot capture is unbound, and Warp/VS Code Actions use Ctrl+Shift+A to avoid collisions.
Existing custom overrides remain honored. The pane helper receives the selected tab's context.

Globals provides search, Markdown editing, creation and retirement of memories/aliases, and
in-place editing of known instruction sources. Runtime memories are consumed, capped, matched
and overridden; global writes preserve existing alias identities and reject stale edits.
Evidence: [GUI screenshots, driver and notes](../../docs/qa_evidence/2026-09-21-projects-sessions-globals/).

## Tests
- `tests/test_board_protocol.py::InitTests`
- manual: docs/qa_evidence/2026-09-21-projects-sessions-globals/07-switchboard-direct.png
- `ctest -R conversations` — tests/conversations_test.cpp
- `ctest -R projectspane` — tests/projectspane_test.cpp
- `ctest -R globalspane` — tests/globalspane_test.cpp
- `tests/test_keybindings.py`
- `tests/test_globals_protocol.py`
- `tests/test_memories.py`
- `tests/test_agent_context.py`
- `tests/test_remote_wire.py`
- manual: docs/qa_evidence/2026-09-21-projects-sessions-globals/

### Check 2026-09-23 15:03
- passed · unittest:tests.test_board_protocol.InitTests — tests/test_board_protocol.py::InitTests passed for this revision on spark-dcc9, 2026-09-23T19:03:05Z
- not-applicable · manual:docs/qa_evidence/2026-09-21-projects-sessions-globals/07-switchboard-direct.png — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-projects-sessions-globals/07-switchboard-direct.png
- missing-evidence · ctest:conversations — no run of ctest -R conversations for this revision, from any host, and no attached result
- missing-evidence · ctest:projectspane — no run of ctest -R projectspane for this revision, from any host, and no attached result
- missing-evidence · ctest:globalspane — no run of ctest -R globalspane for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_keybindings — no run of tests/test_keybindings.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_globals_protocol — no run of tests/test_globals_protocol.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_memories — no run of tests/test_memories.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_agent_context — no run of tests/test_agent_context.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_remote_wire — no run of tests/test_remote_wire.py for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-21-projects-sessions-globals/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-projects-sessions-globals/
- notice · ctest:conversations — ctest -R conversations is slow: p95 3.18 s, p50 2.91 s
- notice · unittest:tests.test_keybindings — tests/test_keybindings.py: 7 of 32 never ran here (test_qwas_defaults, test_plain_ctrl_is_left_to_the_editor_for_asdzxcp, test_f1_is_not_bound…)
- notice · unittest:tests.test_globals_protocol — tests/test_globals_protocol.py: 1 of 11 never ran here (test_suggestions_have_their_own_list_accept_and_reject)
- notice · unittest:tests.test_agent_context — tests/test_agent_context.py: 1 of 24 never ran here (test_the_models_pane_has_a_brief_of_its_own)
- notice · unittest:tests.test_remote_wire — tests/test_remote_wire.py: 48 of 48 never ran here (test_every_worker_event_is_classified, test_the_guest_channel_names_are_not_worker_events, test_no_event_is_both_forwarded_and_withheld…)
- notice · unittest:tests.test_remote_wire — tests/test_remote_wire.py: 48 of 48 were edited since their history began, so it starts over (test_every_worker_event_is_classified, test_the_guest_channel_names_are_not_worker_events, test_no_event_is_both_forwarded_and_withheld…)
- notice · unittest:tests.test_remote_wire — tests/test_remote_wire.py: 9 of 48 are skipped for good (test_the_client_resumes_under_the_hubs_own_stream_names, test_a_plain_link_parses, test_percent_encoded_separators_still_parse…)
history: thread
## QA checklist
- [ ] Open Projects, click Sessions: existing sessions load without typing or using a shortcut.
- [ ] Recently closed opens from the Sessions button; Back returns to the same list and filters.
- [ ] Ctrl+Shift+S opens Switchboard directly from a loose folder; Ctrl+Shift+P opens the new Projects tab.
- [ ] P/Y/G shortcuts select Projects/Sessions/Globals without duplicating the pane.
- [ ] Project browsing preserves the current attachment; explicit open/attach/Switchboard actions work.
- [ ] Pin, Forget and declined-project restoration preserve project files.
- [ ] Sessions retains its search/filter behavior; project links and No project filter correctly.
- [ ] A saved global memory loads into a later agent prompt; project overrides take precedence.
- [ ] Globals preserves an unsaved draft across tab switches and refuses stale external edits.
- [ ] Screenshot has no default key; slow paths show live shortcut hints.

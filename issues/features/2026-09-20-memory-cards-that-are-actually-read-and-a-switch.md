---
id: Y2MP
type: work
status: needs-verification
labels: [feature, switchboard, agent]
assignee: codex
rank: m
created: '2026-09-20'
source: 'conversation, 2026-09-20'
links: {plans: [], commits: [237c37f0267e2a3328cf57b61fbbb33511a39494, 60ffc6b2250b, ece82b268bc9, 32721d28, a6319d5e, 7273f7b5, 6e5b2a33, be42269896bc3e0beecff69b28da487e40cfee63, f24b12a728eed750c8bf7031fb7655f162bf29da], evidence: [docs/qa_evidence/2026-09-21-projects-sessions-globals/], related: [VQ8T, X7NB], github: null}
---
# Memory cards that are actually read, and a Switchboard HQ for what is global

## Issue
Owner, 2026-09-20:

> afterward, i want to check if the memories / aliases / plans functionality for switchboards is
> imeplemnted, and also whether we need to set up a "switchboard HQ" for globals:
> memories, aliases, system prompts, etc

and, after the audit: **"file the initial plan for memories and switchboard HQ in discussing."**

## Decisions
- Owner: "lets go with subagents for efficiency" — implement HQ within the Globals tab in card #P7SJ.

## What the audit found

| type | format | agent tools | user can create | read at runtime |
|---|---|---|---|---|
| alias | yes | partly broken (#W3KD) | yes, outside the board | **yes, fully** |
| memory | yes | generic only | no | **no** |
| plan | yes | generic only | no | **no** — being dropped, #X7NB |

**Memory cards are a format with no consumer.** `MEMORY_FIELDS = ("name", "description", "kind",
"topic", "scope", "paths", "pinned", "supersedes", "reviewed", "author")` (`board.py:217-218`) are
parsed, validated and round-tripped, and **nothing reads any of them**. Specifically:

- `Agent.system_prompt` (`agent.py:728-739`) assembles SYSTEM + workspace + skills + instructions +
  todos + board rules + app rules + own rules + plan. No memory anywhere in it.
- The board rules it injects are `board_policy.md`, 54 lines, which contains **zero** occurrences of
  "memory", "alias" or "plan card". An agent is never told memory cards exist.
- `BoardPane.cpp` contains zero occurrences of "memory". There is no view, no editor, no topic
  grouping. `Card::topic` is parsed in `BoardModel.cpp:936` and read nowhere.
- `board.yaml` carries `memory: {autonomy: auto}` (`board.py:985`). Nothing reads that key.
- Quick-add hard-codes `card_type: "work"` (`BoardPane.cpp:3761`), so a person cannot make one, and
  `board_list` hides non-work cards by default (`board_tools.py:1682-1683`), so an agent will not
  see one.
- The board pane still labels the section *"in use — loaded when it applies"*
  (`BoardModel.cpp:161`), a promise nothing keeps.

**Global state today is exactly one thing: global aliases.** `aliases.global_root()`
(`aliases.py:444-450`) resolves `$RELAY_GLOBAL_SWITCHBOARD` → `$XDG_CONFIG_HOME/relay/switchboard`
→ `~/.config/relay/switchboard`. That directory **does not exist on this machine**; `~/.config/relay`
holds only `email.json` and `local-models.json`. `SCOPES = ("local", "global")` (`aliases.py:60`),
local shadows global, and the shadowed one stays listed.

There is **no global board**: `find_board_root` (`board_tools.py:989-1014`) walks up from the
workspace with no user-level fallback, and `scripts/relay-board.py` never looks at `~/.config/relay`.
The global root gets an `aliases/` folder and no `board.yaml`.

**System prompts are already solved, separately, and already work.** `instructions.py` reads project
convention files (`AGENTS.md`, `CLAUDE.md`, `WARP.md`, and a dozen other tools') from the git root
down, and global ones from `~/.config/relay/relay.md`, `~/.claude/CLAUDE.md`, `~/.warp/WARP.md`,
`~/.codex/AGENTS*.md` and others. Both are injected into the system prompt. The synthesis target is
`$XDG_CONFIG_HOME/relay/relay.md` — a **sibling** of `relay/switchboard/`, not inside it.

The intent was already written down: `docs/TASKS-AND-MEMORY-DESIGN.md:419-425` (decision 6 —
guidance, global skills, aliases, memory, MCP config, "same UI for both, switchable") and `:448-452`
(memory `scope: project | team | user`). The `scope` field exists on memory cards and nothing reads
it.

## Plan

Proposed order, to discuss before executing. **Memory before HQ**, because a global memory that
nothing reads is the same nothing as a project memory that nothing reads.

### Stage 1 — make memory cards work, per project

1. **Tell the agent they exist.** A paragraph in `board_policy.md` saying what a memory card is,
   when to write one, and that one fact per card is the shape. Without this the tools are unreachable
   in practice.
2. **Inject the matching ones.** A `memory` section in `Agent.system_prompt`, beside
   `instructions.section`: the `pinned` ones always, plus any whose `paths` globs match the
   workspace. Budgeted like `instructions.py` does it, with a byte cap and a named header so the
   model knows it is lower-priority context.
3. **A way to write one.** `board_create_card {type: "memory"}` already works; the gap is the GUI.
   Give quick-add a type, or a "Remember this" action that files a memory card from the pane.
4. **A way to see one.** The Memory tab already synthesises (`BoardModel.cpp:1016-1030`); it needs a
   view that is a list of facts rather than a work queue, and honours `supersedes`.
5. **Make the label true.** Either implement "loaded when it applies" or change the section text.

### Stage 2 — Switchboard HQ

Cheap once stage 1 works, because the scope field and the directory already exist.

6. Give `$XDG_CONFIG_HOME/relay/switchboard` a `board.yaml`, so it is a board and not just an
   aliases folder.
7. A user-level fallback in `find_board_root` and in `scripts/relay-board.py`.
8. Read `scope` on memory cards; resolve global under project the way aliases already do, shadowing
   included.
9. One switchable UI over the two scopes, per design decision 6.

### Not in scope
System prompts stay where they are. HQ cannot own `~/.claude/CLAUDE.md` or `~/.warp/WARP.md`, so
absorbing `relay.md` into it would buy a move and no capability. Alias gaps are #W3KD and the two
unreachable backend paths (`alias_delete`, `suggest {kind: "alias"}`), which are their own cards.

## Questions
On the card, numbered, with a recommendation each — see the thread.

## Execution Summary
HQ is the Globals tab of the shared manager (card #P7SJ), opened with Ctrl+Shift+G. Global memory
and alias cards can be searched, created, edited and retired; existing instruction sources are
visible and editable in place. The store gains board.yaml on its first card write.

Memory cards now enter full and short agent prompts when pinned or matching the workspace,
with a byte budget, project precedence, applicable supersession and inactive team scope.
Board policy explains memory creation. Scoped project memories remain on project Switchboards,
authored using existing board tools; global memory creation has the new Globals editor.
The initial exploratory global work-card fallback was superseded by card #P7SJ's routing boundary:
loose work retains the explicit project picker/default-project behavior.
Evidence: [GUI screenshots, driver and notes](../../docs/qa_evidence/2026-09-21-projects-sessions-globals/).

## Tests
- `tests/test_globals_protocol.py`
- `tests/test_memories.py`
- `tests/test_aliases.py`
- `tests/test_system_prompt.py`
- `tests/test_prompt_profiles.py`
- `ctest -R globalspane` — tests/globalspane_test.cpp
- manual: docs/qa_evidence/2026-09-21-projects-sessions-globals/

## QA checklist
- [ ] Create/edit/retire a global memory and confirm later prompts reflect the saved state.
- [ ] Pinned/path matching, project shadowing and scoped supersession choose expected memories.
- [ ] Aliases created in Globals can be edited through the existing alias UI without duplicate files.
- [ ] Instruction edits stay in the displayed source file and use existing loading settings.
- [ ] Global content never silently becomes the inbox for loose project work.

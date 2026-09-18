---
id: 1AXN
type: work
status: needs-qa-llm
labels: [switchboard]
component: [worker, gui]
milestone: desktop-alpha
workstream: switchboard
assignee: Claude Opus 5 (1M context), 2026-09-17
implemented_by: Claude Opus 5 (1M context)
rank: zzzi
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-switchboard-phase1/` (Xvfb pass on a copy of the real 86-card tracker with a live model: the pane opens on Ctrl+Shift+S, an agent turn creates, moves and comments on a card through the tools, the pane follows by itself, and the Switchboard agent answers on the card); 131 new tests (78 + 37 Python, 16 C++), suite at 623 Python tests and 17 ctest groups'
source: '`docs/SWITCHBOARD-DESIGN.md` sections 4-6 and 9.1 with the owner decisions in 12 (especially 12.3 and 12.5); `docs/TASKS-AND-MEMORY-DESIGN.md` section 9 (plans and memories are card types)'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-17-switchboard-phase1/], related: [23XM], github: null}
---
# Switchboard phase 1: agent tools and policy, the protocol, the pane, `#` references

## What landed

Phase 1 of `docs/SWITCHBOARD-DESIGN.md`, on top of phase 0's format (`#23XM`).

**Agent tools and policy** (`backend/relay_core/board_tools.py`, `board_policy.md`)

The six tools of design 6.1 — `board_list`, `board_read`, `board_create_card`,
`board_update_card`, `board_move_card`, `board_comment` — with the guardrails of 6.3 and the
owner's decision 12.3. They are added to a pane agent's tool list, with the policy block in the
system prompt, whenever the workspace has an `issues/board.yaml` and autonomy is not `off`. Plan
mode keeps the two read tools and drops the four writes.

- No delete tool; a card closes by moving to `done`/`dropped` with a reason.
- `id`, `type`, `created`, `source`, `rank`, `status` and `private` are never writable through
  `board_update_card`.
- **Owner text may be rewritten** (12.3, superseding 6.3's refusal): replacing `## Request` or the
  title appends a `rewrite` thread entry holding the old *and* the new text.
- Every write appends a thread entry with actor, model, pane and `session/turn`.
- Writes are atomic and hash-checked; a stale `base_hash` returns `board_conflict` with the
  current hash and overwrites nothing.
- Limits: 5 creates and 20 other writes per turn, 30 creates per hour per workspace (shared
  between panes through `<workspace>/.relay/board-rate.json` under `flock`); over them the tool
  returns `board_rate_limited` and the policy tells the agent to summarize in its reply.
- A fuzzy duplicate check on create, overridable with `not_duplicate_of`.
- Into a QA lane needs `evidence` and `implemented_by`; out of one to `done` needs a verdict
  section and a **different model family** from the implementer.
- A `decision` comment must quote the user verbatim.
- Every write records an undo snapshot (file bytes plus the thread length), restorable through
  `board_undo`; undoing a creation removes the file only when git has never seen it.

**Protocol** — a new section 17 in `docs/AGENT-SESSIONS-PROTOCOL.md`, implemented in
`backend/relay_core/board_protocol.py`: `board_open`, `board_refresh`, `board_card_get`,
`board_check`, `board_create`, `board_update`, `board_move`, `board_comment`, `board_undo` and
`board_ask`, plus `board_activity` on every write, `configure`'s `board {dir, autonomy, limits}`
block and `ask {cards: […]}`. The GUI never parses a card. `board_ask` is answered by the
**Switchboard agent**: a worker per window on the `switchboard` model role (protocol 13,
defaulting to the main agent), stateless per card — the first question seeds the conversation from
the card and its thread tail, and any edit to the card reseeds it.

**The pane** — `src/BoardModel.{h,cpp}` (pure logic), `src/BoardPane.{h,cpp}` (`relay::BoardView`,
a `ToolPane` leaf) and `src/BoardWorker.{h,cpp}` (the per-window worker). Ctrl+Shift+S
(`board.open`), the palette entry with aliases, `/switchboard`, tabs with counts, columns from
`board.yaml` with drag and drop, quick add, keyboard navigation, the filter language, a problems
banner, and a card detail view with the rendered body, the `## Tasks` list, the links, the thread
and a reply box that either asks the agent or appends a comment. A `QFileSystemWatcher` on
`issues/` turns every write — from this window, a pane agent, an editor or a `git pull` — into one
debounced `board_refresh`. Plans and memories are card types with their own tabs: the Plans tab
has draft → approved → executing → done, and the Memory tab is one list per topic.

**From the terminal** — `#` and a few characters opens a card picker in agent or auto mode (in
terminal mode `#` stays a Bash comment), a resolved `#K7Q2` travels with the prompt as
`ask {cards: […]}`, `/card <text>` captures a card verbatim without opening anything, and every
agent card write prints one line plus a toast in the pane that caused it.

## Also in this change

- `board.append_thread` now picks the next entry id **after the last one on disk, under the
  lock**, so two writes in the same second stay ordered and `relay-board.py check` stays clean.
  (Without this the Switchboard's own writes made the tracker fail `check` within one turn.)
- `RichEditor::setPlaceholders` so the card reply box hints at its own job.
- `attachments.format_block` takes a `label`, so a card block says it is a card rather than a file
  the user picked with `@`.
- `src/main.cpp`: one line put back to `dynamic_cast<ChromeButton *>` — the base commit
  (`9191fa8`) does not compile without it, and current `main` already carries the same fix.

## Not in this change (phase 2 and later)

- `board_scan` / `board_convert` / `board_cleanup_sources`: no detection or conversion of
  non-compliant notes, intake files or `TODO.md` (design 7).
- `autonomy: suggest` is accepted and stated in the prompt, but writes still apply directly; there
  is no proposal-acceptance UI.
- The `## Tasks` checklist is **read-only in the pane**: ticking a box says so and re-reads the
  card. The tool can already write the list (`board_update_card {tasks}`).
- The Undo toast: `board_undo` works over the protocol and is covered by tests, but the pane has
  no Undo button yet.
- No label, assignee or `waiting_on` editors in the detail view (status and tab pickers only).
- No eval harness (`tests/board_evals/`, design 6.4).
- No `relay board` shell command, no Board button in the tab-bar row, no unread dots.
- The repository's own `issues/` tree is still on the pre-board `open`/`ready` mix that phase 0
  migrated; nothing here changes it.

## Implementer check (2026-09-17)

- `./scripts/test.sh` — 623 tests, all passing.
- `ctest --test-dir build` — 17/17 groups, including the new `board` group.
- `cmake --build build` — no new warnings.
- `python3 scripts/relay-board.py check` — 86 cards, 0 errors, 0 warnings.
- Live pass under Xvfb with `deepseek/deepseek-v4.1-flash` through OpenRouter against a copy of
  the real 86-card tracker: `docs/qa_evidence/2026-09-17-switchboard-phase1/`.

## QA checklist

Run against a **copy** of a repository with `issues/board.yaml` (never the real tracker), under
Xvfb with an isolated `XDG_CONFIG_HOME`. Capture the window id, not the root window.

### The tools and their guardrails (`tests/test_board_tools.py`, plus a live model)

1. In a pane agent, ask for three unrelated things in one prompt. Three cards, each with the
   user's words verbatim in `## Request`, and the reply names them as `#ID`.
2. Ask for something the tracker already covers. The agent calls `board_list` first and updates
   the existing card, or the create is refused with `board_possible_duplicate` and it reads the
   candidate before overriding with `not_duplicate_of`.
3. Ask a question the agent cannot answer. It becomes a `question` comment — numbered, with a
   recommendation — the card goes to `discussing` with `waiting_on: owner`, and the terminal reply
   names the card instead of burying the question.
4. Make a decision in the terminal ("use the cloud one"). A `decision` comment quotes you
   verbatim. Verify a decision comment with no quotation marks is refused.
5. Ask the agent to move a landed card to `needs-qa-llm` without evidence: refused. With evidence
   but no `implemented_by`: refused. With both: it moves, the file lands in `needs_qa_llm/`, and
   the evidence path is in `links.evidence`.
6. Ask the *same model family* that implemented a QA-lane card to close it: refused
   (`independent_model`). Add a `## Verdict` section and try from a different provider: allowed.
7. Ask the agent to change a card's `id`, `created` or `source`: each refused by name.
8. Ask it to rewrite a card's `## Request`. It is allowed, and the thread gains a `rewrite` entry
   holding **both** the old and the new text. Confirm you can restore the old text from it.
9. Ask for more than five cards in one turn: the sixth returns `board_rate_limited` and the agent
   summarizes the rest in chat rather than retrying.
10. Set `agent: {autonomy: off}` in `board.yaml`, restart the pane: the `board_*` tools and the
    policy block are gone from the prompt and no card can be written.
11. Confirm no tool can delete a card, and that `scripts/relay-board.py check` is clean after
    every one of the steps above.

### The pane

12. **Ctrl+Shift+S** in a terminal pane opens the Switchboard split right; pressing it again on
    the Switchboard returns to the terminal pane; in another tab it opens its own. Check the
    palette entry ("Switchboard", searchable as board/issues/cards/kanban) and `/switchboard`.
13. Tabs carry counts; Ctrl+PgUp/PgDn switch them; a deferred card appears only under Deferred and
    a done card only under Done (newest first).
14. Drag a card between two columns: the file moves into the matching state folder, the status
    changes, and the thread records the move. Drag it between two cards in a column: only that
    card's `rank` changes.
15. `n` (and `+`) opens the quick-add field; the text is stored **verbatim** as `## Request` and
    the title is its first line. Arrows select, Enter opens, Esc closes, `m` moves, `/` filters,
    `y` copies `#ID`, `t` inserts `#ID` in the composer, `o` opens the card file, and
    Alt+Shift+arrows move a card.
16. Filter with `label:x`, `status:ready`, `@agent`, `waiting:me` and free text, alone and
    combined. Counts follow the filter.
17. Open a card: the body renders, `## Tasks` are listed, links and front matter show in the meta
    line, and the thread is in order with authors and models. Reply with **Comment only** (no
    model call) and with **Ask the agent** (the answer streams in and is appended to the thread).
18. Edit a card file in an editor, or let an agent in another pane write one. The pane picks it up
    within about a second without being touched.
19. Open a Plans tab card and a Memory tab card: plans have draft/approved/executing/done, memory
    is grouped by topic, and neither appears among the work cards.
20. Close Relay with a Switchboard open and restart: the pane comes back on the same tab. Open it
    in a workspace **without** `issues/board.yaml`: it says so and opens nothing.
21. Pull a branch that changed several cards: the columns follow, and a card with a merge conflict
    shows in the problems banner rather than silently vanishing.

### From the terminal

22. Type `#` then a letter in agent or auto mode: the picker lists cards (open ones first), Enter
    inserts `#ID `. In terminal mode `#` does nothing (it is a Bash comment).
23. Send a prompt containing `#K7Q2`: the agent has the card body, its open tasks and the recent
    thread, and posts progress back with `board_comment`.
24. `/card some note`: a card appears in the Inbox with the text verbatim, without opening a pane,
    and an inline line names it.
25. After any agent card write, check the inline line and toast appear **in the pane that caused
    it** and nowhere else.

### Hints and keys (WARP.md's standing rule)

26. Opening the Switchboard from the palette, clicking `+`, dragging a card, and typing
    `/switchboard` or `/card` each hint the faster path at most three times, using the live Keymap
    text, and nothing hints when "Shortcut hints" is off.
27. `?` in an empty composer lists `#` and Ctrl+Shift+S; Ctrl+? lists `board.open`; Ctrl+Shift+S is
    free in all four keybinding presets and reaches Relay from inside a full-screen program under
    the default `program_keys` policy.

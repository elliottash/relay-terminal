---
id: MEMS
type: work
status: needs-verification
labels: [feature, memory, globals, guests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: bb89ea17-0ded-4516-bfb9-de4165ae9d5a
rank: mumrz
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22 (follow-up to #M7RY)'
links: {plans: [], commits: [], evidence: [], related: [M7RY, 1V4F], github: null}
---
# Learned user memory as confirmed suggestions, remembered rejections, and Claude/Codex import and replace

## Issue
explain the no auto learning decision. dont codex and claude learn on auto?

yes, new memories are suggestions that the user confirms. and if the user rejects, rejections are remembered so that, new potentialy memories will be declined if they trigger that same suggestion again. 

on startup, relay can get the memories from claude and codex and import them. 

can we give the users a way to replace codex / claude memories with relay memories?

## Decisions
- 2026-09-22, owner: "new memories are suggestions that the user confirms." Agents propose user facts as they learn them; nothing lands in user memory without a confirm.
- 2026-09-22, owner: "if the user rejects, rejections are remembered so that, new potentialy memories will be declined if they trigger that same suggestion again."
- 2026-09-22, owner: "on startup, relay can get the memories from claude and codex and import them."
- 2026-09-22, owner: "yes to 1-4. guests use relay memory, and thats the default." Relay-launched claude/codex default to Relay memory: their own memory off, Relay user memory injected, their suggestions go through Relay's confirm.

## Discussion points
- **Replace guest memory with Relay memory (asked by the owner).** Feasible per launch, without touching the user's own `~/.claude` / `~/.codex` config: Relay already starts `claude --settings <file> --append-system-prompt …` (`backend/relay_core/guest_launch.py`, `guest_harness_claude.py`) and `codex -c key=value …` (`guest_harness_codex.py`). The installed CLIs have the switches: Claude `autoMemoryEnabled` / `CLAUDE_CODE_DISABLE_AUTO_MEMORY`; Codex `memories.generate_memories` and a use switch under `memories.*` (seen in the 2026-09-22 binaries; exact keys to be confirmed at implementation). "Replace" = guest's own memory off for Relay-launched sessions + Relay user memory injected into its instructions + guest suggestions routed to Relay's confirm flow. Question: one global setting "Guests use: their own memory / Relay memory / both", default?

## Plan
**Goal:** Relay learns about its user the way Claude Code does, but every new fact is a suggestion the user confirms; rejections stick; existing Claude/Codex memories are imported once; Relay-launched guests default to Relay memory. **Parts A–D are implemented; what remains is build, targeted verification, commit and landing.**

**Findings (verified in the tree, 2026-09-24):**
- *Part A (landed, b01bf00d + a61b4716):* `backend/relay_core/memory_suggestions.py` (suggest/pending/rejected/accept/reject/rejection_digest), `globals_protocol.py` wire requests `globals_suggestions` / `globals_suggestion_accept` / `globals_suggestion_reject`, `app_tools.py` `app_user_memory suggest` + `suggestions`, `board.py` suggested/rejected statuses.
- *Part B (landed, 861123ea):* `memory_import.py` imports `~/.claude/CLAUDE.md`, `~/.claude/projects/*/memory` user+feedback files, `~/.codex/memories/memory_summary.md` and `~/.codex/AGENTS.md` as suggestions; ledger `memory/imported.json`; runs once on worker first configure; `RELAY_MEMORY_IMPORT=off` killswitch.
- *Part C (landed, 28e74f5e + d2178b6f):* option `guests/memory` (relay default | own | both); Claude flags `autoMemoryEnabled`/`autoDreamEnabled` false + `CLAUDE_CODE_DISABLE_AUTO_MEMORY=1`, Relay memory via `--append-system-prompt`; Codex `-c features.memories=false, memories.generate_memories=false, memories.use_memories=false`, Relay memory via developer_instructions; guests suggest via `app_user_memory` / `guest_launch suggest-memory`.
- *Part D (in the working tree; the part-C thread notes the Pane.h/RelayWindow.h hunks were not compiled yet):* `src/GlobalsPane.{h,cpp}` Suggestions section with Keep/Edit/Reject buttons, rejected-list toggle, `showSuggestion()`; `src/Pane.h` (~9400) prints "✦ Remember: <fact> · Keep · Edit · No" inline via `relay://memory/<pane>/<keep|edit|no>/<id>` links; `src/Pane.h` (~9472) and `src/RelayWindow.h` (~4713) turn `memory_import` worker events into a notification with a Review action (`memory.review` → `openMemorySuggestion`, wired in `RelayWindowCore.cpp` ~1003/1074); `src/RelayWindowSettings.cpp` (~943) Privacy toggle "Offer Claude Code and Codex memories" (`memory/import_guests`, default true); `src/PaneEvents.cpp` (~43) sends that toggle to the worker.

**Steps:**
1. `git log --oneline -15` and `git status` — see exactly which part-D paths are uncommitted, and whether another session is mid-edit in the shared big headers. Claim every path you will touch with `python3 scripts/land.py begin <me> <paths>` before editing (CLAUDE.md procedure; this checkout commits to `main` only through `scripts/land.py`).
2. Build through `scripts/relay-build` so all part-D hunks are actually compiled (the part-C thread explicitly warns they were not).
3. Run the targeted tests, not the full suites: the backend memory unittests (`pytest backend/tests -k 'memory'` — covers test_guest_memory.py, suggestions and import suites) and `ctest --test-dir build -R ^globalspane$` for the Suggestions list.
4. Live drive in isolation: an agent calls `app_user_memory suggest` → the "✦ Remember: …" line appears in the pane and the suggestion shows in Globals › Suggestions; Keep lands it in user memory; reject it, suggest the same fact again → declined against the rejection; toggle the import option off → no `memory_import` event on the next worker start.
5. Commit the remaining part-D paths with `python3 scripts/land.py commit <me> -m "…"` (dry-run first; the big headers may be contested — follow the confirm/exclude-hunk flow), then move the card to `needs-verification` with an evidence path.

**Risks:** shared checkout — `src/Pane.h` and `src/RelayWindow.h` are multi-thousand-line headers other sessions edit; expect contested hunks and do not land another session's work. The `relay://memory/…` link handler must route in a guest terminal pane as well as an agent console (step 4 covers the console; check a terminal pane if the handler differs). No open owner questions remain — the guest-memory default was decided ("guests use relay memory, and thats the default").

**Verify:** steps 3–4 are the verification. Failure shows as in `## Done means`: a fact in user memory with no Keep, a rejected fact offered twice, an import re-offered on restart, or a guest memory file changing during a Relay-launched session.

## Done means
- A fact an agent learns shows up as a suggestion (the "✦ Remember: … Keep · Edit · No" line and Globals › Suggestions), and lands in user memory only after Keep.
- A rejected suggestion never reappears when the same fact is suggested again, from any agent or from an import.
- On first start with existing Claude/Codex memories, the user gets one review list of importable facts; a second start offers nothing already seen.
- With "Relay memory" selected (the default), a Relay-launched claude/codex writes nothing to its own memory store and sees the Relay user memory.
- Failure shows as a fact in user memory with no Keep, a rejected fact offered twice, an import offered again on restart, or a guest memory file changing during a Relay-launched session.

---
id: MEMS
type: work
status: discussing
labels: [feature, memory, globals, guests]
assignee: claude-code
waiting_on: owner
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

## Discussion points
- **Replace guest memory with Relay memory (asked by the owner).** Feasible per launch, without touching the user's own `~/.claude` / `~/.codex` config: Relay already starts `claude --settings <file> --append-system-prompt …` (`backend/relay_core/guest_launch.py`, `guest_harness_claude.py`) and `codex -c key=value …` (`guest_harness_codex.py`). The installed CLIs have the switches: Claude `autoMemoryEnabled` / `CLAUDE_CODE_DISABLE_AUTO_MEMORY`; Codex `memories.generate_memories` and a use switch under `memories.*` (seen in the 2026-09-22 binaries; exact keys to be confirmed at implementation). "Replace" = guest's own memory off for Relay-launched sessions + Relay user memory injected into its instructions + guest suggestions routed to Relay's confirm flow. Question: one global setting "Guests use: their own memory / Relay memory / both", default?

## Plan
**Goal:** Relay learns about its user the way Claude Code does, but every new fact is a suggestion the user confirms; rejections stick; existing Claude/Codex memories are imported; guests can be switched to Relay's memory.

**Findings:** store and editor from #M7RY — `backend/relay_core/globals_protocol.py`, `memories.py`, `app_tools.py` (`app_user_memory`), `src/GlobalsPane.cpp`, Globals helper in `agent_context.py`. Guest launch in `guest_launch.py`, `guest_harness_claude.py`, `guest_harness_codex.py`. Claude store: `~/.claude/CLAUDE.md` and `~/.claude/projects/*/memory/*.md` (front matter `type: user|feedback|project|reference`). Codex store: `~/.codex/memories/`, `~/.codex/AGENTS.md`.

**Steps:**
1. Suggestion record: `app_user_memory suggest` writes a pending candidate (text, normalised key, source pane/turn); no user-memory card until confirmed.
2. Confirm UI: a compact "Relay wants to remember: … [Keep] [Edit] [No]" line in the pane transcript, plus a Suggestions list in Globals › User memory.
3. Rejection store: a rejected candidate is kept (retired-style record, never loaded into context). `suggest` compares new candidates against rejections (normalised text + same key; model-side check against the rejection list given in the tool result) and declines matches silently, reporting "declined: matches rejection from <date>".
4. Agent guidance: all Relay agents (not only the Globals helper) propose a user fact when they learn a durable one; one line in the system prompt, per [[agent-acts-unasked-in-terminal]] style.
5. Import at startup: read Claude `type: user`/`feedback` memories and `~/.claude/CLAUDE.md`, and Codex memories / `~/.codex/AGENTS.md`; new, unseen items become *suggestions* (source = "claude"/"codex"), deduped against existing memory and rejections; remember what was imported (path + hash) so restarts do not re-offer.
6. Replace switch (pending the question above): per-launch flags turning off the guest's own memory, and Relay user memory appended to the guest's instructions.

**Risks:** import floods the user with suggestions (batch them into one review list, not one prompt each); project-scoped Claude memories wrongly promoted to user scope (only `user`/`feedback` types, and they stay suggestions); CLI flag names drift between versions (probe the installed CLI, fall back to "both").

**Verify:** unit tests for suggest/confirm/reject/decline-by-rejection and for import dedupe/idempotence against fixture `~/.claude` and `~/.codex` trees; argv tests that the replace setting adds the flags; `ctest -R ^globalspane$` for the Suggestions list; one isolated live drive: an agent suggests, owner rejects, same fact is declined next time.

## Done means
- A fact an agent learns shows up as a suggestion, and lands in user memory only after Keep.
- A rejected suggestion never reappears when the same fact is suggested again, from any agent or from an import.
- On first start with existing Claude/Codex memories, the user gets one review list of importable facts; a second start offers nothing already seen.
- With "Relay memory" selected, a Relay-launched claude/codex writes nothing to its own memory store and sees the Relay user memory.
- Failure shows as a fact in user memory with no Keep, a rejected fact offered twice, or a guest memory file changing during a Relay-launched session.

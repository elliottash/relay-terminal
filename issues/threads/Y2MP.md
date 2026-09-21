<!-- relay:entry 20260920T161118Z-e1 author=claude-code kind=question -->
### Claude Code · 2026-09-20 16:11
Five decisions before this is executable. Recommendation on each.

**1. Memory first, or HQ first?**
*Recommend memory first (stages 1 then 2 as filed).* A global memory nothing reads is the same
nothing as a project memory nothing reads, and stage 2 is cheap once stage 1 works because `scope`
and the directory already exist. The alternative is building HQ first so aliases and memory land in
one shape, at the cost of shipping nothing that works for longer.

**2. What decides that a memory applies to this turn?**
*Recommend: `pinned` always in, plus `paths` globs matched against the workspace, with `topic`
used for search only.* That uses the fields already declared and costs no model call. The
alternatives are a retrieval step (a model or embedding picks), which is slower and unpredictable,
or all-memories-always, which does not survive a board with fifty of them.

**3. Does HQ absorb `~/.config/relay/relay.md`, or stay a sibling?**
*Recommend sibling, unchanged.* HQ cannot own `~/.claude/CLAUDE.md`, `~/.warp/WARP.md` or
`~/.codex/AGENTS.md`, which `instructions.py` already reads, so pulling only Relay's own file into
HQ splits one concept across two places for no capability.

**4. Is `scope: team` in or out?**
*Recommend out for now, keep the value legal.* `docs/TASKS-AND-MEMORY-DESIGN.md:448-452` declares
project | team | user. Project and user have obvious homes; team has no store, no sync and no
sharing model, and inventing one here would be the largest part of the work.

**5. What happens to the "loaded when it applies" label if stage 1 slips?**
*Recommend changing the text now, in its own one-line commit.* `BoardModel.cpp:161` currently tells
the user a memory card is in use. It is not. That is a false statement on screen today, independent
of everything above.

Status `discussing`, `waiting_on: owner`.

<!-- relay:entry 20260921T220533Z-a3 author=codex kind=decision -->
### Codex · 2026-09-21 22:05
Owner: "i am liking the middle option. can you include a \"globals\" tab that has our proposed \"swithcboard hq\" functionality"

Owner: "and ctrl shift g can open globals"

Card #P7SJ captures the shared Projects / Sessions / Globals pane proposal. Globals is the requested
UI home for this card's HQ functionality, with Ctrl+Shift+G as its direct entry. The owner also said
"i dont need the pane screenshot key, not sure why i have that", releasing its current binding.
This settles the requested UI placement; this card's existing memory/runtime and scope questions
remain open. Its original body and implementation status are unchanged.

<!-- relay:entry 20260921T220927Z-p1 author=codex kind=progress -->
### Codex · 2026-09-21 22:09
Claimed the HQ dependency for implementation within card #P7SJ. Owner: "lets go with subagents for efficiency". Proceed with recommended deterministic bounded memory matching, source files kept in place and no team store. Global knowledge resolution will not replace project work-card routing.

<!-- relay:entry 20260921T222440Z-q1 author=codex kind=evidence -->
### Codex · 2026-09-21 22:24
Implemented with three subagents and landed integration be42269896bc. Targeted backend/widget/keymap tests and exact-tree application build pass. Isolated Xvfb verifies all three shortcut tabs, a saved global memory consumed by runtime and draft preservation. Evidence: docs/qa_evidence/2026-09-21-projects-sessions-globals/. Added execution summary, tests and manual QA checklist; moved to needs-verification.

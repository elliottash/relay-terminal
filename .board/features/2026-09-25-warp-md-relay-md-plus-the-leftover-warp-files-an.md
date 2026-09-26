---
id: 2M26
type: work
status: needs-verification
labels: [feature, docs, cleanup]
assignee: agent
implemented_by: glm/glm-5.3
session: 97149268-97e8-4cc9-bf13-249118806781
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
source: 'pane 1, 2026-09-25, follow-up to the #HRF6 audit'
links: {plans: [], commits: [bf5aa57c4fd9], evidence: [docs/qa_evidence/2026-09-25-2m26-warp-md-to-relay-md/], related: [HRF6], github: null}
---
# WARP.md → RELAY.md, plus the leftover warp files and .claude/worktrees cleanup

## Issue
Rename the repo's own working-rules file WARP.md to RELAY.md (follow-up to the #HRF6 borrowed-names audit), update every functional and documentation reference, keep WARP.md recognized as a legacy instruction-file name for other projects, and clear the stray leftovers: two warp_*.txt knowledge dumps in the repo root and an empty .claude/worktrees directory.

> yes, WARP.md -> RELAY.md. and do the cleanup
> — elliott · [session:9698842186c7475288ae58fe80f77b2e](relay://session/9698842186c7475288ae58fe80f77b2e) · 2026-09-25

## Done means
- The repo's working rules live in `RELAY.md`; `WARP.md` no longer exists in the checkout, and Relay's instruction-file discovery recognises `RELAY.md` (first) while still reading a `WARP.md` from projects that have one — a project with both must prefer `RELAY.md`. Failure: a fresh checkout's agent prompts missing the working rules, or `test_board.py`/`test_session_protocol.py` failing.
- UI strings and every code/doc/test pointer in this repo say `RELAY.md`; the only remaining `WARP.md` mentions are history (`.board/`, `research_notes/`, `reports/`, recorded test fixtures) and the deliberate legacy-name support in `instructions.py`.
- `warp_changelog.txt` and `warp_terminal.txt` are gone from the repo root (parked in a scratch dir that expires with the session), and the empty `.claude/worktrees/` is removed. A stray `rg -l 'WARP\.md'` outside the allowlist is how failure shows.
- `scripts/relay-build` compiles and the touched test files pass.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_session_protocol tests.test_board` — 194 tests, all pass, including the new `test_relay_md_preferred_to_warp` and the scaffold-leaves-RELAY.md-alone assert. `scripts/relay-build` compiles (one pre-existing failure in the shared working tree came from another session's mid-edit `src/Pane.h`, not this change; land.py's verify gate built tip + only this change's hunks and passed). Evidence: `docs/qa_evidence/2026-09-25-2m26-warp-md-to-relay-md/`. Landed as `bf5aa57c`.

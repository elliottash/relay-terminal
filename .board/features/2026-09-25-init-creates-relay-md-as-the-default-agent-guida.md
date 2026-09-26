---
id: C8XD
type: work
status: needs-verification
labels: [feature, board, onboarding, instructions]
assignee: agent
implemented_by: glm/glm-5.3
session: 0e858e88-8869-47e7-a7c2-16425f166d4d
rank: zzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
source: 'pane 1, 2026-09-25, follow-up to #2M26'
links: {plans: [], commits: [e7ce0f13b8c2, 3cc4397e5123, 9d31b6aaf977], evidence: [docs/qa_evidence/2026-09-25-c8xd-relay-md-default-guidance/], related: [2M26], github: null}
---
# /init creates RELAY.md as the default agent guidance and points the other instruction files at it

## Issue
/init (and the board scaffold) should create a RELAY.md starter file as the project's default agent guidance, holding the short board instruction, and add a note to each existing instruction file (CLAUDE.md / AGENTS.md / legacy WARP.md) pointing agents at RELAY.md. Design questions: move the generated Board policy block out of CLAUDE.md/AGENTS.md into RELAY.md, and auto-rename a legacy WARP.md.

> yes, init should create that and that should be the default agent guidance in relay. on /init, relay also adds a note to claude.md or agents/md or warp.md to also read relay.md, which will give them a short instruction about the board. make sense? anything else it should do?
> — elliott · [session:9698842186c7475288ae58fe80f77b2e](relay://session/9698842186c7475288ae58fe80f77b2e) · 2026-09-25

## Planning notes
Grounding from the code: `/init` for guests is the CLI's own; Relay's entry points are the board scaffold (`B.scaffold`, run by the board `init` tool) and `instructions.load`, which resolves `@path` imports recursively — so `AGENTS.md → @CLAUDE.md → @RELAY.md` chains already work for Relay, Claude Code and Codex alike.

Proposed shape:

1. **`RELAY.md` is created when missing**, seeded with a short starter template (what this project is, one placeholder for the owner's working rules) plus the generated **Board section** — the same policy text the scaffold today embeds into `CLAUDE.md`/`AGENTS.md`, between the same `relay:switchboard-policy` fences so re-runs replace it in place and hand-written text is never touched.
2. **Each instruction file Relay recognizes gets a generated one-liner block** — `@RELAY.md` (an import, not prose, so every tool actually loads it) — in `CLAUDE.md`, `AGENTS.md`, and a legacy `WARP.md` when that is all a project has. Existing full policy blocks in `CLAUDE.md`/`AGENTS.md` are migrated to the one-liner on the next scaffold run.
3. **No `PROJECT_ORDER` change**: a lone `RELAY.md` is already a first hit when nothing above it exists, and the imports carry it into every prompt otherwise.
4. `doctor` warns when an instruction file `@`-imports `RELAY.md` but the file is missing (deleted by hand).

## Done means
- Running the board init (`/init`) in a project that has no `RELAY.md` creates one: a short starter template plus the generated Board section (policy text, `relay:switchboard-policy` fences), and `CLAUDE.md` / `AGENTS.md` / a Warp-terminal `WARP.md`, whichever exist, get the generated one-liner `@RELAY.md` block instead of the embedded policy text. An `@`-import actually resolves in `instructions.load`, so Relay, Claude Code and Codex all see the board policy.
- Re-running init replaces the generated blocks in place and never rewrites anything outside the fences; a hand-written `RELAY.md` gains the block without losing a word.
- A project that already has the old full policy block in `CLAUDE.md`/`AGENTS.md` is migrated on the next init run.
- `WARP.md` is never renamed (it is the Warp terminal's own agents file).
- If `RELAY.md` is later deleted by hand, `instructions.load` reports it in `skipped` ("missing") wherever a note still imports it, and the next scaffold run recreates it — there is no separate `doctor` command in Relay's backend to carry that warning.
- `tests/test_board.py` covers each bullet; evidence shows the scaffold run on this repo.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_board tests.test_session_protocol` — 197 tests, all pass. New: the note-imports-RELAY.md chain test (policy reaches the prompt through `RELAY.md`, `CLAUDE.md` keeps its own text), the old-block migration test, the hand-written-`RELAY.md` test, and the WARP.md-annotated-never-renamed test. Also fixed `test_this_repositorys_own_policy_is_a_fresh_regeneration` (#WC3E) to read the board this checkout actually uses instead of a leftover gitignored legacy `issues/` folder, and `.board/POLICY.md` — stale on main, still saying `issues/` — is regenerated and lands with this change as that test demands. Evidence: `docs/qa_evidence/2026-09-25-c8xd-relay-md-default-guidance/`. Landed as `e7ce0f13` + `3cc4397e`.
Follow-up `9d31b6aa` (owner: "dont create agents or claude or warp when absent"): the scaffold no longer creates an `AGENTS.md` when the project has none — `_new_agents_text` is gone; `CLAUDE.md`/`AGENTS.md`/`WARP.md` are annotated only when they already exist. Same suite, 197 pass, including the two rewritten creation tests; a scaffold re-run on this repo reports "unchanged ... (and the instruction files)".

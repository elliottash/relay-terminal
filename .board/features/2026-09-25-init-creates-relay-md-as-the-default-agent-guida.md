---
id: C8XD
type: work
status: executing
labels: [feature, board, onboarding, instructions]
assignee: agent
implemented_by: glm/glm-5.3
session: 0e858e88-8869-47e7-a7c2-16425f166d4d
rank: zzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
source: 'pane 1, 2026-09-25, follow-up to #2M26'
links: {plans: [], commits: [], evidence: [], related: [2M26], github: null}
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
- `doctor` warns when an instruction file `@`-imports a `RELAY.md` that is missing.
- `tests/test_board.py` and `tests/test_session_protocol.py` cover each bullet; evidence shows the scaffold run on this repo.

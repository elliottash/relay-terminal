---
id: G9ZD
type: work
status: discussing
labels: [feature, switchboard, skills, board, qa, worker]
component: [worker, board]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'a failed case under on_fail: file produces exactly one open card per server, and a third ad-hoc case produces a draft the agent can file in one call', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'owner, Relay conversation, 2026-09-23 and 2026-09-25; #1QKM §5 loops 1 and 2, open question 1'
links: {plans: [], commits: [], evidence: [], related: [1QKM, 95VZ, MSJ0, C3Q2], github: null}
---
# Close the two loops: a failed case suggests or files a card on its server, and the third ad-hoc case drafts the card that builds one

## Issue
a card is built, a case is served. a program serves the case algorithmically; a skill serves it intelligently. [...] 1 im not sure, i dont think theres a single approach for that. but 2 definitely yes, think about a doctor or lawyer using relay to help with their case work. [...] analyze these issues and add comprehensive plans to cards. clarify anything with me with questions.

## Plan
**Goal.** The two loops #1QKM §5 says the Board cannot close today: **server → card** (a case that fails because the server is wrong files, or suggests, a card *on the server*, not on the case) and **case → card** (the third ad-hoc case of one shape drafts the card that builds a server for it). The owner ruled there is no single approach for the first loop, so the choice is a per-server setting with a project override, not a global rule; a broken skill must never flood the Board. Both loops stay agent-facing: the person sees a card only when one is filed.

**Findings.**
- `backend/relay_core/cases.py`: `third_case_hint` is true exactly at the third person-served row of a server in 90 days and `hint_line` is one sentence for the agent's reply ("three cases of X served by a person in 90 days; build a server? (/deliver)"). It fires once and carries no draft; nothing stops it when a card was already filed for that server, and nothing links the card back.
- `board_tools.record_case` writes `fail` rows from a `## Verdict` (`verdict_from_text`), from a move to a stage other than done, and from Try it (`tryit_protocol`); `record_turn_cases` writes a `pending` row per profiled skill at turn end. A fail row does nothing afterwards.
- `skills.PROFILE_VOCAB` has no key for what a failure should do; `qa_policy.parse` reads `board.yaml qa:` with four keys and no per-server layer.
- Work cards have no field naming the server they build or fix; `_server_for_card` guesses from the loaded skill or the newest ledger row about the card, else `card:<ID>`.
- Creation limits (`agent.max_creates_per_turn: 5`, policy rule 8) already exist and apply to any card the worker files.

**Steps.**
1. **Profile key `on_fail`** in `skills.PROFILE_VOCAB`: `suggest` (default) | `file` | `none`. Project override in `board.yaml`: `qa: on_fail:` for every server, and `qa: servers: {<skill id>: {on_fail: …}}` for one; `qa_policy.parse` gains the layer with the same fall-through and problem sentences. Tests in `tests/test_qa_policy.py`, `tests/test_skills.py`.
2. **`server:` on work cards** (front matter, optional; a skill id or a program path; validated like the ledger's `server`). Set by the agent on the card that builds or fixes a server, and by the worker on the cards this card files. `_server_for_card` reads it before guessing. `board_list {server: X}` filters on it. Shown nowhere except the skill page (#9FX8). Protocol 19.20 note.
3. **Server → card.** In `record_case`, when the row's verdict is `fail` and the server is a skill or program (not `person`, not `card:`): resolve `on_fail` (project per-server → project global → profile → `suggest`). `suggest`: the tool result of the write that produced the row carries `server_card: {suggest: true, title, request, labels, related, server}` and one hint line; the agent puts the line in its reply and files only when the person says so. `file`: the worker creates a bug card — title "<server> failed case <id>", `server:` set, `links.evidence` the case id, `related` the card the case was about, `request` the verdict's first line (ids only, never `input`, when the row is confidential) — and comments the case on it. `none`: the row only. **Flood guard** for `file`: at most one *open* card with that `server:` and the `auto-filed` label; a second fail appends a `note` to it instead of filing; the per-turn creation limit still applies on top. Tests: each mode, the guard, confidentiality, the turn limit.
4. **Case → card.** `board_case` result: when `third_case_hint` is true, carry `suggested_card: {title: "Build a server for <server>", request: the three rows as ids and dates (inputs only when not confidential), labels: [feature, skills], server, related}` so `/deliver` can file it in one `board_create_card`. The hint is **suppressed** when a card with that `server:` exists and is open or closed within 90 days (`cases.third_case_hint` gains a `cards` argument; `board_tools` passes the ids). A filed card's thread gets an `evidence` entry listing the case ids.
5. **`pending` rows.** Out of scope: `cases.stats` already ignores them and #GW74 decides the authority counters; note it there.
6. **Docs.** Protocol 19.22 gains `on_fail`, `server:`, `server_card` and `suggested_card`; `board_policy.md` rule 6 gets one sentence ("a case that fails files against the server per its `on_fail`"); the `deliver` skill's step 5 names the draft.

**Risks.**
- A `file` skill whose UI changed fails every case: the guard caps it at one open card plus notes; the registry (#9FX8) shows the fail streak.
- Auto-filed cards for a confidential server must carry ids only; the row already drops `input`, the card builder must never read it back from the verdict text.
- `server:` is a new front-matter key: `board.check` must accept it and the field list in `WORK_FIELDS` grow; older boards ignore it.
- Owner decisions (thread): default mode, whether `file` ships now, the `server:` field.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_cases tests.test_qa_policy tests.test_board_tools.LoopTests`; then one staged board with a profiled skill: force a fail row under each mode and read the result; record three person-served `board_case` rows and read the draft; file it and confirm the fourth row carries no hint.

## Done means
- A skill's `profile:` (or `board.yaml qa: servers:`) says `on_fail: suggest | file | none`; a fail row under `suggest` puts a draft in the tool result, under `file` creates one bug card with `server:` set and never a second while that one is open, under `none` nothing.
- The third person-served case of one server in 90 days returns `suggested_card` the agent can file in one call, and the hint stops once a card with that `server:` exists.
- Work cards accept `server:`; `board_list {server}` filters on it; confidential servers produce cards and drafts carrying ids only.
- Failure looks like: two open auto-filed cards for one server, a draft quoting a confidential input, or a hint that fires again after its card was filed.

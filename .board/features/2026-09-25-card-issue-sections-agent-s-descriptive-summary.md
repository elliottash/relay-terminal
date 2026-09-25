---
id: EMWF
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
session: c0a599e0-a250-4e1b-b441-16a276235a3d
rank: zzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: text, primary: script, also: [], human: none, criteria: New cards carry summary + attributed session-linked quote; legacy bodies unchanged; policy text updated, sign_off: none, effort: medium}
source: Relay pane session, 2026-09-25
links: {plans: [], commits: [543c6119], evidence: [docs/qa_evidence/2026-09-25-emwf-issue-quote/], related: [WZ3K], github: null}
---
# Card Issue sections: agent's descriptive summary, with the user's request kept as an attributed quote linked to its session

## Issue
feature request. when the agent founds a card, it usually quotes the user, here is an example: #WZ3K where issue is "delegate a subagent to file the scratch watcher issue". i think issue should be more of a descriptive summary. but i do really like the quote. it shoudl be kept, but indicated as a user quote  with the user indicated and a cited session id that links to the session.

## Plan
Findings: the quote's session id is already in reach — `Agent.ask` stamps `board.context.session_id` (agent.py:2625) each turn, so `BoardTools._create` can cite it with no protocol change; the Board card page renders the body with `setMarkdown` and resolves session links (`BoardView::resolveAgentLink`, Kind::Session), so a markdown link in the attribution is clickable with no C++ change; `getpass.getuser()` names the person; session ids are UUIDs (this card's own front matter shows the shape).

1. `board.py` `new_card`: add `summary`, `quote_user`, `quote_session` keywords; the body becomes `## Issue\n{summary}\n\n> {request lines}\n> — {user} · session:{id} (as a relay://session/{id} markdown link) · {date}`. `request` only → legacy plain Issue (back-compat for imports/intake/guests); `summary` only → summary alone. Rewrite rule 2 (`board_policy.md` + `policy_text` appendix) from "Verbatim requests" to summarize-then-quote.
2. `board_tools.py`: create spec requires `summary`, `request` optional and described as the verbatim quote; `_create` passes `self.context.session_id` and the OS user into `new_card`; duplicate similarity still compares `request` against the section text.
3. `deliver` skill step 2 wording matches.
4. Tests: body format with/without session, summary-only, legacy request-only; policy-text assertions; update tests that pin today's body.
Scope edges (all keep today's plain Issue, none quote a person): split cards (board.py:3235 excerpts), forge_sync imports, globals/alias cards, signal-promoted bug cards (board_tools.py:3513), `.board/*_intake.txt` triage, and guests serving the old schema until their tool list refreshes. `## Issue` readers take the whole section text, so mixed-format boards need no migration; only the Issue-section doc inside `policy_text` and the duplicate-similarity input (request vs section text) are affected, and similarity keeps working because the quote is in the section.

## Done means
- `board_create_card` gains `summary` (the filing agent's descriptive summary, required) and `request` becomes the optional verbatim quote — omitted when a card quotes no one (a fault an agent noticed, an import).
- A new card's `## Issue` reads: the summary, then the user's words as a `>` blockquote attributed `— <user> · session:<id> (link) · <date>`, the session link opening that conversation on the card page.
- Cards filed the old way (quote only, no summary — intake, imports, old-prompt guests) keep today's plain Issue, and existing cards are untouched.
- The policy text (system prompt rules + `.board/POLICY.md`) and the `deliver` skill teach the new rule.

## Tasks

- [x] board.py: new_card summary + quoted-request builder with session link <!-- t:z7 -->
- [x] board_tools.py: create spec (summary required, request optional quote) + context wiring <!-- t:pc -->
- [x] Policy text: board_policy.md rule 2, policy_text appendix, deliver skill <!-- t:ch -->
- [x] Tests: body formats, policy assertions; update pinned-body tests <!-- t:sc -->
- [x] Land via scripts/land.py; regenerate this board's POLICY.md <!-- t:0e -->

## Decisions
- 2026-09-25 — "lets just plan here, dont implement": the plan is the deliverable for now; implementation waits for the owner's go.

## Execution Summary
Landed in 543c6119 (17 files; board.py, board_tools.py, board_policy.md, deliver SKILL.md, both test files, the regenerated .board/POLICY.md, this card, two new bug cards and the evidence).

- `board.py`: `issue_quote()` writes the request as a `>` block ending `> — {user} · [session:{id}](relay://session/{id}) · {date}`; `new_card` takes `summary`/`quote_user`/`quote_session`. Summary+request → summary then quote; request only → legacy plain Issue; summary only → summary alone.
- `board_tools.py`: create spec requires `summary`, `request` optional ("the user's own words, verbatim … omit when the card quotes no one"); `_create` passes `getpass.getuser()` and `self.context.session_id`; duplicate similarity uses `request or summary`; `_maybe_text` added for optional text args.
- Policy: rule 2 is now Summarize-then-quote; appendix example shows the quote block; deliver skill step 2 updated. The policy block was 3208 bytes at HEAD — already over its 3 KiB budget — so the same pass re-tightened rules 1, 3, 5, 7, 8, 11 and the memory note (meaning and pinned phrases kept) to 2977 bytes; `test_the_board_policy_block_stays_tiered` is green again.
- Decisions applied: attribution shows the OS username (elliott); the link label carries the full session id so it stays greppable.
- Note for the verifier: live sessions still serving the old schema (this one included) keep requiring `request`; the new `summary`-required schema reaches sessions started after the landed worker code. #M58P (dropped) was filed through the real tools as the live check.

Unrelated, filed: #K54A (prompt 497 bytes over budget, red at HEAD) and #G19V (guest bridge parity, red at HEAD).

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_board_tools tests.test_board tests.test_system_prompt tests.test_board_protocol tests.test_guest_board_bridge` — 708 tests; every test covering this change passes, including the four new CreateTests cases and the four new IssueSectionTests cases, the legacy-body test, both policy-phrase pins, the tiered-size test and the POLICY.md freshness test. Two pre-existing failures remain, reproduced identically on a clean copy of git HEAD's backend and filed as #K54A and #G19V. Log: docs/qa_evidence/2026-09-25-emwf-issue-quote/test-run.txt.

## Try it
Open: `docs/qa_evidence/2026-09-25-tryit-EMWF/stage.sh`

It prints three seeded card Issues side by side: one filed the old way (your words pasted in as the whole Issue, like #WZ3K), one the new way (summary, then your words as a quote attributed `— elliott · session:<id> · 2026-09-25`), and one quoting nobody (summary alone). The session link resolves on real cards; in this fixture it is a stand-in id.

One judgement (~1 minute): do the two new sections give you what you asked for — a real summary first, and your words unmistakably yours, with who said them and where — or is anything still missing, wrong or noisy?

Expected: docs/qa_evidence/2026-09-25-tryit-EMWF/expected.md (sealed until you answer)

---
id: EMWF
type: work
status: executing
labels: [feature, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
session: c0a599e0-a250-4e1b-b441-16a276235a3d
rank: zzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: text, primary: script, also: [], human: none, criteria: New cards carry summary + attributed session-linked quote; legacy bodies unchanged; policy text updated, sign_off: none, effort: medium}
source: Relay pane session, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [WZ3K], github: null}
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

- [ ] board.py: new_card summary + quoted-request builder with session link <!-- t:z7 -->
- [ ] board_tools.py: create spec (summary required, request optional quote) + context wiring <!-- t:pc -->
- [ ] Policy text: board_policy.md rule 2, policy_text appendix, deliver skill <!-- t:ch -->
- [ ] Tests: body formats, policy assertions; update pinned-body tests <!-- t:sc -->
- [ ] Land via scripts/land.py; regenerate this board's POLICY.md <!-- t:0e -->

## Decisions
- 2026-09-25 — "lets just plan here, dont implement": the plan is the deliverable for now; implementation waits for the owner's go.

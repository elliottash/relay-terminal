---
id: XHH4
type: work
status: planned
rank: zzzzzzzzzzzzzzzzr
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# potential bug: switchboard agent didnt recognize a session id.

## Issue
potential bug: switchboard agent didnt recognize a session id.

## Done means
An agent handed a saved conversation's session id — for example one copied out of a pane's ⓘ info — can identify that conversation (title, when, model, workspace) without opening a pane, through `app_sessions_search` alone. Failure looks like the original report: the Switchboard (or Sessions) agent answers "I don't recognize that session id" for an id that is sitting right there in the Sessions list, because its only search is full-text over titles and message text, where a bare id never matches.

## Plan
**Goal** — close the gap behind the owner report: an agent given a bare session id could not "recognize" it, because no read-only tool resolves an id to a conversation.

**Findings**
- `backend/relay_core/app_tools.py` `AppTools._sessions_search` answers only from `ConversationIndex.search`, an FTS5 query over titles and message text (`conv_index.py`, `entries_fts`). Session ids are not indexed text, so pasting a 32-hex id as the query returns nothing and the agent concludes the session does not exist.
- The operator vocabulary (`conv_index.py` `OPERATOR_KEYS = (project, file, model, branch, before, after, has, is, in)`) has no `id:` operator.
- The only id-exact path is `AppTools._conversation_row`, and it is reachable solely through `app_open {target: "conversation"}` — which *opens a pane*, a visible side effect no agent should perform just to identify a conversation.
- `ConversationIndex.conversation(session_id)` (`conv_index.py:2498`) resolves an id globally (no workspace scoping), so the lookup itself is not the problem; reachability is.
- Related context: #YQC3 put the session id (with a copy button) into the pane ⓘ info, which is the likely source of the pasted id; #H6VQ/#FEJQ are the same helper-tools family.

**Steps**
1. **Confirm the diagnosis first.** Read the Switchboard console's own session/log around 2026-09-20 (worker `relay.log` and the switchboard session under the sessions root) and find the turn the owner is reporting: what id form was pasted (full hex id, a prefix, or a pane token from `app_panes`?). If it was a *pane token* rather than a conversation id, stop and re-scope the card with a comment before changing code — the fix would belong in pane resolution, not search.
2. Add an `id` lookup to `conv_index.ConversationIndex`: a small method (or an `id:` search operator, matching the existing operator machinery at `conv_index.py:487-564` and `_filters` at 2201) that resolves an exact session id — or an unambiguous prefix of, say, ≥ 6 characters — to its conversation row. Bound parameters only, as the rest of the module does.
3. Wire it into `AppTools._sessions_search` (`app_tools.py`): when the query is an `id:<value>` operator or is itself a bare id/prefix, resolve and include that row in the same shape the FTS results use (`id`, `title`, `updated`, `model`, `workspace`, `turns`, `source`), ahead of or merged with the text results. Keep the 2026-09-20 `session_id` → `id` normalisation intact.
4. Update the `app_sessions_search` tool description (`app_tools.py` `TOOL_SPECS`) and the operator table in `docs/AGENT-SESSIONS-PROTOCOL.md` (section 14) so agents and the Sessions pane know an id — or `id:` — works as a query. If the Sessions pane's own search goes through the same `search` path it inherits the fix; check that and say so in the commit message.

**Risks**
- Step 1 may show the report was about a pane token (`app_panes` id) or a helper session id, not a saved conversation — then this plan's steps 2–4 are the wrong fix; re-scope rather than force it.
- Prefix matching must stay unambiguous: never resolve a prefix that names more than one conversation to a single row; return the candidates instead.

**Verify**
- New unit tests in `tests/test_app_tools.py` (search by full id returns the row; by unique prefix; ambiguous prefix returns candidates, not one row; unknown id returns an empty result, not an error) and in `tests/test_conv_index.py` for the new lookup/operator. Run just those two test files/cases — not the full suite.
- Live check under a normal build: paste a real session id (from a pane's ⓘ info) into the Switchboard console and confirm the agent now names the conversation instead of disavowing it.

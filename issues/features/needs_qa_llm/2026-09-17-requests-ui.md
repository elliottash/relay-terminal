# Request ledger UI: Requests chip, request list, Continue after the step limit, open items

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (Claude Code), 2026-09-17
- **Source**: `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` section 6 item 6 and section 9; contract `docs/AGENT-SESSIONS-PROTOCOL.md` section 12 (backend commit 8a18a11)

## Behavior as implemented

- **Chip.** `Requests N open` (amber) while any user request is open or in progress, else `Requests ✓ N` (N = total); hidden until the session has a request. Tooltip: counts per status and open todos. It sits in the queue strip header while the strip is visible, otherwise in the composer row after the context indicator. Updated from every `requests` event. Open count was chosen over done/total because done/total never reaches the total when requests are cancelled, deferred or blocked, and "is anything still open" is the question the chip answers.
- **List** (click the chip, `/requests`, `/todos`, Actions › Requests and todos…, bindable `agent.requests`, no default key). Floats over the right of the terminal. Rows: glyph ✓ ◐ ○ ✕ ⏸, id, elided preview; tooltip and the detail area below show the verbatim text (`request_get`), status, source, turn, reason, attachments, audit flags. Enter/Space (or →/←) expands to reasons, "⚠ may be unaddressed: …" flags and linked todos with their glyph and note; todos linked to no listed request show under "Other todos". Keys: `d` done, `x`/Delete cancel (`cancelled_by_user`), `o` reopen, `r` re-ask (`request_reask`, when `queue`), Esc closes; buttons do the same.
- **Terminal lines.** `done {stop_reason: "limit"}` prints `‖ Stopped at the step limit (3 model steps, limit 3) · the request stays open` and `▸ Continue (Ctrl+click · /continue)`, a `relay://continue/<pane>` link (plain text when the shell is not idle at its prompt). Continue (link, `/continue`, Actions › Continue agent turn, bindable `agent.continue`) sends an ordinary ask "Continue". `done`/`cancelled`/`error` with `open_items` print `○ 2 requests still open: R3 “…”, R4 “…” · 1 todo open · /requests`. `completion_check` prints `✦ checking open items (1/2)`. `request_audit` with flags prints `⚠ may be unaddressed: R2 “…”`. `state_loaded.open_requests` adds `○ N requests still open · /requests`; `recap.open_items` adds an `Open · …` line; the resume picker's Turns column shows `5 · 3 open`.
- **Agent options.** Step limit per turn (1–500, default 50), Tool-call limit per turn (1–2000, default 150), Audit requests after each turn (off). Stored in QSettings `agent/max_steps`, `agent/max_tool_calls`, `agent/audit_requests`, sent in `configure` and immediately via `set_agent_options`; the `agent_options` status line shows them.
- **Hints.** Clicking the chip: "Next time: /requests …" (or the bound key). Continue from the link or palette: "Next time: /continue …" (or the bound key). Palette aliases: todos, tasks, ledger, open items, continue, keep going, limit, max steps, audit.

## Implementer check (not a QA verdict)

Automated: `tests/requests_test.cpp` (model parsing, verbatim text across refreshes, chip text, glyphs, inline texts, panel keys d/x/o/r/Esc and refresh keeping selection); `ctest --test-dir build` 6/6; `./scripts/test.sh` 272 OK; build without new warnings.

Live under Xvfb, isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`, preset `openrouter` (DeepSeek V4.1 Flash), `agent/max_steps=3`, audit on. Screenshots in `docs/qa_evidence/2026-09-17-requests-ui/`:
1. A three-file task stopped at 3 steps: limit line, Continue link, open-items line, chip `Requests 1 open` (01).
2. `/requests` list; `x` cancelled R2, `d` marked R1 done (chip `Requests ✓ 3`), `o` reopened and `r` re-asked R1 (◐, prompt printed) (02–04).
3. A todo-planned request stopped at the limit: `1 request still open … · 2 todos open` (05).
4. Palette › Step limit per turn… set 50 (12); restart and `/resume`: picker `5 · 3 open`, `○ 3 requests still open`, recap with `Open · …` (06–07).
5. `/continue` plus a queued prompt: chip in the queue strip header (08); `/todos`, R4 expanded to T1–T3 (09); chip click hint (10); palette "todos" alias (11).

Not verified live: `completion_check` and `request_audit` lines (the model never triggered them; covered by unit tests of the texts), Ctrl+click on the Continue link (no URL handler under Xvfb; `/continue` was used), `cancelled` with open items.

Observed, not in this change: interrupting a turn while its first model request was streaming printed `✗ Agent error (AttributeError).` (screenshot not kept). Likely `ChatProvider.cancel()` closing the HTTP response from another thread so `readline` raises AttributeError, which `complete()` does not map to `Cancelled`.

## QA checklist

1. Ask something multi-part with a low step limit (Agent options › Step limit per turn… = 3): the limit line, `▸ Continue` and the open-items line appear; Ctrl+click Continue continues the work.
2. The chip counts match the list; it moves into the queue strip while prompts are queued and back afterwards.
3. In the list, only the keyboard: select, expand, `d`, `x`, `o`, `r`, Esc. The worker's state matches after `/resume`.
4. Re-ask while the agent is busy: the request is queued and runs after the current turn; the chip shows it in progress.
5. Turn on Audit requests; ask two things and let the model skip one: a `⚠ may be unaddressed` line appears and the flag shows under the request.
6. Narrow split pane: the list stays usable and the chip does not push the composer controls off-screen.

---
id: WGAR
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code), 2026-09-17; reworked into Tasks the same day
rank: t1
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: '`docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` section 6 item 6 and section 9; contract `docs/AGENT-SESSIONS-PROTOCOL.md` section 12 (backend commit 8a18a11)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Tasks UI: Tasks 3/5 chip, task list, Continue after the step limit, end-of-turn task line

## Rework: Tasks (owner request, 2026-09-17)

> "for the task list, why are you calling it requests rather than tasks. i actually want 3 / 5 type tracking. at the end, it says (2 failed, or (1 deferred, 1 failed)"

- **Name.** User-facing name is **Tasks**: chip, panel title, Actions › Tasks…, `/tasks` (primary; `/requests` and `/todos` stay as aliases), hints, docs. Requests (what the user typed, verbatim) are the group rows inside the panel; the tasks sit under them. Code and protocol names (`RequestLedgerModel`, `agent.requests`, `requests` events) are unchanged. No backend change.
- **What a task is.** Each of the model's todos. A user request with no linked todos counts as one task itself (a one-ask turn shows `Tasks 0/1` → `Tasks 1/1`); so does a request that is open again while all its todos are settled (reopened, re-asked, or its turn stopped after the last todo). Relay-origin requests (subagent wake-ups) are not tasks. Todos the model later drops from its list are kept in the count when they were settled (so `5/5` does not shrink); dropped open todos disappear.
- **Mapping.** todo `completed` → numerator; `blocked` → failed; `deferred` → deferred; `cancelled` → cancelled. Request without todos: `done` → completed, `cancelled`/`cancelled_by_user` → cancelled, `blocked` → failed, `deferred` → deferred. Open todos and open requests are *not yet done* while any request is `in_progress` or they are waiting in the queue (never delivered, or re-asked/requeued since the last delivery); otherwise they are *unfinished* (their turn ended with an error, cancel, `stop_reason: limit`, or a completion check that gave up). Open todos of a request the user marked done (`d`) or cancelled (`x`) follow that request.
- **Chip.** `Tasks c/t` for the current list, neutral while anything is not yet done or a request of the list is in progress; when settled `Tasks 5/5` (green) or `Tasks 3/5 (1 failed, 1 deferred)` (amber). Suffix order: failed, deferred, cancelled, unfinished (the owner's example "(1 deferred, 1 failed)" is printed as "(1 failed, 1 deferred)" to follow the stated order). Tooltip: counts, the earlier lists' total, session request count. Hidden until there is a task.
- **Current list (batch) rule.** Requests are walked in id order. A new user request starts a new list when the current list has at least one task, none of them is not-yet-done, no older request is in progress, and it was not delivered in the same turn as a request of the current list; otherwise it joins the current list. A todo seen for the first time joins its request's list if that request is new in the same update; otherwise an open todo joins the current list and a settled one the latest list of its requests. Tasks of earlier lists that become unfinished or not-yet-done again (the step-limit todos when you say "Continue", a reopened or re-asked request) move into the current list; a re-ask of an earlier request when the current list is settled starts a new list first. So after `Tasks 1/2 (1 unfinished)`, "Continue" shows `Tasks 0/2` (the unfinished todo plus the Continue request), not `0/1`. After a restart or `/resume` the same walk runs over the loaded ledger (turns group requests), so a resumed session shows its last list; a new chat, another session or a rewind (highest id went down, or an id now has other text) resets the lists. The rule lives in `RequestLedgerModel::updateBatches()`.
- **Panel.** Title `Tasks · 3/5 (1 failed, 1 deferred)`. Current requests first, unfolded, each `glyph id c/t preview` with reasons, audit flags and todos (✓ ◐ ○ ✗ failed ⏸ deferred ✕ cancelled); then "Other tasks (n)" (todos linked to no listed request); then a folded `Earlier · c/t (…)` row with the older requests. Enter/Space fold the selected request or group; `select()` unfolds Earlier. Keys d/x/o/r/Esc unchanged.
- **End-of-turn line.** On `done`/`cancelled`/`error`: `✦ Tasks 3/5 (1 failed, 1 deferred) · T4 “change the system hostname” failed, T5 “write a 20-page report on quan…” deferred  · /tasks` (non-completed tasks, cut with "+N more"). Not printed for a list of one completed task. Replaces "N requests still open". `state_loaded` prints `○ N unfinished requests · /tasks`; recap `Open · …` lines point to `/tasks`.
- **Key.** `agent.requests` toggles the panel: **Ctrl+Shift+K** in the Relay preset (no other Relay action uses it; Relay's window filter takes it before KonsolePart's own Ctrl+Shift+K "Clear Scrollback and Reset", which remains in the context menu). Unbound in the Warp (clear blocks), VS Code (delete line) and Konsole (clear scrollback and reset) presets. Documented in `docs/KEYBINDING-PRESETS.md`.
- **Hints.** Chip click → "Next time: Ctrl+Shift+K · task list" (or `/tasks` when unbound); `/tasks`, `/requests`, `/todos` → the key hint when bound; palette activations get the generic palette hint. Palette aliases for Tasks…: todos, requests, ledger, open items, checklist, progress.

### Implementer check for the rework (not a QA verdict)

Automated: `tests/requests_test.cpp` 12 cases, new: every outcome mapping including user overrides, queued and running states, suffix order and cut-off; live batch sequence (restart on settle, steer joins, dropped todos kept, step-limit unfinished carried by "Continue", re-ask starts a new list, reopen, new chat reset); rebuild on load and on another session; panel Earlier row. `ctest --test-dir build` 6/6; `./scripts/test.sh` 273 OK; build without warnings.

Live under Xvfb (isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, workspace under `/tmp`), preset `openrouter` (DeepSeek V4.1 Flash), default limits. Screenshots `docs/qa_evidence/2026-09-17-requests-ui/tasks-*.png`:
1. A five-ask prompt (three file tasks, one to mark blocked, one to mark deferred): chip `Tasks 0/5` while running (01, cropped to the composer because the model's `ls -l` output above showed file owners).
2. End of turn: chip `Tasks 3/5 (1 failed, 1 deferred)` amber and the line `✦ Tasks 3/5 (1 failed, 1 deferred) · T4 … failed, T5 … deferred · /tasks` (02).
3. Ctrl+Shift+K opened the panel: R1 with T1–T5 and reasons (03).
4. A one-ask prompt afterwards: chip restarted at `Tasks 0/1` (04), then `Tasks 1/1` green with no task line (05).
5. Restart and `/resume`: chip `Tasks 1/1`, `/tasks` opened the panel with `Earlier · 3/5 (1 failed, 1 deferred)` folded and the hint "Next time: Ctrl+Shift+K · task list" (06); Enter unfolded Earlier (07).

Not verified live: `unfinished` (step limit or cancel) and Continue carrying tasks, re-ask/reopen batching, the Warp/VS Code/Konsole presets leaving the key unbound (unit tests and code only). Ctrl+Shift+K was pressed from the prompt box only (not from the terminal or inside a program). Screenshot 03 predates a small change: the request row's `c/t` now comes before the preview (in 03 it was after it and elided); 07 shows the new order. The resumed recap still says "1 request still open: R1" for a blocked request (backend `recap.open_items` wording, unchanged).

## Behavior as first implemented (superseded where the rework above differs)

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

1. Ask something with several parts (e.g. three small file edits plus one the agent must mark blocked and one deferred): the chip counts `Tasks 0/5` → … while running, then `Tasks 3/5 (1 failed, 1 deferred)` amber; the terminal ends with the matching `✦ Tasks …` line naming the failed and deferred tasks.
2. A single simple ask afterwards: the chip restarts at `Tasks 0/1` and ends green `Tasks 1/1` with no task line; the panel shows the earlier list under a folded `Earlier · 3/5 (…)` row.
3. Set Agent options › Step limit per turn… to 3 and ask for several tasks: at the limit the chip shows `(… unfinished)`, the limit line and `▸ Continue` appear; Continue (Ctrl+click or `/continue`) shows a new list that includes the unfinished tasks and ends settled.
4. Ctrl+Shift+K toggles the panel from the prompt box, from the terminal and inside `less` (Relay preset). In Actions › Shortcut preset › Warp, VS Code and Konsole the key does nothing to Relay (unbound); `/tasks`, `/requests`, `/todos` and the chip still open it. Clicking the chip shows the "Next time" hint.
5. In the panel, only the keyboard: select, fold, `d`, `x`, `o`, `r`, Esc; the chip counts follow (e.g. `x` on an open request → cancelled in the suffix; `o` on an earlier request brings it into the current list as unfinished). The worker's state matches after `/resume`, and the resumed chip shows the last list, not the whole session.
6. Stop a multi-task turn with Esc: the line and chip show the open tasks as unfinished.
7. Narrow split pane: the list stays usable and the chip (with a long suffix) does not push the composer controls off-screen.

---
id: 8YQ9
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
rank: zzzzzzzz
created: '2026-09-19'
source: pane 1, 2026-09-18
links: [1AXN, KH72, 59b7b190, 6a541562, 2d0d4279, 04b0db90, 0c9cd894, 28b56483]
---
# Switchboard page agent (switchboard-as-context) + new-project survey, clean-up and per-section triage buttons

## Issue
add an agent to the switchboard main page, which will use the switchboard as context by default, designed to help you reorganize etc. put the clean up button down there, or see if there should be other buttons -- check sections, etc. also add a triage button next to each section.

## Tasks

- [x] Agent on the Switchboard main page: switchboard content as default context (all tabs/cards), aimed at reorganizing — merging duplicates, moving cards between tabs/statuses, tidying sections <!-- t:f2 -->
- [x] Move the clean-up button down next to that agent, and survey what other buttons belong there (check sections etc.) <!-- t:z8 -->
- [x] Triage button next to each section (tab column?) on the Switchboard page <!-- t:vn -->
- [x] Decide which model role drives it (see #KH72 model roles — a Switchboard role already exists there) <!-- t:q9 -->
- [x] Clickable problems: invalid cards (and the notice at the top) are clickable; clicking pre-fills the switchboard agent's composer with a fix request (draft-for-confirmation, never auto-run) <!-- t:c6 -->
- [x] Message queueing in the switchboard agent, same semantics as the main terminal panes (#N8VK FIFO / QueueSubmit): a second prompt queues while a turn runs; queued messages shown visibly with the same queue UI as the terminal panes <!-- t:1p -->
- [x] Composer parity with the main panes: model selection, microphone for voice transcription, and the context-left indicator (main composer row already has all three — src/Pane.h "context left / model / microphone"); survey for anything else missing. **Done**: microphone and context-left in 2d0d4279; the model box came with #BRD3's 0a7a43d7, which took the seam `BoardChatPanel::addComposerWidget` left for it, and 28b56483 stopped the filter row reserving width for the two widgets that had left it. Nothing else was found missing. <!-- t:6m -->
- [x] New-project survey: runs on the new board, as the agent's opening turn (so the picker path #916B gets one too — init skips the import offer). Reuse `project_probe.probe()` + `board_import.propose()`, report the probe's `hints` as what Relay leaves alone, no writes until the user confirms. <!-- t:7r -->
- [x] GitHub issues corpus: for a `.git` repository the agent offers to look on github.com for that repo's issues and bring them in — `project_probe` resolves `primary` (upstream over origin) and `forge`. Offer only, never auto-sync. **Done** (04b0db90): #GDQN has landed, so the survey's "Look for issues on GitHub" button sends `forge_sync_plan {repo}` — a dry run that by protocol (19.14) writes to neither side — and reports what a sync would do, always saying nothing was written. `forge_sync_run` is never sent from here: that is #ZKR0's surface, and the card says offer only. <!-- t:qt -->

## Decisions
- Problems found by check/triage are clickable; clicking one sends a fix note to the switchboard agent (owner, 2026-09-19: "if there are problems, when you click on them, it sends a fix note to the switchboard agent").
- Clicking an invalid card or the notice at the top pre-fills the switchboard agent's composer with a fix request — draft-for-confirmation, not auto-run (owner, 2026-09-19).
- Switchboard agent composer mirrors the main terminals: message queueing while a turn runs, plus model selection, voice-transcription microphone, and a context-remaining indicator (owner, 2026-09-19: "draft you confirm. also allow queueing of messages in the switchboard agent, same as the main terminals").
- For a new project the Switchboard agent runs the survey itself: it searches for existing todos/tracking and offers to convert what it finds into cards; if the project is a `.git` repository it also offers to look on github.com for an issues corpus to sync (owner, 2026-09-19: "for a new project, the switchboard agent should search for existing project todos or tracking. if it finds anything, it offers to conver to switchboard issues. if its .git, it should offer to look on github.com for an issues corpus to sync.").
- The new-project survey runs on a board that already exists: it is the switchboard agent's opening turn on the freshly created board, not part of the init question before the board exists (owner, 2026-09-19: "yeah, new board required"). Because the survey lives on the board page, the picker path (#916B) gets it too, instead of a board with no survey.

## Plan
**Goal** — the Switchboard's list page grows the page agent: a persistent whole-board conversation panel whose composer mirrors the main panes (FIFO queueing, model picker, microphone, context-left), the Clean up button moved into its row, a per-section triage button, and problems and triage findings that pre-fill the composer with a fix note. The worker side (protocol 19.18, `relay_core.board_chat.py`, `board_check {section}`, the survey) landed in `59b7b190`; this is the GUI half, plus one small worker fix the model picker needs.

**Findings** — code as of `59b7b190`:

- `backend/relay_core/board_chat.py` `PageAgent`: one conversation per board per worker, FIFO queue (cap 20), `state()` = `{running, turn_id, model, survey, seconds, queue[], history[]}`. Turn events are the ordinary ones tagged `chat: true` (`delta`, `thinking*`, `tool_started`, `tool_result`, `context`, `done`…), and the `board` event carries `chat: state()` — so a pane opened mid-conversation draws the whole panel from one event.
- `backend/relay_core/board_protocol.py`: `board_chat {text, model?, survey?}` (`model` is a role id, empty = the `switchboard` role), `board_chat_cancel`, `board_chat_queue_remove {item}`, `board_chat_queue_move {item, to}`; `board_check {section}` answers `board_problems {items, section}`; the survey emits `board_survey {root, project, hints, counts, proposals, git}` and then runs the read-only turn; `board_import_apply {keys}` (19.13) is the confirm path.
- `PageAgent._start` rebuilds its agent only when `self.model` (set by `board_chat {model}`) changes — a `configure` that changes the `switchboard` role's resolved model does **not** rebuild a live conversation (`chat.drop()` runs only on `_repoint`). The GUI's picker goes through `configure` (`onModelPick`), so it needs a worker-side nudge or the pick silently keeps the old provider.
- `src/BoardPane.{h,cpp}` `BoardView`: the list page is built in `buildListTools` (count, filter, `+ New card`, `Clean up` → `requestCleanup`/`updateCleanupButton`), `m_modelBox` (`rebuildModelBox`, fed by `configured`/`presets`, picks go out through `onModelPick`), section checkboxes, `RowList` rows with section headers, and `showProblems` rendering the problems banner (`m_problems`, RichText, `linkActivated` currently opens the file). Card turns (19.16) already stream into `CardDetail` through `m_cardTurns`; `handleCleanupEvent`'s `cleanup: true` routing is the exact precedent for `chat: true`.
- Composer-parity references in `src/Pane.h`: the queue strip (rows in delivery order, × removes, reorder), the mic (`relay::voice::Capture` → `{"type":"transcribe",…}` → `transcribed` inserts at cursor; `backend/worker.py:304` handles it on any worker, the board worker included), the context-left chip (`context` event → "% left" label, `Pane.h:3933`).
- Wiring: `src/RelayWindow.h:4633` hosts `BoardView`; `onSend` → `boardWorker(workspace)->send(…)`, `onModelPick` writes the `switchboard` role and reconfigures the workers.
- Tests: worker side in `tests/test_board_chat.py` (35 cases); `BoardView` offscreen widget tests live in `tests/boardmodel_test.cpp`, driven through `handleEvent` by hand.

**Steps**

1. **Worker nudge for the picker** (`board_chat.py` + `board_protocol.py`): on `configure`, when the resolved `switchboard` model differs from the one the live conversation was built on, mark the `PageAgent` for rebuild (a tiny `invalidate()` clearing `_built_model`); `_start` already rebuilds keeping every message. One new case in `test_board_chat.py`.
2. **The panel** (`src/BoardPane.{h,cpp}`): a chat panel pinned to the bottom of the list page — a conversation view (QTextBrowser: owner lines, agent markdown, streaming in), the queue rows under it, then the composer row. List page only: an open card keeps the page to itself, as the tools row already does. State comes from `chat` on the `board` event and from `board_chat_state`.
3. **Routing**: in `handleEvent`, `chat: true` events route to the panel before card/cleanup handling (the `handleCleanupEvent` precedent): `board_chat_started/queued/cancelled/state`, `delta` and `thinking*` (stream + thinking line), `tool_started`/`tool_result` (a progress line), `context` (the chip), `done`/`error`/`cancelled` (settle into history). `sendChat()` sends `board_chat {id, text}` on Enter; a prompt while a turn runs just queues — the row appears from the event, never a refusal.
4. **Queue UI**: rows in delivery order with × (`board_chat_queue_remove`) and move (`board_chat_queue_move`), the panes' queue-strip look; while a turn runs the send button becomes Stop (`board_chat_cancel`).
5. **Buttons**: move `m_cleanup` out of `buildListTools` into the panel's row (`layoutListTools` shrinks); beside it a **Check** button (unscoped `board_check`, findings in the same clickable list as triage) — the answer to "what other buttons belong down there". Clean up disables with a tooltip while a chat turn runs (the worker refuses it; the two are exclusive).
6. **Triage per section**: a hover ⚠ on each section header row (delegate-painted, like the priority flag) sends `board_check {section}`; findings render as a clickable list under the section (or in the notice area).
7. **Clickable problems**: `showProblems` links and triage findings pre-fill the composer with a fix note ("Fix #K7Q2: <message> — please fix this card.") and focus it — a draft, never sent; opening the file stays on `o`.
8. **Composer parity**: move `m_modelBox` into the composer row (rows and `onModelPick` unchanged — step 1 makes a pick reach a live conversation); the mic chip (`voice::Capture` + `transcribe`/`transcribed`, with the key-offer shape); the context-left label from `context` events.
9. **Survey**: on `board_survey`, mark the panel's opening turn; render the narration plus the proposals as an import row — checkboxes and an **Import N cards** button → `board_import_apply {keys}`; the `git` block's GitHub URL is a plain external link (link-out only, #GDQN/#ZKR0). A pending board surveys itself on first `board_open`; no GUI trigger needed (`board_chat {survey:true}` stays the manual path).
10. **Hints, theme, docs**: shortcut hints for the new fast paths (composer focus, triage) through the existing registry with live Keymap text; qss for the new object names beside `boardCleanup`; a line in `docs/ARCHITECTURE.md`'s Switchboard section.

**Risks**

- *Panel placement* is the one visible design choice: pinned to the bottom of the list page, the conversation collapsed to the last exchange when idle and expanding while streaming. If you want it fully collapsible or hidden until addressed, say so on the card.
- Step 1 is the only worker change; the alternative — the picker sending `board_chat {model: <role>}` per conversation — would split model picking into two mechanisms and is not taken.
- A scoped `board_check` leaves out problems that belong to no single section (worker rule), so a triage click can honestly say "nothing" while the banner still lists board-level problems.
- The mic wiring duplicates ~40 lines of `Pane.h` logic; extract a shared helper only if it stays awkward.

**Verify**

- `ctest --test-dir build -R boardmodel` — extend `tests/boardmodel_test.cpp`: the panel seeds from the `board` event's `chat` block; a prompt sends `board_chat`; a second prompt while running shows a queue row and sends nothing extra; × and move send the queue ops; a problem click pre-fills the composer without sending; the survey's import button sends `board_import_apply {keys}`; the context chip follows a `context` event; Clean up lives in the panel row.
- `python3 -m pytest tests/test_board_chat.py -q` (and `test_board_protocol.py`; its ForgeSync failure is #GDQN's in-flight work, not ours).
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`, against a **copy** of a fixture board (never the real tracker), evidence in `docs/qa_evidence/2026-09-20-switchboard-page-agent/`: a streaming answer, a queued second prompt with its row, Stop, triage on one section, a problem click pre-filling the draft, the mic with a key, the moved Clean up, and the survey on a board created through the picker's "Initialize new project here" (#916B's path) — import ticked, cards created, `survey-state.json` settled to done.
- Build through `scripts/relay-build`; land with `scripts/land.py begin/commit`; the card moves to `needs-verification` with the evidence path and a QA checklist in the same commit.

## QA checklist

Evidence: `docs/qa_evidence/2026-09-20-switchboard-page-agent/` (live Xvfb run, isolated
`XDG_*`/`TMPDIR` under `/tmp/q8`, `RELAY_KEYRING=off`, a stub provider, against a **copy** of a
fixture board — the real `issues/` was never opened). Read its README first: it is the
implementer's own run, it found three defects, and all three were fixed in `0c9cd894` **after**
the screenshots were taken. The shots marked `-BUG` are the record of the defect, not of the
build you are checking.

- [x] `ctest --test-dir build -R '^board$'` — 73 pass. Three fail (`executeHandsTheCardToAPane…`,
      `theExecuteTaskCarriesTheBoardsConventions`, `theVerifyTaskIsTheQaChecklist…`) and one
      `boardsections` case fails: **all four fail identically on a clean `HEAD` export** built in
      `/tmp`, so they are not this card's. Worth someone's card — the Execute/Verify brief text
      has drifted from its assertions.
- [x] `python3 -m unittest test_board_chat` (from `tests/`, with `backend` and `tests` on
      `PYTHONPATH`; there is no pytest on this machine) — 45 pass.
- [x] `scripts/relay-build` clean, and every commit went through `land.py`'s build gate, which
      compiles the exact tree it puts on `main`.
- [ ] **Re-run the three fixed defects against the current build** — this is the main thing asked
      of the verifier, because the evidence predates the fixes:
      1. Initialize a project with no board (picker → "Initialize new project here"). The page
         agent's panel must be **on screen** on the empty board, and the survey's proposals and
         Import button with it. Before `0c9cd894` the panel was absent and the survey was
         unreachable — and the marker is one-shot, so it was never offered again.
      2. Press **`a`** on the board: the composer takes the keyboard. (`Ctrl+/` is
         `help.shortcuts` and opens the Actions palette — that is correct, not a regression.)
      3. The microphone chip in the composer row is a **microphone**, not a box.
- [ ] Queueing: a second prompt typed while a turn runs is accepted and appears as a queue row;
      when it starts, the row goes at once (not when the turn ends). × and ▲▼ send
      `board_chat_queue_remove` / `_move`. Stop ends the turn and the queue survives.
- [ ] A running answer is drawn as **one** `✦ AGENT` block, including while a second prompt is
      queued behind it.
- [ ] Check and a section header's ⚠: findings listed, and a click puts a fix request **in the
      composer without sending it** (owner: "draft you confirm"). The problems banner does the
      same. A scoped check must not rewrite the banner.
- [ ] Clean up is in the panel's button row and not in the filter row, and still previews first.
- [ ] Composer parity: model box, microphone and context-left chip in the one row (the box came
      with #BRD3's `0a7a43d7`); a pick still reaches a **live** conversation — change the model
      mid-conversation and check the next turn answers on the new one (`6a541562`).
- [ ] The survey's "Look for issues on GitHub" on a real GitHub repo: it reports what a sync would
      do and says nothing was written. It must **never** send `forge_sync_run` — that is #ZKR0.
      With no credential it should say so rather than show a raw error.
- [ ] Nothing of the page agent reaches an open card's thread.

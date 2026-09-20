# Switchboard page agent — live QA evidence (#8YQ9, 2026-09-20)

QA of the panel that landed in `6a541562` and `2d0d4279` (protocol `docs/AGENT-SESSIONS-PROTOCOL.md`
19.18; `src/BoardChat.{h,cpp}`, `BoardView::buildChatPanel` / `handleChatEvent` / `requestCheck` /
`showProblems` in `src/BoardPane.cpp`). Nothing here was built or edited: the binary under test is
`build/relay`, **build id `2026-09-19.23H.06`**, which is the first relink after `2d0d4279`
(`logs/relay-gui.log`, first line). Eight of the ten asked-for items came out as specified, one is
blocked by a keybinding conflict and one needed a workaround; three bugs are written up at the
bottom and none of them was fixed here.

## What was run

`drive.sh` (in this directory) does the scriptable half and leaves Relay up; the button clicks were
then driven by hand from the same shell, because the panel's controls move down the pane as the
queue and the findings list grow and a blind click lands on the card list instead — which is exactly
what happened on the first take (`n` reached the list and quick-added a card; that card and its
thread were deleted and the run restarted with a fresh worker).

```
docs/qa_evidence/2026-09-20-switchboard-page-agent/drive.sh
```

Environment, all of it isolated and all of it under a **short** path (`/tmp/q8/…`, because the
worker's Unix socket has to fit in 108 bytes):

```
HOME=/tmp/q8/home  XDG_CONFIG_HOME=$HOME/.config  XDG_DATA_HOME=$HOME/.local/share
XDG_CACHE_HOME=$HOME/.cache  XDG_RUNTIME_DIR=/tmp/q8/run  TMPDIR=/tmp/q8/tmp
DISPLAY=:170 (Xvfb, 1600x1080x24)  RELAY_KEYRING=off  RELAY_SESSION=qa8yq9
build/relay --workspace /tmp/q8/home/proj
```

No provider account and no key anywhere: `relay.conf` sets `provider/preset=local:stub` and
`relay/local-models.json` points `local:stub` at `stub-provider.py` on `127.0.0.1:8823`. The
`switchboard` role defaults to the main agent's tier (`relay_core/roles.py`, `ROLE_TIERS`), so the
board worker's page agent answers on the stub — `worker.log` shows every page-agent turn as
`pane=switchboard … model=stub host=127.0.0.1`.

**The real board at `issues/` was never touched.** Everything ran against a fixture board that
`drive.sh` writes at `/tmp/q8/home/proj/.switchboard/`: five well-formed cards and two deliberately
malformed ones —

* `2026-09-20-broken-card-with-no-id.md`, front matter with no `id`. `board.check()` reports
  `missing_id` (error). Because `BoardCommands._problem_section` skips a card with no id, this
  problem belongs to **no section**, so only the unscoped Check button and the banner over the list
  report it — which is the behaviour 19.18 specifies.
* `2026-09-20-stray-front-matter-field.md`, `id: N2M3`, in **Executing**, with an `urgency:` key
  the card format does not define → `unknown_field` (error), and it *does* belong to a section, so
  the Executing header's ⚠ has exactly one thing to find.

`fixture-check.json` is `scripts/relay-board.py check --json` over that board, for comparison with
what the panel showed.

### The stub's one gotcha, re-learned

`stub-provider.py` keys its scenes on **whole phrases out of the typed prompt**, not on single
words and not on "the last user message". The recorded gotcha (the worker appends user-role
messages of its own at the end of a turn) is real, but there is a second one on this surface: the
page agent's *first* prompt is the whole-board roster plus the brief (`board_chat.chat_prompt`), so
a one-word key like `label` matches the roster's `· labels -` column and picks the wrong scene. The
first take answered the duplicates question with the labels answer for exactly that reason.

## The screenshots

Every shot is a crop of the Switchboard pane out of the full window. `notes-ocr.txt` is tesseract
over each one.

| Shot | What it shows |
| --- | --- |
| `01-panel-buttons.png` | **Item 1.** The list page with the page-agent panel pinned under it: the head row `Switchboard agent`, the button row with **Check** and **Clean up**, the conversation log with its empty-state line, and the composer (`Ask about the board — Enter sends, a second prompt queues`) with the mic chip and Send. The filter row above holds only the filter box, the model box and `+ New card (n)` — **neither Check nor Clean up is in it**. The problems banner `⚠ 2 problems · 2026-09-20-broken-card-with-no-id.md · no id in front matter` sits between the two. |
| `02-answer-streaming.png` | **Item 2.** `which cards are duplicates of each other?` sent; the answer streaming into the log under `✦ AGENT`, the head clock at `0:06`, the busy strip (`✦ Switchboard agent · Requesting …`) and the Send button reading **Stop**. |
| `03-second-prompt-queued.png` | **Item 3.** `then sort the inbox by rank` typed while the first turn was still running: **accepted, not refused**, and shown as `1 prompt waiting` / `1. then sort the inbox by rank` under the conversation. The status bar said `Queued — it starts when this turn ends.` |
| `04-send-reads-stop.png` | **Item 4a.** A third prompt queued: `2 prompts waiting`, each row with ▲ ▼ and ×, and the Send button reading **Stop** (the busy strip's `✕ Stop` beside it). |
| `05-stopped-queue-carries-on.png` | **Item 4b.** Stop clicked. The cancelled turn is folded into the history with `(cancelled)` appended to what it had written; the queue survived and the next prompt has already started (`Sorted by rank in my head: …`). `worker.log`: `turn=chat-0c0809 outcome=cancelled ms=69409`, then `chat-41c924` and `chat-f499b8` in order. |
| `05b-queue-stale-while-draining.png` | The same conversation a few seconds later: both queued prompts ran in order, the queue box is gone, the context chip reads `93% left`. |
| `06-check-findings.png` | **Item 5.** The panel's **Check** clicked: `Check: 2 problems` with both findings as clickable lines — `error · 2026-09-20-broken-card-with-no-id.md · no id in front matter` and `error · 2026-09-20-stray-front-matter-field.md · unknown front matter field(s) for a work card: urgency`. Matches `fixture-check.json` exactly. |
| `07-executing-header-hover-triage.png` | **Item 6a.** The EXECUTING header hovered: the ⚠ and the `+` appear at its right end (a header at rest is just a name). |
| `07b-triage-mark-zoom.png` | The same header row at 200 %, so the ⚠ (left of the `+`) is legible. |
| `08-triage-findings-scoped.png` | **Item 6b.** That ⚠ clicked: `Triage · Executing: 1 problem`, and only the Executing card's problem. The `missing_id` card is correctly absent — it belongs to no section. |
| `09-finding-drafts-into-composer.png` | **Item 7.** The finding clicked. `Fix features/2026-09-20-stray-front-matter-field.md: unknown front matter field(s) for a work card: urgency — please fix this card.` is **sitting in the composer**; the button still reads **Send**, the log has no new `YOU` row and no turn started. A draft the owner confirms, as decided. |
| `10-composer-mic-and-context-chip.png` | **Item 9.** The composer row at 170 %: the microphone chip, the `93% left` context chip (drawn from a `context` event tagged `chat: true`), and Send. |
| `10b-mic-chip-tofu-BUG.png` | The mic button at 500 % — it is a **missing-glyph box**, not a microphone. See bug 3. |
| `11-project-picker-init-here.png` | **Item 10a.** The project picker (#916B) in a second tab standing in `/tmp/q8/home/newproj`, with `＋ Initialize new project here ~/newproj` selected. |
| `12-fresh-board-panel-hidden-BUG.png` | **Item 10b, and bug 1.** What the Switchboard looks like straight after that: the board was created (`Switchboard created in /tmp/q8/home/newproj/.switchboard/ · git repository initialized`) and the survey **did** run — `worker.log` `turn=chat-2a360a … prompt_chars=2023 … outcome=done ms=7535`, and `survey-state.json` settled to `done` — but the page is the empty-board placeholder and **the panel is not on screen at all**. Nothing of the survey was visible. |
| `13-survey-proposals-and-import.png` | **Item 10c**, after the workaround below. `Survey · newproj` · `Found 2 items in 1 tracker already in this project.` · `An import would create 2 cards. Untick anything Relay should leave where it is:` with both proposals as **ticked checkboxes** (`Write the README — checklist: TODO.md`, `Add a licence header to every source file — checklist: TODO.md`) and the **`Import 2 cards`** button, with the agent's survey narration streaming into the log below it. |
| `14-one-proposal-ticked.png` | One proposal unticked; the button follows and reads **`Import 1 card`**. |
| `15-imported-card-created.png` | Import clicked: `#51N8 Write the README` in Discussing, labelled `imported` `checklist` `todo`, and the label chips row has appeared on the board. Only the ticked item was imported. `15-imported-card.md` is the file it wrote (it carries `source: 'relay-import: "checklist:TODO.md#ffdb67f5" line 3'`), and `15-survey-state-done.json` is the marker settled to `done`. |
| `16-ctrl-slash-opens-actions-BUG.png` | **Item 8 — not reachable.** With a card row selected in the Switchboard, Ctrl+/ opened the **Actions** pane; the toast in the shot reads "… every action and its keys. You pressed Ctrl+/." The composer was not focused. See bug 2. |

### The survey: which path, and why

The card asked for the survey through the picker's "Initialize new project here", and that path was
taken (`11-…`): a second tab, `cd /tmp/q8/home/newproj`, Ctrl+Shift+A → `Projects` → Enter →
`Initialize new project here`. It worked — the board was created, `board_init` left
`survey-state.json` at `pending`, the first `board_open` ran the survey turn, and the marker settled
to `done`. **But none of it was visible**, because a freshly created board has no cards and the
empty-board placeholder hides the whole splitter, panel included (bug 1).

So the survey's *content* was captured with the documented fallback, on the same board: one card
file was written into `/tmp/q8/home/newproj/.switchboard/features/` so the board was not empty,
`survey-state.json` was reset to `{"version": 1, "state": "pending"}`, and the Switchboard was
closed and reopened (which restarts that board's worker, so `board_open` surveys again). That is
shots 13–15. The seed card `#S4TY Seed card so the board is not empty` in those shots is the QA
run's, not the survey's.

## Logs kept

* `logs/relay-gui.log` — the GUI's own log: `gui_start … build=2026-09-19.23H.06`, the worker
  starts, and `board_state … board=/tmp/q8/home/proj/.switchboard state=ready`.
* `logs/worker.log` — every page-agent turn of the run, in order: the mis-keyed first take, the
  fresh conversation, the cancelled turn and the two queued ones draining behind it
  (`outcome=cancelled ms=69409` → `chat-41c924` → `chat-f499b8`), and the two survey turns on
  `newproj` (`prompt_chars=2023`, `outcome=done`). `worker-faults.log` stayed empty; there were no
  crashes and `relay.log` (stderr) was empty, so it is not kept.
* `fixture-check.json` — `scripts/relay-board.py check` over the fixture board, the ground truth
  the Check and triage findings are compared against.

## What was fixed afterwards (all three bugs, same session)

This run was the implementer's own, and everything it found was fixed before the card moved on.
The screenshots below are kept **as they were taken** — they are the record of the defect, not of
the current build — so read them with this:

| Bug | Fixed in | What changed |
|---|---|---|
| 1. A freshly created board hid the panel, so the survey could never be seen | `0c9cd894` | The panel was built inside the splitter, which `rebuild()` hides when the board has no cards — and a board with no cards is the only board that gets a survey. It now sits below the splitter and owns its own visibility. |
| 2. Ctrl+/ opened the Actions palette instead of focusing the composer | `0c9cd894` | `help.shortcuts` owns Ctrl+/ in `src/Keymap.h` and the window's event filter accepts it first. The composer key is now **`a`** — a bare letter the board itself handles, as `/` is the filter — and the key line teaches it. |
| 3. The microphone chip was a missing-glyph box | `0c9cd894` | It was the emoji U+1F3A4, which the UI font has no glyph for. It is drawn now: four QPainter strokes, no font and no asset, taking the recording colour itself. |

The two smaller observations in "What did not work" were fixed in the same commit: a running turn
is drawn once rather than splitting into two `✦ AGENT` blocks, and the drain announces itself so a
prompt that has started is no longer still listed as queued.

A re-run of items 8, 10 and the mic against the fixed build has **not** been done — that is the
verifier's, and the QA checklist on #8YQ9 asks for it by name.

## What did not work

Three bugs. None was fixed here (this run was evidence only); each is written up with the smallest
reproduction.

### Bug 1 — a freshly created board hides the page agent, so the survey is invisible

`BoardView::rebuild()` does `m_splitter->setVisible(m_open && !empty)` when the board holds no
cards (`src/BoardPane.cpp:3978` at `2d0d4279`), and the chat panel lives *inside* the splitter:
`buildChatPanel(listLayout)` adds it to `m_listPane` (line 2641) and `m_splitter->addWidget(m_listPane)`
is the next line (2642). So on an empty
board the composer, Check, Clean up and the survey's proposals-and-Import block are all off screen.

The board that is *always* empty is the board that was just created — which is the only kind of
board the survey ever runs on. The survey therefore runs, streams, settles `survey-state.json` to
`done`, and can never be seen or answered; because the marker is one-shot, reopening the board does
not offer it again either.

Smallest reproduction: in a directory with no board, initialize one (project picker →
"Initialize new project here", or the init question). The Switchboard opens on "No cards yet." with
no panel. `<board>/survey-state.json` reads `{"state": "done"}` and `worker.log` shows the survey
turn ran. Evidence: `12-fresh-board-panel-hidden-BUG.png`, `logs/worker.log` (turn `chat-2a360a`).

### Bug 2 — Ctrl+/ opens the Actions palette instead of focusing the composer

`help.shortcuts` is bound to `Ctrl+/` among its spellings (`src/Keymap.h:371`:
`{"Ctrl+?", "Ctrl+Shift+/", "Ctrl+/", "F1"}`), and the window's `eventFilter`
(`src/RelayWindow.h:775`; all line numbers here are as of `2d0d4279`) matches the keymap on `ShortcutOverride`/`KeyPress` and accepts the event
before it can reach a pane. `BoardView::handleBoardKey`'s `Ctrl+/` (`src/BoardPane.cpp:4690-4697`,
and the promise in `src/BoardPane.h:132`) therefore never runs.

Smallest reproduction: open the Switchboard, click a card row, press Ctrl+/. The Actions pane opens
and the shortcut toast says "You pressed Ctrl+/". Evidence:
`16-ctrl-slash-opens-actions-BUG.png`.

Whose call the fix is: it is either the keymap's (drop `Ctrl+/` from `help.shortcuts`, keeping
`Ctrl+?`, `Ctrl+Shift+/` and `F1`) or the board's (give the panel a real `board.focusChat` action in
the Keymap so the window-level dispatcher runs it) — both touch files outside this run's remit
(`src/Keymap.h`, and `src/BoardPane.*` which another session is editing right now).

### Bug 3 — the microphone chip renders as a missing-glyph box

`m_mic->setText("\U0001F3A4")` (`src/BoardChat.cpp`, the composer row) draws as an empty rectangle.
`fc-list :charset=1F3A4` on this machine lists `Noto Color Emoji`, so the font is installed; Qt is
not falling back to it for that button. The terminal pane's own mic, which is an SVG from
`theme::themeDataDir()`, renders correctly two panes away in the same screenshots. The code comment
anticipates this ("The stylesheet can still give `#boardChatMic` an icon of its own"). Cosmetic, but
it is the first thing in the composer row.

Smallest reproduction: open the Switchboard and look at the button between the composer and Send.
Evidence: `10-composer-mic-and-context-chip.png`, `10b-mic-chip-tofu-BUG.png`.

### Two smaller observations, not filed as bugs

* **A streaming answer is split into two `✦ AGENT` blocks the moment a second prompt is queued.**
  `PageAgent._collect` appends each delta into `self.history` *while the turn runs*
  (`backend/relay_core/board_chat.py:439`), so the `chat` block that rides on `board_chat_state` — which is
  exactly what queueing sends — already contains the partial answer. `setChatState` then clears
  `m_streamed` because "the block is the better copy" (`src/BoardChat.cpp`), which is right, but
  `rebuildLog` still draws the live `✦ AGENT` header for the running turn, so the answer continues
  under a second header mid-sentence. Visible in `03-second-prompt-queued.png` and
  `05-stopped-queue-carries-on.png`. Nothing is lost or duplicated — it reads as two agent
  messages where there was one.
* **While the queue drains, the prompt that is already running is still listed in the queue.**
  `PageAgent._run` announces state *before* `_drain()` pops the next item, and `_drain` calls
  `_start` directly rather than through `ask`, so no further state goes out until that turn ends.
  In `05-stopped-queue-carries-on.png` the panel shows `2 prompts waiting` with `1. then sort the
  inbox by rank` while that same prompt is the turn running above it. It corrects itself at the end
  of each turn (`05b-…`).

## Files

```
README.md                              this
drive.sh                               the harness (fixtures, stub, Xvfb, Relay, the scripted half)
stub-provider.py                       loopback OpenAI-compatible endpoint, phrase-keyed scenes
01-panel-buttons.png                   item 1
02-answer-streaming.png                item 2
03-second-prompt-queued.png            item 3
04-send-reads-stop.png                 item 4 (Send reads Stop, two queued)
05-stopped-queue-carries-on.png        item 4 (stopped; queue survived)
05b-queue-stale-while-draining.png     the queue drained, context chip
06-check-findings.png                  item 5
07-executing-header-hover-triage.png   item 6 (the ⚠ on hover)
07b-triage-mark-zoom.png               the same header row at 200 %
08-triage-findings-scoped.png          item 6 (findings scoped to Executing)
09-finding-drafts-into-composer.png    item 7 (drafted, not sent)
10-composer-mic-and-context-chip.png   item 9
10b-mic-chip-tofu-BUG.png              bug 3
11-project-picker-init-here.png        item 10 (the picker path)
12-fresh-board-panel-hidden-BUG.png    bug 1
13-survey-proposals-and-import.png     item 10 (proposals ticked, Import button)
14-one-proposal-ticked.png             item 10 (one ticked → "Import 1 card")
15-imported-card-created.png           item 10 (the card it created)
15-imported-card.md                    the card file, with its `source` key
15-survey-state-done.json              the marker settled to `done`
16-ctrl-slash-opens-actions-BUG.png    bug 2 (item 8 not reachable)
notes-ocr.txt                          tesseract over every shot
fixture-check.json                     relay-board.py check over the fixture board
logs/relay-gui.log                     the GUI log (build id, workers, board_state)
logs/worker.log                        every page-agent turn of the run
```

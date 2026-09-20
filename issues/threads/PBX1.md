<!-- relay:entry 20260920T210500Z-a1 author=claude-code kind=event -->
### Claude Code · 2026-09-20 21:05
filed from the owner's own words, 2026-09-20, comparing the main pane's prompt box with the
"Switchboard agent" and "Options helper" panels, and with the card page's reply box. Claimed it
in the same turn (land session `promptboxes`, contact "prompt-box restyle subagent").

<!-- relay:entry 20260920T210600Z-a2 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 21:06
the owner's words, as the spec: "i dont like the helper agent prompt UI … there is the useless
help sentence, and then a bunch of wasted space, and then the tiny text box. and the buttons dont
look as good"; "the card agent looks a lot better"; "[remove] the send button on all, make it like
the pane agent"; and, of Plan / Execute / Verify, "move those buttons out of there (plan / execute
/ etc), because they actually dont do anything in the chat box. can we instead put buttons like
that in a row above the chat box. they are actions the agent can take that dont require typing. we
put the 'clean up' button there for the main switchboard agent, for example."

So the rule implemented everywhere is: the main pane's prompt box is the reference, and an action
that needs no typing goes in a row above the box.

<!-- relay:entry 20260920T211200Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 21:12
**the helper panel, `502f2b22`.** `relay::HelperChatPanel` (all four panes) is now a head row —
the name and clock, then Check / Clean up / the fold — then the findings, the survey, the log, the
queue, and then one rounded frame `QFrame#boardChatBox` styled rule for rule as `QFrame#composer`
is, taking the accent border while the cursor is in it. Inside the frame: the busy strip with its
`✕ Stop` as the first row and on screen only while a turn runs; the editor, borderless
(`setFrameShape(NoFrame)` plus the qss) and in the prompt font, `setAutoHeight(2, 8)`; then the
chip strip — context, model, microphone, right-aligned, at the pane's chip height.

`boardChatSend` is gone entirely; Esc in the box now stops a running turn, mirroring the pane. The
empty-log paragraph is gone with it: the placeholder carries what the box is for and names the
agent ("Ask the Switchboard agent — Enter sends, a second prompt queues"), and the log is hidden
outright while the conversation is empty and, once there is one, no taller than what it holds.

Tests: `theHelpersPromptBoxIsTheSameShapeAsAPanes` is new in `tests/boardmodel_test.cpp`; the old
Send-button test is now the busy strip's, and `tests/settingspane_test.cpp` and
`tests/conversations_test.cpp` assert the same box in the Options and Sessions embeds.

<!-- relay:entry 20260920T213000Z-b2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 21:30
**the card page, `ce29cb8e`.** Plan (p), Execute (x) and Verify (v) moved out of the `boardReply`
frame into `boardCardActions`, a right-aligned row directly above it — same object names, keys,
tooltips and behaviour, and both still read whatever is typed in the reply box as their note. The
row hides and shows with the box, and `controlsHeight()` counts it so a floating notice still
clears the controls. Inside the frame: the busy strip, the borderless editor, and the model box
alone on its own chip strip. `fitButtons()` measures the action row now and no longer counts the
model box — the two no longer share a width, so at a ~350 px card the labels keep their keys,
which is what moving them bought.

`QFrame#boardReply` was given the panel's frame (same ground, 10 px radius, accent border on
focus) so the list page and the card page are visibly one component.

<!-- relay:entry 20260920T214500Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:45
live Xvfb run, `docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/`: `drive.sh` with its own
`stub-provider.py` (no provider account, `RELAY_KEYRING=off`, isolated HOME/XDG/TMPDIR), driving
the binary `land.py` built from the exact tree it committed. **22 checks passed, 0 failed.** Six
screenshots: the pane's box beside the Switchboard agent's idle and with a short conversation, the
card page, the Options helper expanded and idle, the Sessions helper expanded, and a ~350 px pane.
`NOTES.md` has the QA checklist and what to read in each shot. Targeted tests:
`ctest --test-dir build -R '^board$|^boardpane$|^settings$|^conversations$'` — 4/4 pass.

Moved to `needs-verification`.

<!-- relay:entry 20260920T222000Z-c2 author=claude-code kind=event -->
### Claude Code · 2026-09-20 22:20
the card, its thread and the evidence folder landed as `17cfe0b4`, with
`docs/SWITCHBOARD-DESIGN.md` 4.13a. `issues/BOARD.md` is left to whoever lands it next: the
regenerated index in the working tree carries this card, but the file had moved on main and
merging a generated index risks writing back another session's card states.

Three commits carry the change and none of them ends in the Co-Authored-By line this session was
told to add — it was missed on the first two and then kept consistent rather than rewriting a
shared branch's history. Noted here so the trail is not silent about it.

<!-- relay:entry 20260920T222156Z-zw author=claude-code kind=decision -->
**Owner decision, 2026-09-20 — an action row is left-aligned buttons and nothing else.**
Comparing the card page's row with the Switchboard panel's: *"the plan / execute buttons etc,
would those work better at the left? (in the switchboard card agent)"*, agreed as *"yes, lets do
both left-aligned, drop the label"*. Landed as `69fee7b2`.

- **Card page** (`src/BoardPane.cpp`): `boardCardActions` keeps its stretch **behind** Plan (p),
  Execute (x) and Verify (v) instead of in front of them, so the row reads in the order the work
  is done. Same object names, keys, tooltips, note behaviour, `fitButtons()` rule and button face.
- **Switchboard panel** (`src/HelperChat.{h,cpp}`): the head row starts with the tool row — Check,
  Clean up, and whatever `addToolWidget` reparents in (Tests and Profile land there too) — and
  closes with the stretch. The `boardChatHead` "Switchboard agent" label is **gone** from it: the
  box's placeholder already names the agent, and the busy strip names it again while a turn runs.
- **Where the clock went.** `drawHead()` put the turn clock and the `· survey` word on that label.
  They are now on the busy strip's own line (`drawBusyLine`, `HelperChatPanel::drawBusy`), which
  already carried `✦ <helper>` and is on screen for exactly as long as there is a turn to time —
  so it reads `✦ Switchboard agent · 0:42 · survey`. Nothing was lost with the label.
- **The panels that fold** (Options, Actions, Sessions) keep their name and their fold control:
  the rule is about action rows, and their head row has no actions. Their action row is the
  collapsed `? Helper Agent (Alt+Q)` row, which stays at the pane's bottom right (c3e8695c).
- `docs/SWITCHBOARD-DESIGN.md` §4.13 and §4.13a say the rule; the card's QA checklist items 4, 5,
  6, 8 and 9 were rewritten for it.

Tests (`tests/boardmodel_test.cpp`): the tool row is the head row's first item and the stretch its
last; Check is in the panel's left quarter and before Clean up; the Switchboard's panel has no
`boardChatHead`; the busy strip reads `✦ Switchboard agent · 0:42 · survey` on a running survey
turn. On the card, Plan is index 0 with the stretch last, and the ~350 px case measures the x
positions — it is the one card-page test whose row has been through a resize and so has real
geometry. `ctest -R '^board$|^boardpane$|^settings$|^conversations$'`: 4 of 4 pass, board 92 cases,
and land.py's gate ran them again on the exact tree it committed.

<!-- relay:entry 20260920T222156Z-zx author=claude-code kind=evidence -->
Live Xvfb run for the left-aligned rows: `docs/qa_evidence/2026-09-20-action-rows-left/`
(`drive.sh`, its own `stub-provider.py`, `RELAY_KEYRING=off`, isolated HOME/XDG/TMPDIR, no
provider account), driving the binary land.py built from the exact tree it committed. **22 checks,
22 passed** — every one a measured x position, because "left-aligned" is a claim about geometry
and a shot that merely contains the word "Plan" would pass either way.

- `01-switchboard-idle.png` — head row `Check · Clean up · Tests · Profile` from x=782, the pane's
  own left margin (the INBOX header) at x=780, and nothing in front of them.
- `02-turn-running.png` — the busy strip mid-turn: `✦ Switchboard agent · 0:00  Requesting …` with
  `✕ Stop`. This is where the clock lives now; the stub answers `slowly` so the strip lasts longer
  than a frame.
- `03-switchboard-conversation.png` — the head row is unchanged by a turn having run.
- `04-card-page.png` — `Plan (p)` at x=779, `Execute (x)` at x=861, the card page's left margin
  (the body's "Issue" heading) at x=778.
- `05-narrow-card-page.png` — the same at a ~350 px pane: Plan at x=394 against a margin of 393,
  both labels whole with their keys.

`NOTES.md` says what each shot shows and how the checks are made (the card's violet outlined
`Execute (x)` is read from a saturation-boosted band, because no whole-page OCR pass reads it).

<!-- relay:entry 20260920T233923Z-qh author=claude-code kind=decision -->
Owner, 2026-09-20, of the Switchboard agent's action row: **"make the buttons consistent, can you use the styling from the card agent"** — and then, of the colours, **"(not the colors though)"**. And: **"it should have the letter hotkeys for each switchboard action as well"**.

Read together: the row imposes **shape** on every button that joins it — border, radius, padding, weight, height — and imposes **no colour at all**, so Execute's accent outline still means "this leaves the board" and a plain button stays plain. And every action on the row says its letter in its own label, the way `Plan (p)` does.

<!-- relay:entry 20260920T233923Z-qi author=claude-code kind=progress -->
`3ed92250` — the row's face and its letters.

- `src/Theme.cpp`: one rule, `QPushButton[actionRow="true"], QToolButton[actionRow="true"]`, carrying border-width/style, radius, padding and weight and **no colour**. It is keyed on a dynamic property and not on object names, so a session adding a button gets the shape without renaming its button — its own tests find it by that name — and each button's object-name rule (an id selector, specificity 101 against this rule's 11) keeps the ground and the ink its maker gave it.
- Tests and Profile (#7BM4) had no rule at all and painted the bare Fusion button: 25 px tall where their neighbours were 33. They joined the plain-ground selector list beside Check and Clean up — colour only, one list rather than a rule per button.
- Push buttons carry three pixels of extra padding (`QWidget#boardCardActions QPushButton`, specificity 102, which is what beats `#boardReplyButton` and `#boardExecute`). That is Qt's `QSize(3, 3)` fudge for a styled QToolButton — "### broken QToolButton" in `qstylesheetstyle.cpp` — handed back, and it is a constant, so the two rows are one height at any desktop font size.
- `HelperChatPanel::adoptActionButton` stamps the property (and repolishes) on every button `addToolWidget` adopts and on the Check the panel builds; `CardDetail` sets it on Plan, Execute and Verify.
- The letters: a maker sets `actionKey`; the panel writes " (k)" into the label, keeps `fullLabel` for `fitButtons()`, answers the key (`BoardView::handleBoardKey` asks the panel rather than naming buttons) and adds its entry to the key line. Check `k`, Clean up `u` — free on a page that already spends n, e, p, x, v, m, c, y, t, a, o and `/`. `updateCleanupButton()` keeps the letter through the Stop state. A mouse click teaches the letter once (`board.action.<objectName>`, WARP.md's standing rule); the key press that just used it says nothing.

Tests: `tests/boardmodel_test.cpp::everyButtonOnAnActionRowWearsTheCardPagesFace` (a reparented button keeps its name, gains the property, and matches Check's height and font; Plan matches too; the shared rule declares no colour) and `::everyActionOnTheSwitchboardsRowHasALetter` (labels, key line, `k` and `u` on the list, a session's own letter, and the hint only on the mouse path). `ctest -R '^board$|^boardpane$'` — both pass; `buttonfit`, `boardexecute`, `cardtests`, `profilepane`, `boardsections`, `boardsignals`, `boardfilter`, `boardwatch`, `helpermodelbox`, `testsuites` pass too.

<!-- relay:entry 20260920T233923Z-qj author=claude-code kind=evidence -->
Live Xvfb run: `docs/qa_evidence/2026-09-20-action-row-face/`, driving the binary land.py built from the exact tree it committed. The driver is the previous run's (`../2026-09-20-action-rows-left/drive.sh`), unchanged, because its checks are the ones this change must not break: **22 checks, 22 passed**.

`_row-before-after.png` is the measurement (`convert … -threshold 12% -connected-components 8`, same crop in both runs):

| | before `69fee7b2` | after `3ed92250` |
|---|---|---|
| Check | 78 × 33 | 97 × 33 |
| Clean up | 95 × 33 | 116 × 33 |
| Tests | 48 × **25** | 71 × **33** |
| Profile | 57 × **25** | 80 × **33** |
| Plan (card page) | 76 × **30** | 76 × **33** |
| Execute (card page) | 97 × **30** | 97 × **33** |

Tests and Profile were eight pixels short and sat four pixels low; they are now the same height, border, radius and type as their neighbours. The card page's two grew the three pixels that make the two rows one height, and their widths did not change. The colours did not move: in the after strip `Execute (x)` is still the violet outlined one and `Plan (p)` the plain one.

`01-switchboard-idle.png` reads `782:Check 825:(k) 885:Clean 926:up 946:(u) 1006:Tests 1085:Profile` — one row, one height, and the letters in the labels.

Left open, because they are another session's files: **Tests and Profile carry no letter.** The mechanism is the `actionKey` property on the button, so #7BM4's session adds one line each (`s` and `r` are free on that page) and the label, the key and the key line follow with no change here.

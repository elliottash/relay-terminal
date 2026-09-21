# An option written from a console is announced, marked and reversible

Card #AGNT, the QA item **"An agent's option write is not announced"** — the last thing the
integration drive of 2026-09-21 could not close. Its report was:

> after an `app_option_set` that **reaches the setting** (`relay.conf: copy_on_select=true`), the
> only rows are the two turns' own "Agent finished" — no "Agent changed Copy on select · Undo",
> and no "changed by the agent" on the row. `src/AppCommands.cpp:829-833` posts both
> unconditionally once the write lands, so something between them is swallowing them.

Nothing was swallowing them. Both were posted, and both were on screen — one of them outside the
rectangle it was drawn in.

## What was actually wrong

`NotificationsPopup::rebuild()` (`src/WindowChrome.h`) sized the list's viewport at
`min(6, count) * 56` pixels. 56 px is what an entry with a **one-line body** measures; an entry
whose body wraps is nearer ninety, and one that carries an offer — "Agent changed Copy on select
· off → on · [Undo]" — is over a hundred. So with two entries in the list the second was already
half outside the viewport, and with three the third was outside it altogether: posted, laid out,
drawn, and clipped away under the bottom edge, with its Undo button unreachable.

The drive's own two turns each post an "Agent finished" of their own, so by the time the bell was
opened the change's entry was the third. That is the whole of "no notification appeared".

Measured, on the build before the fix (the rectangles come from `RELAY_QA_RECTS`, which reports
what a widget *is*, not what is visible):

```
notificationsScroll    y =  90 … 202      (the viewport: 112 px for min(6,2)*56)
notificationRow        y =  90 … 179      "Agent finished"
notificationRow#2      y = 183 … 268      "Agent changed Copy on select"   ← 66 px of it drawn
popupTextButton#2      y = 234 … 261      "Undo"                          ← entirely outside
```

The row's marker was never missing either: with the Terminal page of Options open, the row reads
"changed by the agent just now: off → on". The earlier drive read its marker check off
`c07-revealed.png`, in which the Options pane is still on its **General** page — the `option:`
link it had clicked to get there had not moved it — so what the check matched was the words
"Copy on select" in the console's own transcript. Both halves of that check were reading the
wrong pixels.

## The fix

- `src/WindowChrome.h` — the list is as tall as its rows measure, clamped between 64 px and
  `kMaxListHeight`; past that it scrolls, which is what a scroll area is for. Rows being replaced
  leave the layout at once (`removeWidget` + `hide`) instead of waiting for `deleteLater`, so the
  height is measured from the rows this pass built.
- `src/AppCommands.cpp` — an **agent's own `app_undo`** now posts its own "Agent changed <row>:
  <before> → <after> · Undo" as well as amending the entry whose offer it answered. Before, the
  only trace of an agent putting a setting back was that amendment, which carries no offer at all
  — and if the person had dismissed the original entry, the agent's write was announced nowhere.
  A **person** pressing Undo still only amends, because the person did it and is looking at it.

Nothing changed in the route a console's `app_command` takes: it was right. One worker per tab,
the window answers that pipe once (`RelayWindow::boardWorker`, `who = "helper"`), and
`AppCommands::execute` marks the row and posts the notice for every agent in the window —
terminal pane, Switchboard, card, Options, Actions and Sessions alike.

## Running it

```
docs/qa_evidence/2026-09-21-console-write-undo/drive.sh [relay-binary] [out-dir]
```

Xvfb, a private `HOME`/`XDG_*`/`TMPDIR` under a short path, `RELAY_KEYRING=off`, and no provider
account: the profile points a local model endpoint at `stub-provider.py` on loopback, so the only
agent in the run is that script. It is the sibling drive's stub
(`../2026-09-21-agents-are-consoles/stub-provider.py`) cut down to the app-command scenes, with
the two it did not have — an action, and the agent taking its own change back.

`inside_list` is the check the bug needed: it reads the notification list's viewport and the
entry's Undo button out of `RELAY_QA_RECTS` and compares the two rectangles, so "the notice is
there" cannot again mean "the notice exists somewhere off the visible list".

## The shots

| | |
|---|---|
| `b01a-console.png` | Alt+Q expands the Options helper into a console |
| `b01-changed.png` | the console's answer: it says what it changed |
| `b02-row-marked.png` | Options › Terminal: the row reads "changed by the agent just now: off → on" |
| `b03-notice.png` | the bell's list: "Agent changed Copy on select · off → on · Undo", inside the viewport |
| `b04-undone.png` | after Undo: the setting is back and the row's marker is gone |
| `b05a-again.png` `b05b-reverted.png` | a second write, then the agent's own `app_undo` |
| `b05-agent-undo.png` | the agent's revert announced with its own Undo, above "Undone: Copy on select" |
| `b05c-row.png` | the row is still marked: an agent's undo is still an agent holding the row |
| `b06a-action.png` `b06-action.png` | `app_action_run` and "Agent ran Reload themes" |
| `before-fix-notice.png` | the same list on the build **before** the fix, from that repro run: "Agent changed Copy on select" clipped to its first line and no Undo anywhere |

`notes.txt` is the run's own PASS/FAIL list — **19 PASS, 0 FAIL** — `rects-last.json` the last
rectangle dump of the run, and `before-fix-rects.json` the one the table above is measured from
(the repro run that opened this investigation, driven the same way on the build
before the fix).

## Tests

`ctest --test-dir build -R appcommands` — `tests/appcommands_test.cpp` gained:

- `aConsolesWriteIsAnnouncedAndMarkedLikeAPanesOwn` — a `set_option` in the **tab worker's** wire
  shape (`event: "app_command"` with the fields beside it), executed with `who = "helper"`: the
  change log, the notice with its Undo, the row's marker, and the offer working from where the
  person finds it.
- `aConsolesActionIsAnnouncedToo` — the same pipe, "Agent ran Reload themes", no offer.
- `undoByHandClearsTheMarkAndUndoByTheAgentKeepsIt` — extended: the agent's own `app_undo` is
  announced with its own way back, and that way back works.

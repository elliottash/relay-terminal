# A link in a console's transcript, and the card page's frame — #AGNT's three finishing items

Card #AGNT's integration drive left three things. One of them turned out not to be a bug in
Relay at all, and the other two were both a border that nobody had measured.

- **(1) An `option:` link in a console's transcript does not reveal its row.** It does, in all
  four cases, and it always did. What the earlier drives clicked was not the link.
- **(2) The card page's reply box shows a faint second border.** Real. The second frame was the
  **console's own**, not `boardReply`'s.
- **(3) Plan (p) shows a bright outline at rest.** Real, and it is not a focus ring: the row's
  buttons already take no focus. `QToolButton[actionRow="true"]` declared no `border-color`, so
  Qt framed the button in its own ink.

Everything below was driven live under Xvfb against a loopback stub provider
(`drive.sh`, `stub-provider.py`), with `RELAY_KEYRING=off`, a private `HOME`/`XDG_*`/`TMPDIR`
and no provider account: **22 PASS · 0 FAIL** (`notes.txt`).

## (1) The link was never broken; the click was aimed at the label

`docs/qa_evidence/2026-09-21-agents-are-consoles/punch/c07-revealed.png` is the shot the finding
was written from: the Options helper answered with the link and the pane was still on **General**
afterwards. The answer on screen reads

```
It is in Terminal: OPENROW (option:terminal/copy_on_select). Clicking that opens the row.
```

which is what `[OPENROW](option:terminal/copy_on_select)` prints as. `MarkdownAnsi` paints the
**label** in the link colour — underlined dark green, `[ui] link`, the owner's 2026-09-19
"dark green, like Warp" — and then prints the target after it in the dim ink. The run the engine
underlines and a click opens is the **target**: `relay::links::candidates` finds
`option:<section>/<row>` in the transcript text and `relay::links::resolve` turns it into
`relay://option/terminal/copy_on_select`. The label is not a link and opens nothing.

The drive clicked the label. It clicked it on purpose — `stub-provider.py` says of that scene
"the link's label is a word that appears nowhere else on screen, so a driver that has to click it
cannot land on the same words in the prose" — so the miss was designed in, and both readings of
"the row was revealed" (the one that passed on the wrong words and the one that failed) were of a
click that had opened nothing at all.

Clicked on the target, the designed path runs exactly as written:

| | file:line |
|---|---|
| the engine reports the activated run | `engine/view/TerminalView.cpp:1476` → `Pane::onLinkActivated`, `src/Pane.h:10161` |
| the pane builds the `links::Target`, kind and all | `src/Pane.h:2761` `openOutputTarget()` |
| the context gets first refusal | `src/Pane.h:3164` `resolveContextLink()` → `src/Pane.h:2778` |
| the window's wrapper forwards it | `src/RelayWindow.h:7573` `TabConsoleContext::resolveLink` |
| Options reveals its own row | `src/SettingsPane.cpp:364` → `SettingsPane::revealOption`, `:545` |
| what a context refuses goes to the window | `src/Pane.h:2827` → `wireAgentConsole`, `src/RelayWindow.h:7637` |

Nothing in that chain is missing. `TabConsoleContext` forwards every virtual of
`relay::agent::Context` — `spec`, `actions`, `submit`, `resolveLink`, `turnFinished`,
`placeholder` — and both directions of `onChanged`; `boardworkspace`'s
`theWindowsWrapperForwardsEveryContextVirtual` reads the virtuals out of `src/AgentContext.h` and
fails if one of them is not forwarded, which is the test that would catch the next one.

Driven, four kinds, four consoles:

| | what was clicked | read |
|---|---|---|
| **a** | `option:terminal/copy_on_select` in the **Options** helper's own transcript | the pane reveals the row **in place**: the page's `settingsRowLabel`s go from General's (`Thinking display`, `Show tool output`, …) to Terminal's (`Copy on select`, `Colour paths and links in output`, …). `a02-link.png`, `a03-revealed.png` |
| **b** | the same link in the **Switchboard's** console, which is not Options | the window opens an Options pane at the row: same Terminal rows, no General ones. `b02-link.png`, `b03-options.png` |
| **c** | `session:2222…` in the **Sessions** helper's transcript | the list selects that conversation and the detail beside it names it — "The pane header and its labels", which the answer never says. `c02-link.png`, `c03-revealed.png` |
| **d** | `#<id>` in the **Switchboard's** console | the card page opens, on the card it named. `d02-link.png`, `d03-card.png` |

**How the gate reads the page.** "Copy on select" is in the console's answer whichever page is
showing, and reading it there is how this was got wrong twice
(`docs/qa_evidence/2026-09-21-console-write-undo/NOTES.md`). So the Options gates read
`RELAY_QA_RECTS`: every `settingsRowLabel` on screen is a row of the page that is **drawn**, and
its `text` is the row's title. A page passes only when its own rows are there **and** the other
page's rows are gone. No gate in this drive reads a word out of a transcript.

The card link is clicked by its `#`, not by its id: a four-character id is exactly what OCR gets
wrong — one pass of this drive read `#YFPM` as `#VFPM` — while the glyph in front of it is
unambiguous and the transcript's reference is the only `#` token in the shot.

## (2) The second frame was the console's own

Measured rather than looked at (`reply-frames.py`: a frame is a one-pixel column lighter than the
pixel on either side of it, and a frame's column runs the height of the band while a button's runs
only the height of the button).

```
before  docs/qa_evidence/2026-09-21-agents-are-consoles/punch/b02-card.png
        FRAMES 4 at=774,782,1464,1472
```

774/1472 and 782/1464 are eight pixels apart, which is `boardReply`'s content margin — so the
outer one looked like `boardReply`. It was not: at `y=1000` the ground between the two is
`#0f1115` (`@bg`), and `QFrame#boardReply` paints `@surface`. `@bg` + `1px @border` +
`border-radius: 8px` is `QWidget#pane`.

The console **is** a `Pane`, and `RelayWindow::wireAgentConsole` ends in
`relay::theme::polishWindow(console)` (`src/RelayWindow.h:7710`). `Pane` declares no `Q_OBJECT`,
so its `metaObject()->className()` is `"QWidget"`, `polishWindow` takes its `QWidget` branch,
finds a `composerEditor` under it and renames it `pane` — overwriting the `agentConsole` name
`createAgentConsole` had given it. That is right nearly everywhere: it is what makes a console
wear a pane's face in the Switchboard's list page, in Options and in Sessions, where the
transcript stands inside the frame and the prompt box inside that, exactly as in a terminal pane
(`docs/qa_evidence/2026-09-21-agents-are-consoles/punch/c02-console.png` — the same two columns,
`@border` at x=775 and x=1471, with the transcript between them). On a **card** the transcript is
hidden until it is used (owner decision 2), so the pane frame came down to eight pixels outside
the composer's own and the owner was looking at a border inside a border.

So the frame steps back for that case and no other: `CardDetail::setConsole` stamps
`cardConsole` on the console beside the `hasConsole` it already stamps on `boardReply`, and
`src/Theme.cpp` keys `QWidget#pane[cardConsole="true"] { background: transparent; border: none; }`
on it. `boardReply`'s own step-back stays exactly as it was.

## (3) Plan's bright outline was a missing colour, not a focus ring

The action row's buttons are already `Qt::NoFocus` (`Pane::rebuildActionRow`, `src/Pane.h:3298`)
and `QToolButton` has no auto-default, so nothing was taking focus from the composer. What was
missing is a colour:

```
QPushButton[actionRow="true"], QToolButton[actionRow="true"] {
    border-width: 1px; border-style: solid; border-radius: 6px; padding: 4px 12px; … }
QToolButton[actionRow="true"] { background: @raised; color: @text; }
```

The shape rule declares the border's **width** and not its colour — deliberately, because it is
shared with push buttons that bring their own id rule — and the plain-ground rule under it
declared a ground and an ink and no border colour either. With no colour, Qt frames the button in
its own ink. Check, Clean up, Tests and Profile escape it because `QToolButton#boardChatCheck`
and friends say `border: 1px solid @border`; the card page's Plan does not, because its id rule
is `QPushButton#boardReplyButton` and the row is tool buttons now. Read off the same shot:

```
before  BUTTONS row=945  797:e5e8eb  883:e5e8eb   Plan's two edges: @text  (#e6e8ec)
                         890:b38df6  997:b38df6   Execute's:       @agent  (#b388f6)
```

One declaration, `border-color: @border` on `QToolButton[actionRow="true"]`, and a plain button
on any action row is the quiet card-shaped one again while `[leaves="true"]` keeps Execute's
accent outline and `:hover` / `:disabled` keep theirs.

## Tests

| suite | what was added | result |
|---|---|---|
| `consolemode` | `theContextGetsFirstRefusalOnEveryLinkKind` — a real console's `openOutputTarget` for `option:`, `session:`, `card:` and a path: the context is offered each one **with the right `links::Kind`**, a refusal reaches the window's `onOpenOption` / `onOpenSessions` / `onOpenCard` / `onOpenPath` at the thing it names, and a context that swallows stops all four. The `seen` list on the file's stub context had never been read by any case. Plus: every action-row button is `Qt::NoFocus` | 15 cases, all passed |
| `boardworkspace` | the two theme rules, pinned as text beside the ones already there, and `CardDetail::setConsole` saying which console is a card's — with a count, so it stays the only one | passed |
| `settings`, `conversations`, `board`, `boardpane`, `outputlinks`, `theme`, `themeswitch`, `buttonfit` | unchanged, run because the sheet and the card page are theirs | 13/13 passed |

And the live run itself: 22 PASS · 0 FAIL.

`theWindowsWrapperForwardsEveryContextVirtual` (`boardworkspace`) already existed and already
covers "a future virtual is not forwarded"; it is listed here because it is the test the punch
list asked for and it needed nothing.

**The terminal pane is untouched.** The two rules added are keyed on `[cardConsole="true"]` and on
`[actionRow="true"]`; a terminal pane's action row is empty (its context declares no actions) and
no terminal pane is a card's console, so neither rule can match one. No pixel diff is run because
`src/Pane.h` was not edited.

## What is in here

- `drive.sh`, `stub-provider.py` — the run. The stub is the #AGNT drive's, with two scenes added
  (`which session`, `which card`) so the other two link kinds have an answer to carry them.
- `reply-frames.py` — the frame and button-border reader used by (2) and (3).
- `notes.txt` — the 22 PASS lines and the numbers each was read from.
- `rects-last.json` — the named widgets' rectangles as the run last wrote them.
- `a*.png` … `e*.png` — the shots, one series per phase.

## Left open

- **A markdown link's label is not clickable.** `[LABEL](target)` prints as `LABEL (target)` and
  only the target opens. Making the label the link means an OSC 8 run around it, and a prose block
  is **already** one OSC 8 run — `relay://prose/<pane>/<n>`, the anchor the engine's fold layer
  re-wraps the block from (`Pane::openProse`, `TerminalView::setProseBlock`). OSC 8 runs do not
  nest, so a link inside a block would end the block's anchor at the label and the rest of the
  paragraph would stop re-wrapping. That is a change to the prose anchor, not to the renderer, and
  it is the owner's call whether it is worth one. It is on #AGNT's QA checklist with this reason.

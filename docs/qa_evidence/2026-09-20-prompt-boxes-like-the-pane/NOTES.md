# #PBX1 — every agent prompt box is the main pane's prompt box

Implementer evidence, 2026-09-20. Commits `502f2b22` (the helper panel), `ce29cb8e` (the card
page) and the commit that carries this folder.

The owner, comparing the main pane's prompt box with the helper panels:

> i dont like the helper agent prompt UI … there is the useless help sentence, and then a bunch of
> wasted space, and then the tiny text box. and the buttons dont look as good.

> [remove] the send button on all, make it like the pane agent.

and, of the card page's Plan / Execute / Verify:

> move those buttons out of there (plan / execute / etc), because they actually dont do anything
> in the chat box. can we instead put buttons like that in a row above the chat box. they are
> actions the agent can take that dont require typing. we put the 'clean up' button there for the
> main switchboard agent, for example.

So one rule, everywhere: **an action that needs no typing goes in a row above the box; the box
holds the text and the chips that qualify it.**

## How to run it again

```
docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/drive.sh [relay-binary] [out-dir]
```

Xvfb, an isolated `HOME` / `XDG_*` / `TMPDIR` under a short path (the 108-byte socket limit),
`RELAY_KEYRING=off`, and no provider account: the profile points a local endpoint at
`stub-provider.py`, so every agent in the run is that script and no key is read. Needs Xvfb,
xdotool, ImageMagick and tesseract. Each check writes one line to `notes.txt` naming the shot it
was read from.

The shots here were taken with the binary `scripts/land.py` built from the exact tree it
committed (`/tmp/claude-1000/land/promptboxes/verify/build/relay`). This checkout's `build/relay`
carries other sessions' in-flight edits, so it is not evidence of what landed.

## The shots

| | what it shows |
|---|---|
| `01-pane-and-switchboard-idle.png` | The terminal pane's prompt box (bottom left) and the Switchboard agent's (bottom right), both idle and side by side. One rounded frame each, a borderless editor in the prompt font, and a chip strip under it; the panel's head row carries Check, Clean up and Tests — the actions that need no typing — and its log, with nothing in it, takes no height at all. No Send button, and no help sentence above the box. |
| `02-switchboard-conversation.png` | The same pair after one turn. The log appeared and is as tall as what it holds, not a 320 px well with two lines at the top of it; the box has the accent border while the cursor is in it, and the context chip ("93% left") joined the strip beside the model box and the microphone, in the pane's order and at the pane's chip height. |
| `03-card-page.png` | The card page. **Plan (p)** and **Execute (x)** are a row of their own above the frame, right-aligned; inside the frame are the editor and the model box alone on its strip. The frame is the panel's frame — same ground, same radius — so the list page and the card page are one component. |
| `04-options-helper.png` | The Options helper, expanded and idle: the head says "Options helper", the log is not there at all, and the box is the pane's box with the model and the microphone on its strip. |
| `05-sessions-helper.png` | The Sessions helper, expanded, with the box focused so the accent border shows. |
| `06-narrow-pane.png` | A ~350 px pane on the card page. Plan and Execute are whole and keep their keys — which is what moving them off the box's strip bought — and the model box is whole under them. |
|  `_approvals.png`, `_board.png`, `_list.png` | Working shots the script reads coordinates from; kept so a rerun can be compared with this one. |

`notes.txt`: **22 passed, 0 failed.**

## Read against the pane, by eye

The three things worth looking at in `01` and `02`, because a test cannot see them:

- **The frame.** The panel's box and the pane's box are the same rectangle: `@surface`, a 1 px
  `@border`, a 10 px radius, `@accentBorder` while the cursor is in it. The editor inside has no
  border of its own — a border inside a frame is what made the old box read as "the tiny text box
  at the bottom".
- **The chip height.** "93% left", the model box and the microphone are one height and one style
  in both boxes (`stripChipLabel` / `stripChip` / `statusPicker` rules, copied rule for rule into
  the `boardChat*` names).
- **No empty block.** In `01` the box sits directly under the head row; in `04` and `05` directly
  under the pane's own last row. The paragraph that used to stand there is gone and the log is
  hidden until there is a conversation.

## QA checklist

1. Open the Switchboard beside a terminal pane. The panel's box and the pane's box should look
   like one control: same frame, same radius, same chip height, no border on either editor.
2. The panel has no Send button. Enter sends. While a turn runs, the busy strip appears **inside**
   the frame with `✕ Stop` on it, and Esc in the box stops the turn too.
3. With an empty conversation there is no log and no help sentence — the box's placeholder ("Ask
   the Switchboard agent — Enter sends, a second prompt queues") is what says what it is for.
   After a turn the log appears and is no taller than what it holds.
4. Check and Clean up are on the head row above the box, and wear the card page's Plan/Execute
   face.
5. Open a card. Plan / Execute (and Verify in a QA lane) are a row above the reply frame, not in
   it; `p`, `x` and `v` still work, and both still carry whatever is typed in the reply box as
   their note.
6. The model box is alone on the reply box's strip, right-aligned.
7. Squeeze the window until the card has the pane to itself at ~350 px: nothing is clipped, and
   the labels keep their keys.
8. Options (Ctrl+Shift+O), Actions and Sessions (Ctrl+Shift+Y): Alt+Q expands the helper, and its
   box is the same box with the same placeholder naming that helper.

# #4X53 — "Activity", and its button on the Relaying line: implementer evidence

The pane card #QT8C shipped as "Agent internals" is called **Activity** everywhere a person reads
it, and a circled Relay mark at the left of the "Relaying – …" line above the prompt box opens it.
Driven by `drive.sh` against `build/relay`, under Xvfb with an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` (a short `/tmp/relay-activity.XXXXXX` path: unix
sockets stop at 108 bytes) and `RELAY_KEYRING=off`, so nothing here touched the owner's profile or
identity key. No provider account: `stub-provider.py` is a loopback OpenAI-compatible endpoint
whose every turn is ~5 s of reasoning, a `run_command` that sleeps 4 s and two reads — long enough
for the busy line to be up, read and clicked.

    docs/qa_evidence/2026-09-19-activity-pane-button/drive.sh "$PWD/build"

| Shot | What it shows |
| --- | --- |
| `implementer-turn-button.png` | An agent turn: "Relaying – thinking… · 2 s · step 1/256 · Esc stops" in the agent's violet, the circled mark at its left, on the prompt text's own left edge |
| `implementer-turn-button-zoom.png` | The same line at 4x: the cord, the dash and the seated tip inside the ring — the app's mark, not a second glyph |
| `implementer-program-button.png` | `sleep 20` in the terminal: "Relaying – sleep…" and the same mark, both in the terminal's blue |
| `implementer-program-button-zoom.png` | …at 4x |
| `implementer-before-click.png` | The line the click aims at (the script finds the word "Relaying" by OCR and clicks 12 px left of it, which is the middle of the icon) |
| `implementer-clicked.png` | After the click: the Activity pane beside the terminal — its header band, its "✦ Activity" title and the tab's "project; Activity · 2" all say Activity — with the busy line and its button still there |
| `implementer-clicked-streaming.png` | The turn's rule, its reasoning and its tool rows arriving in that pane |
| `implementer-clicked-twice.png` | A second turn, a second click: the same pane comes forward, holding both turns (2 turns · 6 tool calls). No second Activity pane, no second tab entry |
| `implementer-hint.png` | The `internals.open` shortcut hint fired by the button's own path: "Next time: Alt+Shift+R · opens the Activity pane" |
| `implementer-palette.png` | The Actions palette: the row is "Activity · Agent · Watch the reasoning and the tool calls in a pane beside the terminal · Alt+Shift+R" |
| `implementer-palette-opened.png` | Running that row opens the same pane |
| `implementer-notes.txt` | Per shot: OCR of the terminal half and of the pane half, and the pixel the click went to |
| `logs-<scene>/` | Relay's and the worker's rotating logs for that scene (the app's own stdout was empty in every one, so those files are not kept) |

Read with the shots:

- **The ink is the line's ink.** The button takes the colour from the same
  `panestatus::stateText(m_state, …)` call the word does, so it is violet for an agent turn (the
  turn shot), the terminal's blue while a program runs (the program shot), and amber when the turn
  is blocked on a question — the one state with no scene here, because the stub has no `ask_user`;
  it is the same value the word beside it is painted with, on the same line of code.
- **The button is on screen exactly when the line is.** `paintEvent` returns before anything is
  drawn when the line has no text, and the widget is hidden between turns: `implementer-clicked-
  streaming.png` and `implementer-clicked-twice.png` are after the turn ended, and there is no
  stray mark above the prompt box.
- **The hint keeps its 20 s global gap.** The first turn's "Reasoning streams under the ✦ line"
  toast takes that gap, which is why the `hint` scene runs a whole turn, waits past it, and starts
  a second turn before clicking. That is `relay::ShortcutHints`' own rule, not the button's.
- **The approvals pane is turned off in the profile** (`security/approvals_chosen=true`): the
  first-launch choice (#K2FV) would take half the window and make the Activity pane split under
  the terminal instead of beside it.
- The first few characters `xdotool type` sends after the window appears are dropped, so every
  typed line clicks into the prompt box first (`focus_prompt`).

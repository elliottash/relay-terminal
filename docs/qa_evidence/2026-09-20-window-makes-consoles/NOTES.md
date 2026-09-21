# The window makes agent consoles — card #AGNT step 5, live

Implementer evidence for "the window makes consoles, and stops making helper panels": the window
builds a no-shell `Pane` for a host that asks for one, hands it over as a `relay::agent::
ConsoleHandle`, and keeps it out of everything a *window's* pane is in.

Three runs, all under Xvfb with an isolated `HOME` / `XDG_*` / `TMPDIR` under a short path,
`RELAY_KEYRING=off` and **no provider account** — the profile points a local model endpoint at
`stub-provider.py` on loopback, so every agent in every run is that script.

| run | script | binary | result |
|---|---|---|---|
| the terminal pane, before and after | `../2026-09-20-agent-console-extraction/drive.sh` | clean exports of `5eb5699e` and of `21fbcc5b` | 6/6 both, and the pixel table below |
| the console itself, unembedded | `../2026-09-20-agent-console-extraction/drive-console.sh` | `relay-console-harness` | 6/6 |
| the window's consoles, embedded | `drive.sh` (this folder) | `relay` | see `notes.txt` |

## 1. The terminal pane is unchanged

`terminal-diffs.txt`, and the shots in `terminal-before/` and `terminal-after/`. The numbers are
read below the pane header (crop `1400x824+0+76`), because the pane's auto-generated title differs
between two runs of the *same* binary — the control in the step 1 evidence measured that at
1 316–1 539 px, and every whole-window figure here is exactly that 1 316.

Eight of the ten shots are pixel-identical below the header. Two differ by **36 px** — the
blinking caret's 2×18 box, which the step 1 and step 3 evidence measured the same way — and
`01-fresh` by 21 310 until the startup quota toast's band is masked, after which it is 36 as well.
The step 1 evidence read the same 21 310 for the same toast.

## 2. The console, embedded in Options

`01-options.png` … `05-panes.png`, and `notes.txt` beside them.

- **01** Options carries the collapsed "Helper Agent (Alt+Q)" row the pane owns.
- **02** Alt+Q expands it into a console the *window* made: the Options context's placeholder in
  the box, its own model box, its own effort picker and its own mic — a prompt box, not a panel.
- **03** it answers, and the answer streams into a **vterm transcript**: the `✦` echo of what was
  asked, the `▸ stub` turn header, and the prose under it. That is the Issue this card opens with,
  closed: "the queue doesn't work like the main terminal, and the thinking bubbles don't work the
  same way."
- **04** the `option:` link in the answer is a link — underlined, clickable — and clicking it
  reveals the row in this same pane, because the context resolves it before the window sees it.
- **05** **the phone's pane list.** `app_panes` is answered from `RelayWindow::allPanes()`, which
  is the same walk `syncTabShares` and `syncAlwaysOnShares` publish to the phone with. Asked with
  two consoles on screen, it comes back `count=1 titles=project`: the terminal pane, and neither
  console. That is `panesIn` stopping at a `ToolPane`, measured rather than argued.
- **06** Sessions, in the same tab, gets its **own** console: its own collapsed "Helper Agent
  (Alt+Q)" row at the bottom right, and its own prompt box behind it (`Ask the Sessions helper…`,
  its own model box and effort chip). Both console headers carry the **same** turn — the tab's
  conversation is one, and every console of the tab draws it (owner decision 1). The one FAIL in
  `notes.txt` is that count read back as 1 rather than 2: the second console sits at the foot of
  a 1600 px screen and its header line is clipped, so the OCR pass misses it. `06-sessions-console.png`
  and `07-second-answer.png` show both, and 07 below measures the same fact without reading pixels.
- **07** **one worker, one conversation.** With two consoles open, this Relay has **two** worker
  processes — the terminal pane's and the tab's — and the helper store holds **one** conversation
  file. That is `persist {scope: "helper", key: <tab id>}` doing what the card's decision 1 asks.


## 3. What the live gate found that the build and the suites did not

Three things, all of them invisible to `ctest`:

1. **A console the window does not attach had no worker at all.** The constructor was deciding
   "a console starts no process" before `onWorkerLine` could be set, so the harness's console was
   never configured and Enter opened the provider dialog over an empty transcript. The question is
   now asked a turn of the event loop later, when the answer exists (`src/Pane.h`).
2. **The embedded console had no room to be a transcript.** The helper strip takes the console's
   size hint — a `Pane`'s, which is a terminal's — so Options and Sessions gave it three rows. The
   first run drew a whole answer into them and the shot showed the pane header wearing the
   answer's auto-generated title over an empty vterm. Both hosts now floor the strip at twenty
   lines, never past the cap they already had.
3. **A screenshot under Xvfb can lie.** There is no compositor, so a damaged region is sometimes
   left unpainted — a blank block exactly where the vterm is. `drive.sh` forces a full expose with
   a one-pixel resize before every shot, the way `drive-console.sh` already did.

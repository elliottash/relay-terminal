# Every agent in Relay is the prompt box — card #AGNT, the integration drive

Implementer evidence for the last of card #AGNT: steps 1–9 landed, and this is the card's own
Verify section driven live. The card opens with the owner looking at the helper's panels —

> "the queue doesn't work like the main terminal, and the thinking bubbles don't work the same
> way. why not just make it feature equal with the terminal agent?"

— and this folder is what says they are the same thing now: a helper agent is the prompt box a
terminal pane has, a no-shell `Pane` with a `relay::agent::Context`, in the Switchboard, on a
card, and in Options, Actions and Sessions.

## How it was driven

`drive.sh [relay-binary] [out-dir] [phase …]`, under Xvfb with an isolated `HOME`, `XDG_*` and
`TMPDIR` under a short path (the 108-byte socket limit), `RELAY_KEYRING=off` and **no provider
account**: the profile points a local model endpoint at `stub-provider.py` on loopback, so every
agent in the run — the terminal pane's and every console the window makes — is that script. The
fixture is a board with three cards and three saved conversations, written into the sandbox
before the first launch.

Seven phases, one per thing the card asks to see:

| phase | what it drives |
|---|---|
| `sb` | the Switchboard console: a thinking bubble that folds, a tool row, the §12 queue strip with a second prompt that can be opened in the box and removed, Esc, the action row (Check k · Clean up u) and Alt+M |
| `card` | the card console: Plan (p) / Execute (x) above the box, Enter discusses and the turn reaches `issues/threads/<ID>.md` with its provenance, Ctrl+Shift+Enter comments |
| `opt` | the Options helper: the collapsed "Helper Agent (Alt+Q)" row, an agent write shown with Undo and the row marked "changed by the agent", an `option:` link revealed in place, and Actions |
| `sess` | the Sessions helper: "open the sessions about panes in new panes" |
| `cross` | the owner's rule: an option changed from the Sessions helper, a card opened from the Options helper, and a terminal pane's agent reading the board |
| `info` | the ⓘ and Activity Ask rows, which still draft into the terminal pane's composer |
| `tabs` | two tabs on one project are two conversations, and a restart comes back to the one it had |

Each check writes one PASS/FAIL line to `notes.txt` naming the shot it was read from, and every
one of them was also looked at by eye — three of the findings below are things OCR called a pass.

## The terminal pane is unchanged

`terminal-diffs.txt`, and the shots in `terminal-before/` and `terminal-after/`. The nine-scene
drive is step 1's own (`../2026-09-20-agent-console-extraction/drive.sh`), run on a clean export
of `67837ec9` — the tip this session started from, before its first commit — and on a build of
its tip. Both pass the same six OCR checks.

`compare -metric AE`, below the pane header (crop `1400x824+0+76`, which is the terminal, the
transcript, the queue strip and the composer):

| shot | whole window | below the header |
|---|---|---|
| `01-fresh` | 36 | **36** |
| `02-thinking` | 1 888 | **0** |
| `03-fold-open` | 1 924 | **36** |
| `03b-fold-closed` | 1 888 | **0** |
| `04-toolrow` | 2 030 | **0** |
| `05-modelbox` | 1 888 | **0** |
| `06-modelbox-closed` | 1 888 | **0** |
| `07-queued` | 2 030 | **0** |
| `08-esc` | 1 888 | **0** |
| `09-after` | 1 888 | **0** |

Eight of the ten are pixel-identical. The two that are not are **36 pixels** — a 2×18 box at
+36+824, the caret in the prompt box, blinking, on in one shot and off in the other; the step 1,
3 and 5 evidence measured the same box the same way. The whole-window numbers are one thing, and
it is the same thing every earlier run of this drive found: the pane's **auto-generated title**,
a 221×20 box at +91+46 — "Explain reasoning fold behavior" against "Explain fold behavior"
(`terminal-diff-02-pane-title.png`). Relay titles a pane from its conversation and the stub does
not make the model deterministic about its own wording, so two runs of the *same* binary differ
there too (the step 1 control measured 1 316–1 539 px). Nothing in the transcript, the folds, the
tool rows, the queue strip, the model box or the composer differs by a pixel.

## What the live gate found that the build and the suites did not

Seven, and every one of them was green in `ctest` and green in the compiler.

1. **The Switchboard's console had three rows to draw in.** `BoardView`'s column adds the console
   with no stretch, so it took the console's own size hint — a `Pane`'s, which is a terminal's —
   and the list page gave it three lines. A whole answer went into them: the ✦ thinking fold, the
   `read_file` row and the prose were all *there* and none of them was on screen, and
   `placeQueueStrip` hides the §12 strip outright when `roomForBubble(0)` is false, so a second
   prompt queued with nothing to say so. That is half this card's Issue reappearing one level up,
   in the surface the card exists to fix. `BoardView::updateConsoleHeight` is the rule Options and
   Sessions already apply: at most ~40 % of the pane, never fewer than twenty lines. (`b8453c97`)
2. **A card's Enter was travelling as an ordinary `ask`.** Every console the window makes is a
   `Pane` with `TabConsoleContext` — the wrapper that knows the tab's project and where its one
   conversation is kept — and the wrapper forwarded every virtual of `relay::agent::Context`
   except `submit`. `CardContext::submit` was therefore never reached through a window-made
   console: the card's conversation ran, `issues/threads/<ID>.md` learned nothing and the stage
   did not advance (19.10, owner decision 2). `consolemode` was green either way, because its
   cases hand a context to a `Pane` directly. (`b8453c97`)
3. **A click in a console made it the window's active pane.** `isLeaf()` is `Pane || ToolPane` and
   a console *is* a `Pane`, so `leafOf()` — which walks **up** from a clicked widget and takes the
   first leaf it meets — answered the console rather than the tool pane around it. Everything the
   window then aimed at "the active pane" landed on a surface whose leaf-shaped hooks are
   deliberately unwired. The drive caught it as "open the sessions about panes in new panes" from
   the Sessions helper: the tool answered ok, the helper said "Opened 3 conversations in new
   panes", and not one pane opened — `m_active->openSavedSession()` had reached a console with no
   `onOpenSessionInNewPane`. `panesIn` stopping at a `ToolPane` (step 5 item 2) fixed the walk
   down; this is the walk up. (`6f0f80a7`)
4. **A card page's prompt box said "…" and nothing else.** `RichEditor` takes the first candidate
   that fits, so the two-rung ladder the console built — the context's line, then "…" — is
   all-or-nothing, and `CardContext`'s line does not fit a card page's box. The three chords the
   owner asked to be said *in the box* (#VZ69: "you just press enter in the prompt box to discuss
   / comment") were said nowhere near it. `relay::agent::placeholderRungs` builds the ladder out
   of the line's own clauses, so a narrow box says less and never something else. (`6f0f80a7`)
5. **The card page wore two headers**: the console brought a `Pane` header with it, so the reply
   box sat under a second heading reading "project", under the card's own title. (`6f0f80a7`)
6. **Enter on a card did nothing at all.** `RichEditor` declares no `Q_OBJECT`, so
   `findChild<RichEditor *>()` matches on `QPlainTextEdit`'s metaobject and answers the first
   plain text edit in the console — the transcript's fallback view, not the prompt box. The card
   page's `m_reply` pointed at it, `CardDetail::submit` read it empty and returned, and the words
   sat in the box: no `board_ask`, no thread entry, no stage advance. Finding 2 above had to be
   fixed before this one was even reachable. (`999a8398`)
7. **And one of my own.** Renaming the composer's `objectName` to something a QA driver would
   rather type broke three things that find the prompt box by `composerEditor` — the stylesheet
   rule that makes it borderless, `theme::polishWindow` (which names the frame `composer` and the
   widget `pane` by finding it) and `RelayWindow::repolishLeaf`. The editor drew its own default
   frame inside the composer's, in every pane and every console. Caught in the before/after crop
   of the Switchboard's box. (`d52a9d54`)

## The card console's transcript: what was chosen, and why

The card's own turn does not print into its console — a `board_ask` turn's `delta` and
`thinking_delta` stream into the **thread view** above, which is where the owner's decision 2 put
them and where the record is. So the question the card left open was whether the vterm under that
thread is dead space.

Looked at live, it is not, and it is not collapsed:

- The tab's conversation is **one**, drawn in every console of it (owner decision 1). A
  Switchboard turn prints in the card's console too; the transcript is empty only until the tab
  has had a turn that is not this card's.
- The thinking bubble and the **§12 queue strip** are drawn in that room. A console with three
  rows draws neither — finding 1 above is exactly that, measured. Collapsing the card's
  transcript to zero would take the queue off the card page, which is the half of this card's
  Issue that is about the queue.

What was wrong was the framing, not the band: a second pane header over it, and a box that said
nothing. Both are fixed, and the console is floored at ten lines on the card page (`CardDetail::
setConsole`) so the strip and the bubble have somewhere to go — the page keeps the rest.
`b02-card.png` is the result: Plan (p) · Execute (x) over a box reading "Reply — Enter discusses,
Ctrl+Enter plans", with the card's thread above it and one header on the page.

## Per item, what it read

`notes.txt` is the full run (38 PASS · 10 FAIL); `rerun-card/notes.txt` and
`rerun-options/notes.txt` are the two phases re-driven after their findings were fixed and after
the drive's own reading was corrected. What each of the card's Verify items came to:

| | what the card asks | read |
|---|---|---|
| **a** | the Switchboard console: a thinking bubble that folds and unfolds, a tool row, the §12 queue strip with a second prompt that can be opened in the box and removed, Esc, Check (k) · Clean up (u) left-aligned above the box, Alt+M | **14/14 PASS.** `a03`–`a05` the bubble under "✦ thought for 1 s", unfolded with Alt+R and folded again; `a07` the `read_file` row; `a09` the strip — "QUEUE · ↑ select a row · Ctrl+↑↓ move · Shift+Del remove", "▸ running ✦ count slowly to twenty" and "✦ and then say hello ×" — over "Relaying · thinking… · 14 s · step 1/500 · Esc stops"; `a10` Up opens the queued line in the box, `a11` Shift+Delete removes it; `a12` Esc; `a14` the pane's own picker; `a16` `k` runs Check |
| **b** | Plan (p) / Execute (x) above the box; Enter discusses and the turn reaches the thread with its provenance; Ctrl+Shift+Enter comments | **5/6 PASS** (`rerun-card/`). `b02` the row and the box ("Reply — Enter discusses, Ctrl+Enter plans"); `thread-DYNH.md` holds the owner's words, the stage move Inbox → Discussing and the answer with `model=stub turn=<session>/<turn>`. **Left:** the Ctrl+Shift+Enter comment did not land — the chord is `CardDetail::submit("")` → `board_comment`, and the drive sends it straight after a Discuss turn; whether that is the page or the drive is not settled here |
| **c** | the collapsed row, an agent write shown with Undo and the row marked "changed by the agent", an `option:` link revealed in place, Actions | **PASS** for the row, the console, the write (`relay.conf: copy_on_select=true`), the link and its reveal, and for Actions having its own row and its own brief. **Left:** the row's "changed by the agent" marker and the notification's Undo were not read — the marker is under the switch in the Terminal section and the notice is in the bell's list, and neither click could be aimed by OCR (`rerun-options/c04-notice.png`) |
| **d** | "open the sessions about <keyword> in new panes" | **PASS.** `d03` the two tool rows and "Opened 3 conversations in new panes: …"; `d04` the tab reads **5** panes. Before the `isLeaf` fix it read 2 and the tool still answered ok |
| **e** | the owner's rule, across contexts | **5/5 PASS.** An option changed from the **Sessions** helper; a card opened from the **Options** helper; and a terminal pane's agent reading the board — `BOARDLIST count=3 first=QMGA` |
| **f** | the ⓘ and Activity Ask rows | **PASS.** `f04` the ⓘ row's question drafted into the *terminal pane's* composer, never sent; `f07` Activity opened from the Options console shows that console's turn |
| **g** | two tabs on one project, and a restart | **PASS** for the two tabs: separate conversations, both answer, two files. **Left:** the restart answered but wrote a **new** conversation file (8 → 9). The helper store is keyed (workspace, tab) and the run's own store shows one tab id under two workspace digests — the project's and the empty one — so a console that asks before its tab's project reaches it, or a tab that gains a project later, moves its conversation. That is #FEJQ's keying rather than this card's wiring, and it is written down here rather than guessed at |

Three of the ten failures were the drive reading rather than the app, and are fixed in the
committed `drive.sh`: `hasword` was used by name and never defined (a silent
`command not found`), `word_xy` takes the **last** match so clicking "reply" hit the page's key
legend instead of the box, and `app_option_set` takes `option:<section>/<row>` — the catalog id
`RelayWindow::toggleRow` builds — not the link's `<section>/<row>`.

## What is in here

- `drive.sh`, `stub-provider.py` — the run.
- `notes.txt` — the PASS/FAIL lines of the full run, and the numbers each was read from.
- `rerun-card/`, `rerun-options/` — those two phases re-driven after their findings were fixed,
  with the card's thread file as it was written (`thread-<ID>.md`).
- `a*.png` … `g*.png` — the shots, one series per phase.
- `terminal-before/`, `terminal-after/`, `terminal-diffs.txt`,
  `terminal-diff-02-pane-title.png` — the ten-shot pixel comparison above.
- `relay.log`, `worker.log` — the run's own logs.

# Relay architecture

Current as of 2026-09-19 (commit `ab8f37e`). Every
statement points at code; paths are relative to the repository root. Planned work is in
[ROADMAP.md](ROADMAP.md), test status in [VALIDATION.md](VALIDATION.md).

Contents:

1. [Overview](#1-overview)
2. [Process model](#2-process-model)
3. [Windows, tabs and panes](#3-windows-tabs-and-panes)
4. [Keyboard: Keymap, presets, palette](#4-keyboard-keymap-presets-palette)
5. [Composer and routing](#5-composer-and-routing)
6. [Shell bridge: staging commands safely](#6-shell-bridge-staging-commands-safely)
7. [Terminal-mode fix loop](#7-terminal-mode-fix-loop)
8. [Inline agent output](#8-inline-agent-output)
9. [Human and agent control](#9-human-and-agent-control)
10. [File panes and `relay open`](#10-file-panes-and-relay-open)
10a. [The Switchboard pane](#10a-the-switchboard-pane)
11. [Agent backend](#11-agent-backend)
11a. [Guest agents: Claude Code and Codex in a pane](#11a-guest-agents-claude-code-and-codex-in-a-pane)
12. [Keys, keyring and imports](#12-keys-keyring-and-imports)
13. [Per-pane isolation](#13-per-pane-isolation)
13a. [Logs](#13a-logs)
14. [Theme](#14-theme)
15. [Packaging layout](#15-packaging-layout)
16. [The terminal: `TerminalBackend` and Relay's engine](#16-the-terminal-terminalbackend-and-relays-engine)
17. [Fragile dependencies and limits](#17-fragile-dependencies-and-limits)
18. [Source map](#18-source-map)
19. [Sharing a pane with a phone](#19-sharing-a-pane-with-a-phone)

## 1. Overview

Relay is a native C++/Qt application for Linux with **its own terminal engine** (`engine/`,
`docs/ENGINE.md`). It embedded KonsolePart until 2026-09-18, when the owner retired it: the
engine had been every pane's terminal long enough to be the only one. Under each terminal sits
a real text editor (the composer). Text typed there goes to the shell or to a bring-your-own-key agent.
The agent runs in a separate Python process per pane and talks to any OpenAI-compatible
chat-completions endpoint.

It builds against Qt5, and against Qt6 once the remaining `qsizetype` narrowing is fixed
(`CMakeLists.txt`, `RELAY_QT_MAJOR=AUTO|6|5`; AUTO takes Qt5 wherever it is installed). KDE
Frameworks is optional and supplies only the file-preview highlighter. It is not a Konsole fork
and does not read or patch Konsole's configuration.

## 2. Process model

```text
relay  (GUI process: Qt, every terminal engine instance, all screens and scrollback)
 |
 |-- WindowManager: private QLocalServer  $TMPDIR/relay-open-XXXXXX/open.sock   <-- scripts/relay-open
 |
 +-- per terminal pane (Pane) -------------------------------------------------------------
 |    runtime dir  $TMPDIR/relay-XXXXXX (0700): state.json, input.txt (0600)
 |
 |    systemd scope relay-pane-<id8>-shell-<n>          systemd scope relay-pane-<id8>-agent-<n>
 |      bash --noprofile --rcfile shell/integration.bash -i    python3 -S -u backend/worker.py
 |        |-- shell/event.py  (writes state.json)                |-- bash -n  (router syntax check)
 |        +-- user commands, vim, builds ...                     +-- bash --norc -c  (run_command)
 |               ^                                                      ^
 |               | PTY (engine TerminalSession / sendInput)             | NDJSON over stdin/stdout
 +---------------+------------------------------------------------------+
```

| Process | Started by | Talks to the GUI through |
|---|---|---|
| `relay` | user or desktop file | n/a |
| Pane shell (Bash) | the engine's `TerminalSession`, wrapped in `systemd-run --user --scope` when available | PTY bytes; atomic `state.json` events; `input.txt` for staged commands |
| `shell/event.py` | Bash prompt and DEBUG hooks | writes `state.json` (token, sequence, event, status, cwd, shell PID, aliases/functions, PATH) |
| Agent worker `backend/worker.py` | `QProcess`, wrapped in `systemd-run` when available | newline-delimited JSON on private stdin/stdout. No TCP port. |
| Agent tool commands | worker, `/bin/bash --noprofile --norc -c` in a new session | results go back through the worker |
| `scripts/relay-open` | `relay open` in a pane shell, or another application opening a `relay://` link | Unix socket `RELAY_OPEN_SOCKET`, one JSON line |

Startup (`main()` in `src/main.cpp`):

1. The data root is the first of `$RELAY_DATA_DIR`, `<exe>/../share/relay`, the compiled
   `RELAY_DATA_DIR`, or the source tree that contains `backend/worker.py`.
2. `<data>/scripts` is prepended to `PATH`; `RELAY_OPEN_HELPER` points at `relay-open`.
3. `WindowManager` opens the socket, then `newWindowAt(--workspace)` creates the first window.

Options: `--workspace/-w PATH` (initial terminal directory and agent workspace) and
`--clean-shell` (skip `~/.bashrc`).

**The private runtime directories, and who cleans them up** (`src/RuntimeDirs.h`). Both directories
above are `QTemporaryDir`s, so an ordinary quit — including SIGTERM, SIGINT and SIGHUP since
0f49c89 — removes them. A crash or `kill -KILL` does not, and the owner's `/tmp` had 876 leftovers
(#9JYK). Each directory therefore carries an **owner file** (`owner`, 0600, written atomically as
soon as the directory is made): `pid` plus the process's `starttime` from `/proc/<pid>/stat`,
because pids are recycled and a bare pid would spare a stranger's directory for ever. Three seconds
after startup — never before the first window — `relay::runtimedirs::sweep()` walks `$TMPDIR` and
removes a directory whose owner is gone, keeps one whose owner still runs (so a second Relay never
touches the first's), and gives a directory with no owner file at all — an older build's, or one
whose mark is a millisecond from being written — a week's grace (an older build may still be
running, and an idle pane does not touch its directory). It considers only names
`QTemporaryDir` itself could have produced (`relay-XXXXXX`, `relay-open-XXXXXX`), only real
directories that are not symlinks, owned by this uid, mode 0700, directly inside `$TMPDIR`; it
never follows a symlink while removing; and it stops after 400 directories or 1.5 s. Counts are
logged as `runtime_sweep` only when something was removed or failed.

## 3. Windows, tabs and panes

| Class | Role |
|---|---|
| `WindowManager` | Window list, a stack of up to 25 closed items (pane, tab or window), the saved window layout, the `relay open` socket |
| `RelayWindow` | `QMainWindow`: its own title bar (the tab row), a `QTabWidget`, the Actions / Options pane (a `ToolPane`, section 12), an application event filter for shortcuts |
| Tab page | One root widget: a leaf or a tree of `QSplitter`s |
| `Pane` (leaf) | Terminal pane: the engine, a Bash bridge, a composer, its own worker and conversation |
| `ToolPane` (leaf) | Folder explorer or file preview (section 10) |

Pane anatomy, top to bottom: the header (the pane title on the left, the directory on the right;
clicking the directory opens the explorer, and what the row does when it runs out of room is the
give-way ladder below), an optional
banner (memory kill, restart), the terminal, the transcript panel (section 8), the composer
frame (route label, input-mode picker, model picker, interrupt-shell button, Submit, editor,
key hints). Overlays float over the terminal without resizing it (a resize makes the idle shell
redraw its prompt in the middle of inline output): the agent queue strip, the thinking panel,
toasts and the pane button row.

**The pane header as it narrows** (owner, 2026-09-19). With everything on — the state glyph and
its word, the title, the directory, the ssh chip, the phone chip, the usage meter and the subagent
badge — the row wants about 470 px, and a pane in a three-pane row has far less. The elements give
way in one decided order, and when the pane widens again they come back in exactly the reverse one:
(1) the directory elides from the left to its legible floor and then goes altogether, since below
that floor "…/x" says nothing a stub could be worth; (2) the title elides (ElideRight) down to its
own floor; (3) the state's word goes to its short form (`stateLabelShort`: "Running", "Subagents")
and then goes, leaving the glyph, which says it too; (4) the ssh chip is squeezed to its 150 px
floor and then drops the `user@` for the host alone — that is what going *below* the floor buys —
and below the host's own width elides the host; (5) the usage meter collapses to CPU alone (`cpu 12%`), with
neither the memory half nor the separator; (6) nothing else gives: the subagent badge stays whole
and the glyph stays, always. Nothing is ever drawn as a partial glyph or cut mid-letter: each step
either elides by whole glyphs or leaves its element out, and both labels are elided by hand rather
than left to the layout, which clips a squeezed QLabel mid-glyph — that is how a crowded header came
to end in a stray half of a character instead of a path.

The whole ladder is one pure function, `relay::panes::headerFit()` in `src/PaneLayout.{h,cpp}`
(floors `kTitleFloorPx`, `kDirectoryFloorPx`, `kSshFloorPx`; tests `tests/panelayout_test.cpp`,
including the rung order, the reverse path and that the answer is a function of the width alone).
It is given the header's width and every element's *natural* widths, and never what an element is
showing at this moment: `Pane::updateHeader()` applies the answer, which changes what those widgets
ask for, so Qt lays the row out and asks again — an answer that read the current forms would feed
itself and the header would flicker between two rungs for ever. Each element is served against the
header minus the *full* widths of the elements above it in the order and the floors of those below,
which is why the thresholds meet (the title reaches its floor at the same width at which the word
starts to shorten) and why a word that has just gone cannot hand its pixels back to the title, which
gave way before it — that would be the order run backwards, the title growing as the pane narrowed.
The unspent pixels go to the stretch in the middle of the row. `Pane` owns the two labels and
`PaneChrome` owns everything else in the row, so the two halves meet over the ladder:
`Pane::onHeaderWants` collects the chips' natural widths and `Pane::onHeaderFit` hands each chip the
form and the room it was given (`PaneChrome::measureHeader`, `PaneChrome::applyHeaderFit`).

Layout rules:

- Splitting reuses the anchor's splitter if the orientation matches, otherwise wraps the
  anchor in a new splitter. Closing a leaf collapses a splitter left with one child.
- Focus movement is geometric: the nearest leaf on the requested side, then the best aligned.
- The focused leaf gets the `relayActive` property (accent outline). Agent and terminal
  actions use the last focused terminal `Pane` in that tab, even when a tool pane has focus.
- Closed items are stored as JSON layout nodes:
  `{"pane":{"cwd","workspace","engine","engine_core","agent_role","agent_mode","input_mode",
  "effort","preset","model","session_id"}}`, `{"explorer":{"path"}}`, `{"preview":{"path"}}`,
  `{"plan":{"path"}}`, `{"split":"h"|"v","sizes":[…],"children":[…]}`. Restoring starts **new
  shells** in the saved directories (`RELAY_START_DIR`, applied by `shell/integration.bash` after
  `.bashrc`). Running programs are not restored. The node carries a `scrollback` id, and the text
  behind it is written when the pane, its tab or its window closes (as well as on quit), so a
  reopened pane replays what was on screen when it was closed, then resumes its conversation.
- **Recently closed** (`src/ClosedStack.h`, `WindowManager::remember` / `restoreClosed`). The last
  25 closed panes, tabs and windows, newest last, each with when it closed, where it sat (split
  direction, divider sizes, tab position, window geometry), hand-set tab names and what its panes
  were called. `closed.restore` (Ctrl+Shift+Z) reopens the newest; `closed.list` and the "Recently
  closed" group in the Actions pane reopen any of them. The list is written to
  `state/closed.json` (0600, atomic) on every change and read back on start, under the same
  rules as the saved layout: only the Relay that owns the layout reads or writes it, and not at
  all when `windows/restore` is off. A pane that ends because its shell exited (`exit`) is
  recorded like any other close. Windows that all close together are a quit — the saved layout
  reopens them — so they leave the list again (`settleClosed()`); a window closed while others
  stay open is kept. The layout's scrollback prune spares every id the list names. An item loaded
  from the file has no live window or sibling and reopens as a tab of the window that asked. A
  conversation that is already open in another pane is not resumed twice: the pane comes back
  with its directory and text and starts a new one. Not recorded: turn transcripts and the
  Settings pane, which are views onto something still open.
  `serializeNode()` writes these
  nodes and `buildNode()` / `createPane()` rebuild them; the saved window layout below uses the
  same shapes, so there is only one layout format in the app.
- Closing a window asks for confirmation when it has more than one pane or anything is busy.
- **Pane button row** (`PaneChrome`, a child of each leaf created in `syncChrome()`): shown for
  the leaf under the mouse (application event filter, Enter/MouseMove). There is **one** new-pane
  button, ⊞ (card #803C; it replaced ⬓+ and ◫+). It runs `pane.newByMouse`, which is not a
  Keymap action: `splitToward(Right)` at once, like Ctrl+E but without the two-second arrow window
  (#78BN) — the pointer is already in hand, so the pane is placed by dragging its header. It
  toasts "New pane · drag its header to place it" and, per the hint registry (`pane.new.mouse`),
  "Next time: <live `pane.splitRight` keys> · new pane". The tooltip shows the
  `pane.splitRight` keys through the button's `keysFrom` property. The other buttons run the same
  actions as the keys (`pane.moveToNewTab`, `pane.close`).
  `PaneChrome` has no `Q_OBJECT`, so it is found with `dynamic_cast` (`chromeOf`), never
  `findChild<PaneChrome*>` (that matches any `QFrame`, such as the transcript panel).
  `showChromeFor()` records the wanted leaf **before** hiding or showing anything: `hide()` and
  `show()` make Qt deliver synthetic enter/leave events for whatever the change put under the
  cursor, and those re-enter the same filter. Acting on them with the old state still in place
  recursed until the stack ran out (a crash when the ⇱ button moved a pane out from under the
  mouse), so a nested call only notes what it wants and a bounded loop applies it.
- **Moving without destroying.** `takeLeaf()` detaches a leaf (collapsing a one-child splitter,
  removing an emptied tab, closing an emptied window) and leaves it parentless;
  `insertBeside()` / `adoptLeafAsTab()` / `adoptPage()` put it back. Shells, workers and
  conversations keep running. Pane callbacks resolve their window at call time
  (`windowOf(pane)`), so nothing has to be rebound when a pane or tab changes window. A leaf is
  hidden, reparented and shown again inside one turn of the event loop, and Qt gives it several
  sizes on the way, so a terminal view must not follow the size it has while hidden —
  `TerminalView` applies its grid from a zero-timer (`scheduleGeometry()`), or the emulator is
  reflowed to one row and the pane comes back blank.
- **Keyboard moves** (`pane.moveLeft/Right/Up/Down`, default Ctrl+Alt+arrows, unbound in the
  Warp preset where those keys focus panes): the neighbor is found like focus movement; adjacent
  siblings in a splitter of that orientation swap, otherwise the pane docks on the neighbor's
  near side, so repeating keeps moving it. A left/right move opens the twin of #78BN's placement
  window (#Q7Y9): for two seconds the Move-down *action* — Ctrl+Alt+Down by default, whatever the
  user bound — docks the pane beneath the neighbor it moved toward instead of moving it down
  (`armBeneathDock` / `dockBeneathNeighbor`; the window closes without consuming anything on any
  other action, on any key the keymap does not bind to one of the three moves — Ctrl+C and Ctrl+L
  at the shell included — and on a click, a scroll or a tab change, and it only ever acts on the
  tab that is on screen). Dragging a pane onto another's bottom edge hints that chord
  (`pane.dockBeneath`), naming the move that takes the pane toward its drop anchor; the chord's
  own arming line is a registry hint (`pane.dockBeneath.chord`), so it stops after a few showings.
- **Layout rules** live in `src/PaneLayout.{h,cpp}` (library `relay-panes`, tests
  `tests/panelayout_test.cpp`): `neighborIndex()` (which pane is on that side, used by focus and
  by moves), `swapInSplitter()`, `dropEdge()`, and the chord's two decisions `moveToward()` (which
  move takes a pane toward its neighbour) and `chordKeyKeepsWindow()`. `swapInSplitter()` is one
  `QSplitter::insertWidget` call in either direction, because that call **moves** a child the
  splitter already owns and numbers the index as if it had been taken out first — "finishing" a
  move toward the end with a second insert of the neighbour puts both back where they started,
  which is what made Ctrl+Alt+Right and Ctrl+Alt+Down do nothing.
- **Drag:** the grip tracks the mouse itself (no `QDrag`, because a terminal accepts text
  drops). `dropTarget()` picks the nearest edge of the leaf under the cursor or a `QTabBar`; a
  translucent `dropZone` frame shows the half that will be taken. Esc cancels. Dropping on the
  half two neighbours already share means "stay here", so nothing moves.
- **Tabs:** a "+" button placed after the last tab, a ⧉ left-side tab button shown on hover and a
  context menu run `tab.new` / `tab.moveToNewWindow`; the tab page moves to
  `WindowManager::newEmptyWindow()`.
- Typing `exit` closes the pane. A shell stopped for memory keeps the pane open (section 13).

### Window header (Relay draws its own title bar)

There is no OS title bar: `RelayWindow` sets `Qt::FramelessWindowHint` and the tab row is the
title bar, with two `QTabWidget` corner widgets on it (`buildWindowChrome`):

| Corner | Holds |
|---|---|
| Top left | The Relay icon |
| Top right | Bell (notification centre); then one button per tool pane — Actions, Sessions, Switchboard, and the gear for Options — each drawn with its pane's own header glyph (`relay::chrome::paintTypeGlyph`) and running the pane's action, so it behaves exactly as the key does and its tooltip and "Next time" hint name that key; then minimize, maximize/restore and close |

`ChromeButton` paints each glyph with `QPainter` instead of using a font character, so the header
does not depend on an emoji font and hover, disabled and close-button colours come from the theme.

- **Moving and resizing.** The window keeps `kFrameMargin` (5 px) of padding; `edgesAt()` turns a
  press there into a resize and `headerDrag()` turns a press on the empty tab row into a move,
  with a double-click maximizing. Both call `startSystemMove()` / `startSystemResize()` first, so
  the window manager's snapping and tiling still apply; when no WM takes the drag, Relay moves or
  resizes the window itself from the press point. Presses on a tab, on `+` or on a header button
  are left to those widgets.
- **Maximized** windows drop the padding and show the "restore" glyph (`changeEvent` →
  `updateChromeState()`).
- **Fallback.** `window/native_frame` (Actions › System title bar) keeps the system decorations;
  the header then shows only the bell and the gear, and the tab row no longer drags the window.
  It applies to windows opened after the change.

### Pane titles and tab labels

`src/PaneTitles.{h,cpp}` (library `relay-titles`, tests `tests/panetitles_test.cpp`) and protocol
section 18. A pane's header shows what that pane is **doing**, not where it lives: a phrase of at
most six words the worker writes on a cheap side call (the `chores` role) after the first turn, and
then only when the work has moved on — five turns later, or after a compaction. It is kept in the
session file, so the conversation list and the resume picker show the same text, and it falls back
to today's first-prompt title when no model is configured or the call fails.

- **Naming a pane by hand.** Double click the header title, or `/rename <name>`; `/rename` with no
  argument opens the same in-place editor (Enter commits, Esc cancels). A hand-set name is fixed:
  the model never overwrites it, and the small `auto` badge beside the title disappears. Clearing
  the field hands the pane back to the model, which writes a fresh title straight away.
- **Tab labels** are derived from the pane titles, so they cost no extra title call: one phrase when
  the panes are on the same work, the titles joined with `"; "` when they are not ("Fixing pane
  drag; Release notes"), elided to the tab. Which of the two is a cheap `tab_label` judgement on the
  same `chores` role, asked only when a pane title actually changed and answered offline
  (`relay::titles::relatedText`) until it comes back or when no model is configured.
- `/rename-tab <name>`, or a double click on the tab, names the tab by hand; it too is fixed until
  the field is cleared, and it follows the tab into a new window.

### Pane types, pane states and remote sessions

Cards #SPBN and #XM0T. The rules are in `src/PaneStatus.{h,cpp}` (library `relay-panestatus`, tests
`tests/panestatus_test.cpp`); the painting is in `src/PaneChrome.h` (`relay::chrome`,
`PaneTypeBand`, the status widgets in `PaneChrome`); the poll and the tab icons are
`RelayWindow::refreshPaneStatus()`.

**The `paneType` property is the contract.** A leaf in the splitter says what it is with one dynamic
property on the leaf widget (the `ToolPane`), read at paint time:

| `paneType` | Band | By type | By group |
|---|---|---|---|
| unset, `terminal`, `explorer`, `preview`, `plan`, `diff` | none — plain | | |
| `board` | jacks, SWITCHBOARD | brass (`warning`) | tools: brass |
| `options` (and `settings`, until nothing sets it) | gear (as on the title-bar button), OPTIONS | green (`success`) | tools: brass |
| `actions` | bolt, ACTIONS | green, shared with Options | tools: brass |
| `sessions` | list, SESSIONS | the shell blue (`shell`) | tools: brass |
| `projects` | jacks, PROJECTS (the project picker, #916B) | brass | tools: brass |
| `sharing` | phone, SHARING | brass | tools: brass |
| `subagent` | tree, SUBAGENT | violet (`agent`) | agents: violet |
| `turn` | bubble, AGENT TURN | violet (`agent`) | agents: violet |
| anything else | a square, the type's own name | brass | tools: brass |

`paneLabel`, when set, replaces the band's text. `ToolPane` gives a pane that set nothing the
default for its kind when it is first polished, and reacts to a change of either property by itself
(`ToolPane::event`), so a new pane type needs `tool->setProperty("paneType", "…")` and nothing else:
the band is inserted at the top of the pane's layout, the chrome's buttons move onto it, and the
view's own first row gets its full width back (`PaneChrome::syncHeaderInset`). Tints are a 10–13 %
mix of the hue into the pane's background, the glyph at least 3:1 and the label at least 4.5:1 on
it in every shipped theme (the test checks). `appearance/pane_colours` = `type` (default) | `group` |
`off`; `off` keeps the band in neutral ink, because the band is the pane's name (the Options pane
draws no title of its own). It is cached: whoever writes it calls `PaneChrome::refreshAll()`.
The focused pane keeps its outline; the band is inside it and never recolours it.

**States.** A terminal pane is in exactly one `relay::panestatus::State`, least urgent first:
idle (ring), running / subagents working / agent working (the Relay mark itself, card #4E13 — the
terminal's blue for a command, the agent's violet for agent work), recommends a command (prompt
chevron), done (tick), failed (disc with a cross), needs you (diamond with `!`). Every still state
has its own shape, so none of the news depends on colour; the live states are one mark in the
work's own colour, and the word beside the title says whose work it is. `Pane::statusFacts()` reads
what the pane already keeps — `m_agentBusy`, the subagent model's live count, `processBusy()`, the
screen prompt / waiting / password state, a `run_in_terminal` prefill in the prompt box — and the
last finished turn (`m_finishSerial`, its outcome, and whether the reply ended on a question or left
a command the agent waits on). "Needs you" is a program asking for input, a handed command the
agent's next turn waits on, or an unseen turn that asked you something. Done, failed and asked are
news until the pane has been the focused pane of the current tab in the active window for 1.5 s
(`PaneChrome::seenSerial`). The header shows the pane's own state as a glyph before the title; the
tab icon is the most urgent state among its panes (a tab with no terminal shows its first special
pane's type glyph). The poll runs every 400 ms and repaints a tab icon only when it changes.

**Live states** (card #V8KT, owner 2026-09-19: "its not clear enough if a pane agent or program is
running… the icons / anims should use blue for terminal work happening and violet for agent work
happening"). Running, Working and Subagents are work happening *now*, and their marks move
(`relay::panestatus::isLive`): the header glyph is the Relay mark itself, blinking on the waiting
dots' 600 ms clock (card #4E13: full size and a dimmed step alternating — `pulseScale`, a scale,
never an opacity, so the ink keeps its contrast), the state's word sits beside it ("Command
running", "Relaying…", "Subagents working" — `stateLabel`) in the work's own colour lifted to 4.5:1
on the header's ground (`stateText`) — shortened to one word (`stateLabelShort`: "Running",
"Subagents") when the row has run out of room for the full one, and left out rather than cut in
half when even that does not fit, since the glyph and the tooltip still say it; which of the three
it shows is rung 3 of the give-way ladder above, not the word's own reading of its width — and a
tab with anything live carries a blinking corner dot
in that colour (`liveMarker`: the agent's violet whenever agent work — a turn or subagents — is
live, else the terminal's blue) even when its icon is showing more urgent news, so "is something
running over there?" never waits for the icon's turn. The dot yields its corner to the ssh mark
and takes the one across. The same word sits bold above the prompt box (`Pane::PaneBusyLine`, card
#4E13): "Relaying – <action>… · N s · Esc stops" in the agent's violet while a turn runs — the
action a gerund of the live tool call, or what the pane waits for — and "Relaying – <program>…" in the
terminal's blue while a program owns the terminal. The desktop's reduce-motion signal — a cursor
flash time of 0, the same one that stills the caret and the waiting dots — draws every live mark at
rest, and the news states (done, failed, needs you) never move: they pull the eye by being news.
Every live mark reads its step off the wall clock (`kStepMs`, 600 ms) rather than counting one per
pane, and each pane's timer is re-armed to the next boundary, so panes that went live seconds apart
blink together instead of each on its own beat.

**The subagent badge** (card #YMSR, owner 2026-09-19: "in the pane header, add a badge with a
number for number of subagents, if applicable") is how many agents that pane's agent has
running, in the header beside the state's word: a violet chip carrying the agent's own
four-point star and the count. It counts *live* subagents — waiting or running — which is the
same number `Facts::liveSubagents` resolves a state from, so the badge and the state's glyph
cannot disagree about the pane; an agent that finished is not work happening now, and the strip
under the composer is where the ones that ended are read. Zero is not a "0":
`relay::panestatus::subagentBadgeText` returns nothing and `PaneChrome::PaneSubagentBadge`
hides itself, which is what "if applicable" asks for and what keeps a pane that has never
started a subagent looking exactly as it did before. Its tooltip says the count in words and
teaches the key that opens the subagents pane (Alt+A, `agent.subagentPane`, read live as the
folded strip does). It is a read-out, not a button: a press on the header moves the pane. It never
gives way either: however narrow the pane, the badge is whole or it is not there, and the give-way
ladder above takes its room from the other elements in the order the owner set.
`relay::panestatus::subagentBadgeStyle` is the chip's fill, hairline and ink on whatever ground
it lands on — the pane's background, or the ssh band's fill, which is where a mid-tone ground
exposed `atLeast` choosing its pole by `isLight`'s 0.35 split rather than by measured contrast
and returning white under 4.5:1; the helper now takes the pole that really is further from the
ground, and the test asserts the number's 4.5:1 in every shipped theme on both grounds.

**Resource meters** (card #D03W, owner 2026-09-19: "would it be possible to have small X%, X%
indicators for CPU and RAM usage by pane and tab?"). What a pane costs this machine: CPU and
memory summed over the pane's two process trees — the shell its pty spawned with whatever that
shell is running (an ssh client included), and the pane's agent worker with its children — read
from `/proc` on the same 400 ms poll that resolves the states, so every pane is measured over the
same interval. The rules and the arithmetic are `src/PaneUsage.{h,cpp}` (`relay-paneusage`,
`relay::usage`, tests `tests/paneusage_test.cpp`): `/proc/<pid>/stat` and `/proc/<pid>/statm` are
summed over a tree walk (capped at 256 processes), CPU ticks over the interval become a percentage
*of the machine* (100 = every core), resident pages a percentage of physical memory.
The walk is `relay::usage::walkTrees()`, and it is **the** `/proc` walk in the
program: it reads `/proc/<pid>/task/<tid>/children` for *every* thread, not the main thread's list
alone — a subprocess the Python worker spawns from a worker thread is parented to that thread, so
a walk of `task/<pid>/children` never saw it and its CPU went unmeasured until the worker reaped
it — and `Pane::programWaitingForInput()` goes through the same function on its own 250 ms poll,
asking for `Detail::PidsOnly` so that poll still opens no `stat` or `statm` and keeping its own cap
of 64. `relay::usage::setProcRoot()` points the walk at a directory a test built, which is how a
process under a non-main thread can be arranged on purpose. Each process contributes
`cutime`/`cstime` as well as `utime`/`stime`, so the
children it has already reaped still count — without that, a build whose compilers each live for
less than one poll interval reads as an idle pane. A poll that reads nothing drops the baseline
rather than zeroing it, so the reading after a blind tick is a fresh baseline and not a spurious
100 %. The meter is the last thing in the header to give way, and all it gives is its memory half
and the separator with it — rung 5 of the give-way ladder — so a narrow pane still says what it is
costing in CPU. Memory is a sum of resident sets, which counts pages two processes share more than once;
the tooltips say so.

A reading also carries **who it is made of**: per-process CPU (that process's own tick delta
between two polls, keyed by pid *and* `starttime`, so a pid the kernel handed out again is a new
process and not a whole lifetime of ticks in one interval) and resident memory, sorted by CPU then
memory, cut to the busiest five and to the rows whose two percentages do not both round to zero
(`relay::usage::topProcesses`). `combined()` merges the panes' rows the way it sums their numbers,
so a tab names the tab's busiest processes and not each pane's. The two roots are named on their
lines — "shell" and "agent worker" — and everything else by its `comm`. A child that has already
exited still counts toward the sum, through its parent's `cutime`/`cstime`, and has no line of its
own: there is no longer a process to name. The breakdown shows in the two places with room for it,
one `<name> · cpu <cpu>% · mem <mem>%` per line: the usage chip's tooltip (written when the tooltip
is asked for, since the lines move faster than the number the chip paints) and the usage section of
the tab's tooltip. The chip and the label stay the sum alone.

**One wording, everywhere** (card #6BGA, owner 2026-09-19 on the mock-ups in
`docs/qa_evidence/2026-09-19-usage-meter-numbers/`: *"the cpu / mem bar things are ugly and
unintuitive. i think it should be numbers"*). A reading is written in exactly one place,
`relay::usage::readingText()`: **`cpu 12% · mem 3%`**, plain words and whole percents. The three
surfaces print that same string — a chip in the pane's header row right of the state's word
(`PaneChrome::PaneUsageChip`, the body face, the words in the header's muted ink and each number
warning at 60 % and erroring at 85 %; `readingParts()` is the same string cut into the pieces it
colours separately), a `  ·  cpu 12% · mem 3%` suffix on the tab label (summed over the tab's
panes), and the tag on the conversation's row in the Sessions pane
(`SessionManager::setLiveUsage`, fed by the poll like `setOpenSessions`; it is the row's last
badge, since it is the only one whose width moves while the row sits there) — and so do the
tooltips, whose first line is `describe()`: the same words with the byte figure added,
`cpu 12% · mem 3% (2.1 GiB)`. Before #6BGA the chip drew a 13 px processor die and a 13 px memory
module before two bare percentages and the tab carried its own `· 12% / 3%`, so the two surfaces
disagreed about how to say the same thing and neither said which number was which; at that size
the die's pins and the module's legs read as bars, which is the complaint. A half with nothing
to say is left out rather than drawn as "0%", and what is left is the same grammar shortened
(`· cpu 20%`), never a bare number. CPU
speaks from half a percent; memory has to clear 256 MiB *and* half a percent, so an agent worker
idling on 60 MB leaves the chip off a small machine as well as a large one. A pane using nothing
shows nothing anywhere: an idle terminal looks exactly as it did before, and the meters are local
— a remote pane measures the ssh client, not the far machine. What a tab label shows is held
still until the reading moves three points or a second has passed
(`relay::usage::labelShouldFollow`), and only the tab whose text moved is relabelled.
`appearance/pane_usage` turns off all four — chip, tab suffix, tab tooltip line and Sessions tag
— through `relay::usage::metersEnabled()`, which is the one place the key is read; with it off
`Pane::refreshUsage()` drops the pane's baseline and walks no `/proc` at all, so the setting stops
the measuring and not only the labels.

**Remote sessions** are a safety signal and ignore the colour setting. `Pane::remoteCommandLine()`
is the foreground process group's command line while it is `ssh`, `mosh`, `mosh-client`, `telnet`
or `autossh` — read live, so it is right when ssh was started with the prompt box hidden and clears
the moment ssh exits or is suspended; ssh run inside a local tmux is not visible from here. The
title row gets a hatched band in the error hue with a firm line under it and a `⇄ user@host` chip
(`relay::panestatus::remoteHost` reads the destination out of the arguments), and the tab icon gets
a red corner mark, or the ⇄ itself when nothing more urgent is going on. The pane also carries
`remoteSession` = the host, for anything else that wants to know.

**Shared with a phone** (Relay's remote share, section 19) is a different state and gets a
different mark: a `phone` chip in the title row in the shell blue, beside the share chip under the
prompt box that already said so.

### Notification centre

`relay::NotificationCenter` (`src/Notifications.*`, the `relay-notifications` library) is one
in-memory list per process, shared by every window: newest first, a kind per entry
(info / success / warning / error), an unread count for the bell badge, dismiss, clear, and a
200-entry cap. `NotificationsPopup` renders it under the bell and marks everything seen on open.

`Pane::notify()` is the single entry point. The centre always keeps the entry; `notify-send` and
`QApplication::alert()` still only fire when Relay is not the active window and
`notifications/desktop` is on. What posts: a command that ran longer than 30 s, a shell or command
killed for memory, a password prompt waiting, a finished subagent, and the pane's own agent turn
finishing, failing, or ending on a question ("Agent needs you", also when it left a command in the
prompt box that its next turn waits on) — only when the user is not watching that pane
(`Pane::watched()`: the active window, the current tab, the focused pane); #XM0T. Working, running
and subagents are the live marks (a blinking glyph, its word, the tab's dot — see "Pane types,
pane states and remote sessions"); a suggested command is a glyph only. Each entry carries the
pane's session token, so clicking it
calls `WindowManager::focusPane()` → `RelayWindow::revealPane()` and lands on that pane in
whatever window it now lives.

### Reopen where I left off (saved window layout)

`src/WindowState.h` + `WindowManager`. Relay keeps one layout file per user,
`$XDG_DATA_HOME/relay/state/windows.json` (0600), and reopens it silently on start — no prompt,
nothing is re-run.

```json
{"version": 1, "saved": 1758000000,
 "windows": [{"geometry": [x, y, w, h], "screen": "DP-1", "current": 0,
              "titles": ["alpha  ·  2", "gamma"], "tabs": [<layout node>, …]}]}
```

- **Tabs** are the same layout nodes as "restore last closed" above, so a restored pane comes back
  with its directory, workspace, engine and engine core, model role, provider preset/model/effort,
  agent mode, input mode, session id, and its tool panes (explorer/preview/plan) and their paths.
  `titles` is informational: Relay derives tab titles from the panes.
- **When it writes.** `RelayWindow::updateTitles()` (every split, close, tab change, directory
  change and model change), `splitterMoved`, `moveEvent`/`resizeEvent` and `closeEvent` all call
  `WindowManager::scheduleSave()`, a 1 s single-shot debounce; `aboutToQuit` flushes. A crash
  therefore loses at most the debounce window. Writes go through `QSaveFile` (temp file + rename,
  0600), so the file is never seen half written.
- **Quit versus closing a window.** `closeEvent` snapshots the whole set *before* the window
  leaves it and settles on the next event-loop turn: if no window is left (a quit) the whole
  snapshot is written, otherwise the remaining windows are. So quitting reopens everything that
  was open, while closing one window of several drops it for good. An empty set is never written.
- **Two Relays at once.** The file has one owner, a `QLockFile` at
  `$XDG_RUNTIME_DIR/relay/windows.lock` held for the life of the process. Only the owner restores
  on start and only the owner saves, so a second Relay opens a plain window and leaves the layout
  alone; if the owner quits, the next save by a still-running Relay takes the lock over. Without a
  runtime directory there is no lock and the last writer wins — still atomically.
- **Restoring.** `usableWindows()` drops records that cannot be rebuilt (unknown node kinds, empty
  splits, trees nested deeper than `kMaxDepth`). `clampToScreens()` puts a window back on the
  screen it was saved on, or, when that screen is gone, on one that still exists, shrinking it to
  fit. Under Wayland only the size is applied (clients cannot place their own windows). A pane's
  directory falls back to its workspace, then `$HOME` (`resolveDirectory()`). Each pane asks the
  worker to `resume` its session id once the agent is configured and prints the usual
  `Session loaded: "…" · N turn(s)` line plus the open-task count; a session whose file is gone
  prints one note and starts fresh. The shell is new, but the pane's **terminal text comes back**
  with it (below).
- **Scrollback across a restart** (owner report, 2026-09-18; before this a restored pane came back
  empty). Each pane node carries a `scrollback` id and its text lives beside the layout, one file
  per pane: `$XDG_DATA_HOME/relay/state/scrollback/<id>.txt` (0600), written with the same
  temp-file-and-rename as `windows.json`. The pure part is `relay::windowstate`
  (`scrollbackPath`, `clampScrollback`, `writeScrollback`, `readScrollback`, `scrollbackIds`,
  `pruneScrollback`); the pane side is `Pane::saveScrollback()` / `replayRestoredScrollback()`.
  - **Text, not cells.** What is saved is `scrollbackText()` plus the visible screen (skipped while
    a full-screen program owns it), without colour. The engine hands the host text, and an
    absolute colour replayed into a new pane is burnt into its history — a terminal cannot
    recolour its scrollback (`src/MarkdownAnsi.h`), so text saved under one theme would come back
    in that theme's colours for good. Anything worth keeping later must be *indexed* SGR, which
    the engine resolves at paint time from the live theme.
  - **Bounded.** At most 5,000 lines and 512 KiB per pane, newest kept, trailing blank rows
    dropped (`kScrollbackMaxLines` / `kScrollbackMaxBytes`, both well inside the engine's 20,000
    line scrollback). A pane whose text is empty leaves no file, every layout write prunes the
    files of panes that are gone, and forgetting the layout deletes the whole store.
  - **When it writes.** Only on the way out — `noteWindowClosing()` (a quit catches every window
    while its panes are still alive) and `aboutToQuit` — never on the 1 s debounce, which must not
    read thousands of lines per pane. A crash therefore loses the scrollback, not just the
    debounce window.
  - **What it looks like.** At the restarted shell's first prompt the pane erases the prompt line,
    prints the saved lines plain between two muted rules (“— scrollback from this pane's previous
    shell —” / “— end of restored scrollback; this shell is new —”) and sends an empty line so the
    shell prints a fresh prompt underneath — `redrawPrompt()` is a no-op here, because Readline's
    idea of where its prompt sits has just scrolled away. The rules are Relay's own chrome and are
    filtered out of the next save, so they do not stack up over restarts, and restored lines go
    through `sanitize()`, so a hand-edited file cannot drive the terminal.
- **Controls.** The setting `windows/restore` ("Reopen windows on start" in the palette, default
  on; turning it off deletes the file and the saved scrollback), `relay --fresh` (ignores the file
  once, keeps it), and the palette action `windows.fresh` ("Start a fresh window set": deletes the
  file and stops saving for the rest of the session, with a shortcut hint pointing at `--fresh`). The
  recently-closed list (`state/closed.json`) follows the same switch: neither read nor written
  while restoring is off, and "Start a fresh window set" deletes the file with the scrollback it
  names, though what was closed in this run can still be reopened from memory.

### Prompt history across a restart

The prompt box's Up/Down history used to live in the `RichEditor` and die with the pane (owner
report, 2026-09-18: "conversation history isnt persisting on exit and re-open. i cant do up arrows
to see what i did before"). It first became one shared file, which the owner then corrected
(2026-09-19: "the up/down history seems to be getting commands from other panes, not just mine" —
"i want pane histories for up/down"). It is now one file per pane:
`$XDG_DATA_HOME/relay/state/prompt-history/<id>.txt` (0600, in a 0700 tree beside `windows.json`),
`relay::prompthistory` in `src/PromptHistory.h`.

- **One history per pane.** The file is keyed by the pane's layout id — the same `scrollback` id
  that names its saved terminal text (`src/WindowState.h`) — so Up and Down walk only what was
  typed at that pane: the pane beside it has its own file, and a pane opened now starts empty. The
  id survives a restart (the pane is restored with it) and a close-and-reopen ("restore last
  closed" carries the node, id and all), which is what keeps the history the pane's own while
  still outliving it. A browse that starts re-reads the pane's file first
  (`RichEditor::refreshHistory()`, on the Up that leaves the draft), which is how a prompt written
  at the door by a paired phone is there; the file is only re-read when its size or mtime has
  changed, so an idle box does no work.
- **Appended as it is submitted**, never on the way out: one O_APPEND write per line, so a Relay
  that is killed rather than quit loses nothing and two Relays running at once interleave their
  lines instead of overwriting each other. The whole file is rewritten only to trim it, once it
  passes 512 KiB, back to the newest 1,000 entries.
- **One line per entry.** A prompt may be several lines, so a newline is stored as `\n` and a
  backslash as `\\`; a hand-written file of plain lines reads back as it looks. An entry longer
  than 10,000 characters stays in the pane's live history but is not written.
- **What never reaches it.** A line written to a running program's stdin and a password are
  refused upstream by `relay::input::retainable` (`src/InputPolicy.h`), and a password never
  enters the composer's document at all — it is typed in the separate masked field.
- **A line typed away from the desktop counts too** (owner, 2026-09-18: "these should always be
  saved"). `Pane::submitRemote()` writes a prompt from a paired phone, tablet or guest browser
  straight to that pane's file — before it is routed, so one that bounces off an unconfigured
  agent is still recallable, and never through the composer, whose draft and browse position
  belong to whoever is at the desk. It is recalled in the pane it was sent to. A remote *shell* command was already kept, by the same path as a local
  one. `append()` stores a line identical to the one already at the end only once, which is what
  keeps the routed case (written at the door, then remembered again when the command runs) to a
  single entry — a prompt box can only dedupe against its own last line.
- **Forgetting it.** The palette action `history.clear` ("Clear prompt history") confirms,
  removes the whole per-pane directory (and the pre-2026-09-19 shared file, should one be left)
  and calls `RichEditor::forgetAllHistory()`, which drops the copy every open prompt box holds —
  including one part-way through a browse, which would otherwise keep offering what was just
  forgotten. Text already in a box is left alone: it is the person's now.
- **Bounded like the scrollback store.** Each pane's file is trimmed to its newest 1,000 entries
  once it passes 512 KiB, and files whose pane is gone for good are pruned whenever the window
  layout is written — the saved layout and the recently-closed list are the ids worth keeping,
  the same list that prunes `state/scrollback/`. "Start a fresh window set" drops the store with
  the layout: without the layout, no pane is coming back to claim its file.

## 4. Keyboard: Keymap, presets, palette

### Shortcut hints

`src/Hints.*` (`relay::ShortcutHints`) decides whether a hint may show: on by default
(`hints/enabled`, toggles in Agent options and the Shortcuts section), at most `limit` (3) times
per id, a per-id cooldown (600 s) and a global gap of 20 s, counts in QSettings `hints/`.
`mayShow()` only asks; `recordShown()` counts a showing and starts the cooldown and the gap;
`shouldShow()` is both at once, for a hint drawn the moment it is allowed (the placement prompt).
`Pane::hint()` (and `RelayWindow::hint()`, which hands it to the active pane) queues a 5 s toast
and records the hint only when that toast actually appears, asking the gates again then, so a hint
waiting behind other toasts costs none of its showings. Idle tips work the same way
(`nextIdleTip()` picks, the pane records on display, and only into a pane with no toast up).
Toasts are events and queue: while one is up the next waits, the one up keeps at least 1.5 s (its
own time if shorter), identical consecutive toasts collapse. The agent turn clock is state, not a
toast: while a turn runs, the line above the prompt box says "Relaying – <action>… · 48 s · Esc
stops" in the agent's violet, left-aligned with the prompt text and in the normal weight
(`Pane::PaneBusyLine`, cards #4E13, #HQ2B; the spaced en dash after the verb is #RR0G) — the
action a gerund of the live tool call ("thinking", "reading src/Pane.h") — and a program that owns
the terminal gets the same line in the terminal's blue, "Relaying – <program>…". **Waiting on
background work** (cards #V7QD and #KP4M): when the pane's main ("orchestrator") agent is blocked
on the subagents or the jobs it started, the prompt box's own placeholder says so — "waiting for
2 subagents, 1 job . . .", the dots growing every 600 ms — and the busy line says "Relaying –
waiting for 2 subagents… · 48 s · Esc stops" instead of "thinking". `relay::panestatus::waitingLines`
(`src/PaneStatus.{h,cpp}`, beside the pane states, because it belongs to neither model) holds the
rule: a kind counts when some of it is live *and*
either the main agent is explicitly blocked on it — `agent_wait`, a live foreground subagent, or a
`command_output` call, which waits on a job — or no turn of its own is running. Only the kinds
actually waited on are named, so a turn blocked on `agent_wait` while a background job also runs
says "waiting for 2 subagents". A turn that started background work and carried on working says
nothing, or the line would be up for most of every turn. `Pane::refreshBackgroundWait` draws it
through `RichEditor::setPlaceholders`, so Qt stops drawing it the instant a steer is typed and the
narrow-pane rungs ("2 subagents, 1 job . . .", then "waiting . . .") come for free. The timer only
runs while the line is on screen, and a desktop cursor flash time of 0 ("do not blink", the same
signal `RichEditor::setCaretColor` takes the caret's blink from) draws the dots in full and starts
no timer at all. `nextTime(shortcut, what)` builds the
text from the live Keymap, so rebinding changes the hint and unbound actions get none. Current
triggers: toolbar and palette activations of actions with shortcuts, pane buttons, the tab "+",
tab close and ⧉ buttons, the plug's "Join with a code…" (→ `/join CODE`, `remote.join.button`), clicking into another pane, mouse model/effort/mode pickers, clicking
the directory line (`@`), the palette's Update action (→ `/update`, `update.palette`), the queue × (on a steer row → ↑ then Shift+Delete), dragging a queued row
(→ ↑ then Ctrl+↑↓; dropped above the steers → Ctrl+↑ sends it at the next tool call), `/shell ` and `/agent ` (`!`, `*`), `/help` (→ `?` in an
empty prompt box), palette rewinds, pane
drags, the first `relay://` link, a click on the pane's ⓘ button (→ `agent.info`, Alt+I; → `/status` only while nothing is bound), the Tasks chip and `/tasks`, `/requests`, `/todos` (→ `agent.requests`, Ctrl+Shift+K), Continue
from the link or palette (→ `/continue` or `agent.continue`), wrong-mode submissions (section 5,
"Wrong-mode hints": a request that failed in terminal mode or a failing shell command in agent
mode → `input.toggle`, with the mode chip flashing), dropping an image on the prompt box (→ the
paste shortcut) and "Screenshot this pane" from the palette (→ `agent.screenshotPane`,
Ctrl+Shift+G), running an alias from the palette (→ `/name`, and for a command the name typed in
terminal mode), asking for a skill by name in a prompt that says "skill" (→ `/name`), renaming a pane or a tab by double click (→ `/rename`, `/rename-tab`),
starting a card edit in the Switchboard with the Edit button, a click on the title or a
double-click in the text (→ `e`), a card's Discuss, Plan, Execute and Verify buttons (→ Enter, `p`, `x`, `v`; `board.verify` is #T71W's, on a card in a QA lane),
the program banner's "Let the agent drive" / "Take over" buttons (→ `program.delegate`, `control.human`), a click on a running-agents row or its folded line (→ `agent.subagentPane`, Alt+A, or ↓ then Enter), a click on a task row of the strip under the prompt and the Tasks chip menu's task rows (→ ↓ then →, `tasks.strip.open.mouse`), the subagent pane's "← main agent" (→ `agent.subagentPane`), a turn that printed tool-call
lines (→ click a ▸ line to unfold it, `Ctrl+Shift+Return` for the nearest) and a diff pane opening
(→ n and p step through the hunks), the share chip on a pane that is already shared (→ the palette, then "Sharing", because
`pane.sharing` deliberately has no key of its own), answering an `ask_user` card by typing an
option out in full (→ its number, `question.number`; an answer in the user's own words is the
card working and is never corrected), dropping a pane on another's bottom edge (→ the move
toward that pane then Move-down, `pane.dockBeneath`; the chord's own arming line is
`pane.dockBeneath.chord`), the first reasoning delta of a turn (→ a click or `agent.thinkingPanel`
folds it away, `thinking.fold`), closing a focused Switchboard by its button or the palette
(→ `board.toggle` again, which closes it when it already has the focus, `board.close`; card #4XR8),
and rotating idle tips 4 s after a finished agent turn with an
empty prompt box. **Every new feature with a shortcut should add a hint on its slow path** (rule
in `WARP.md`); tests in `tests/hints_test.cpp`.

Palette items also match hidden alias words (`paletteAliases()`, keyed by label/key/section
substrings, half weight), e.g. "undo" → Rewind, "reasoning" → effort, "detach" → move actions.

`Keymap` (`src/Keymap.h`) is a process-wide registry of named actions. Each action has an id,
a category, a description and default keys.

| Source, lowest to highest priority | Where |
|---|---|
| Relay defaults | `Keymap::Keymap()` `add(...)` calls |
| Preset table (`relay`, `warp`, `vscode`, `konsole`) | `Keymap::presetJson()`, researched in [KEYBINDING-PRESETS.md](KEYBINDING-PRESETS.md). Actions missing from a table keep the Relay default. |
| User overrides | `~/.config/RelayTerminal/relay/keybindings.json` `bindings` |

The file also holds `preset` and `program_keys` (`shift-only` default, `all`, `none`).
A `QFileSystemWatcher` watches the file and its directory, because atomic replacement
drops a plain file watch. Unknown keys and conflicts are reported in the status bar.
Symbol keys match with or without Shift, because shifted punctuation differs by layout.

Dispatch: `RelayWindow::eventFilter` handles `ShortcutOverride` and `KeyPress` for widgets in
its window and runs `runAction(id)`, which the toolbar and palette also use.

- `control.human` (Ctrl+H), `input.toggle` (Ctrl+I) and `agent.interrupt` (Ctrl+Alt+Enter)
  act only from the composer. In the terminal those keys stay Backspace, Tab and Enter.
- While a foreground program owns the focused terminal, only keys allowed by
  `program_keys` act. The default lets Ctrl+Shift combinations and F-keys through to Relay.
- Terminal clipboard: Ctrl+C invokes the display's `copyToClipboard` slot and treats a
  clipboard change as proof of a selection (a backend need not have a selection query); otherwise
  the key reaches the shell. Ctrl+V pastes at a prompt and passes through inside programs.
  Optional copy-on-select (`terminal/copy_on_select`), which is not terminal-only — see
  "Copy on highlight" below.

### Copy on highlight

One setting, `terminal/copy_on_select` (Options › Terminal, "Copy on select", off by default),
makes a mouse selection copy itself everywhere text can be highlighted. The key still says
`terminal` because that is where the feature started; renaming it would turn the setting off for
everyone who had switched it on, so it is deliberately frozen (owner's request, 2026-09-18:
"allow copy on highlight in info and other panes").

`src/CopyOnSelect.h` holds the whole behaviour: `relay::copyOnSelectEnabled()` reads the setting,
and `relay::installCopyOnSelect(widget, notify)` puts a `CopyOnSelectFilter` on a read-only text
surface. On a left-button release over the widget it copies the selection to PRIMARY where the
platform has one and to the clipboard, which is what the terminal already did between
`TerminalView::mouseReleaseEvent` (PRIMARY) and `Pane::copySelection` (clipboard). It is
header-only because the surfaces live in a dozen static libraries.

Rules the filter keeps:

- **Mouse only.** A keyboard selection never copies; Ctrl+C is still the way to copy that.
- **Read-only only.** `copyOnSelectText()` returns nothing for an editable `QTextEdit`,
  `QPlainTextEdit` or `QLineEdit`, so the prompt box, the composer, the plan editor, the settings
  inputs and a file preview switched to editing (`FilePreview::setEditable`) are never copied from.
- **Nothing when the setting is off**, and nothing when the selection is empty, so a click that
  clears a selection does not wipe what was on the clipboard. (X11's PRIMARY is still set by Qt's
  own text widgets on any selection, as in every Qt and GTK application; that is not Relay's doing
  and is unchanged by this setting.)
- **Quiet**, except where a surface passes a `notify`: the pane toasts "N characters copied" for
  the reasoning bubble and the program transcript, as it always has.

Surfaces that install it: the pane's reasoning bubble and program transcript (`src/Pane.h`), the
conversation info pane (`src/SessionInfo.cpp`), the conversations pane's preview
(`src/Conversations.cpp`), the turn log (`src/TurnTranscript.cpp`), a subagent's transcript
(`src/SubagentTranscript.cpp`), the diff view (`src/DiffView.cpp`), the file explorer's path line
and the file preview's text, rendered Markdown and info page (`src/FilePanes.cpp`), the Switchboard
card document and its cleanup panel (`src/BoardPane.cpp`), the tasks panel's detail
(`src/RequestsPanel.cpp`), the sharing pane's request text (`src/SharingPane.cpp`) and the share
dialog's address and invite link (`src/RemoteShare.cpp`). Tests: `tests/copyonselect_test.cpp`.

Default window shortcuts:

| Action | Key | Action | Key |
|---|---|---|---|
| New window | Ctrl+N | Close pane → tab → window | Ctrl+W |
| Next / previous window | Alt+Tab / Alt+Shift+Tab | Restore closed | Ctrl+Shift+Z |
| New tab | Ctrl+T | Actions pane / Options pane | Ctrl+Shift+A / Ctrl+Shift+O |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab | Take control (from composer) | Ctrl+H |
| Split right / down | Ctrl+P / Ctrl+Shift+P | Back to the prompt | Ctrl+Shift+H |
| Focus neighbor pane | Alt+Arrows | Native input toggle (same hand-over as Ctrl+H) | F12 |
| Toggle terminal/agent input | Ctrl+I | Restart stopped shell/agent | Ctrl+Shift+R |
| Interrupt agent with prompt | Ctrl+Alt+Enter | Step through links in the output | Ctrl+Shift+L |
| Conversation info (the ⓘ view) | Alt+I | Subagents / Flash / Reasoning panes | Alt+A / Alt+F / Alt+R |
| Activity pane | Alt+Shift+R | | |

The session manager (`/resume`, `agent.resume`) is Ctrl+Shift+Y, Warp's key for its conversations menu. Options and
resume have no plain-Ctrl twin (owner, 2026-09-18): Ctrl+O and Ctrl+Y belong to the shell.

The ⓘ view (`agent.info`, `/status`, `/info`) is **Alt+I** (owner, 2026-09-18). Alt+I is the mnemonic
and it is free: no preset table binds any Alt+letter, so all four presets inherit it (the konsole
preset's Ctrl+Alt+I and VS Code's Ctrl+Shift+Alt+I are different combinations), and Readline leaves
M-i unbound, so a shell keeps the key. Like Alt+A and Alt+R it steps aside for a program that owns
the keyboard.

Unbound by default: `conversations.open`, `files.open`, `terminal.interrupt`, `agent.newChat`,
`agent.stop`, `agent.clearQueue`, `agent.resumeQueue`, `agent.provider`, `input.mode*`,
`keybindings.edit`, `keybindings.reload`.

**Actions** (Ctrl+Shift+A or Ctrl+?). The action catalog (`rootItems()` in
`src/RelayWindow.h`) is the same list the palette overlay used to render: items with a stable key and
either a run function or a submenu (Model, Input mode, Reasoning effort, Aliases, Agents, Log
detail). Since 2026-09-18 it is rendered by the Actions pane (section 12, "Actions pane and Options
pane"): one list of every item with its keys — Recent (from `palette/recent`) first, then Agent,
Terminal, Panes and tabs, Relay (Options…, Reload themes, Open your themes folder, Open the log
folder, Reset shortcut hints), Shortcuts, submenus opened inline under their own header — and the
pane's search box reaches every item and every submenu entry ("deep" finds Model › DeepSeek), with
matching options below them as "Options › …" rows. Enter or a click runs one: the pane closes, focus goes back to the
widget that had it, and the action runs against that pane. Toggles (`stayOpen`) run in place and
the pane redraws with their new state. The `set:`/`menu:settings` palette entries are gone: a
setting is now a control in the pane, found by the same search.

**Agent-editable shortcuts.** Each worker receives the action catalog at configure time and
after every reload. The `set_keybinding` tool (`backend/relay_core/keybindings.py`) validates
the action id and key strings, then rewrites only that binding atomically. The watcher reloads it.

## 5. Composer and routing

`RichEditor` (`src/RichEditor.cpp`) is a `QPlainTextEdit`: native mouse and keyboard selection,
undo, multiline, basic shell coloring, draft-preserving history (Up on the first line, Down on
the last), an IME guard (Enter during preedit never submits) and a 128 KiB paste cap.
Pasting never submits.

| Key in the composer | Destination sent to the router |
|---|---|
| Enter | selected input mode (`auto`, `shell`, `agent`); a program reading a line gets it instead (section 9) |
| Ctrl+Enter | `agent` |
| Ctrl+Shift+Enter | `shell` (terminal mode) |
| Ctrl+Alt+Enter | agent, `when: "interrupt"` (section 11) |
| Shift+Enter | newline |
| Esc | skip the question on a card that is up (sessions protocol 27.4), else stop the agent turn, or interrupt the running program; Esc Esc in an empty box opens Rewind. It never takes control of the terminal (Ctrl+H or the "Take control" button do) |
| PageUp / PageDown | scroll the terminal scrollback one page |

Text changes trigger a debounced (150 ms) preview route; the route label shows the decision.

### Voice transcription

The microphone chip at the right of the strip toggles recording; holding the voice key (Right Alt by
default, as in Warp) is push-to-talk. `relay::voice::Capture` (`src/Voice.*`, library `relay-voice`,
tests `tests/voice_test.cpp`) runs whichever capture tool the desktop has — `pw-record`, `parecord`,
`arecord`, then `ffmpeg` — into a temporary 16 kHz mono WAV, so Relay links no audio library. The
worker transcribes it (`backend/relay_core/voice.py`, protocol section 16) with an OpenRouter key of
its own and the GUI deletes the clip; the transcript is inserted at the cursor and never submitted.

The hold key is matched on the event's native keysym (Qt reports both Alt keys as `Qt::Key_Alt`) and
the event is not consumed unless it is F9, because Right Alt is AltGr on most layouts; pressing any
other key while it is held cancels the recording. Options › Voice has the key, the model, the
recording cap, the microphone and the recorder in use.

At a password prompt the composer swaps `RichEditor` for a masked `QLineEdit` (section 9); nothing
typed there is routed, remembered or previewed.

**Prefixes.** `!` or `*` typed (not pasted) as the first character of an empty editor is consumed
and switches the input mode to terminal or agent for one submission (`setPrefixMode`, chip
`prefixChip`); Backspace on the empty editor restores the previous mode. `/shell ` and `/agent `
still work and trigger a shortcut hint.

**The `@` file picker.** `@` after a space opens a fuzzy file picker over the pane's directory.
Its index is `relay::FileIndex` (`src/FileIndex.*`, library `relay-fileindex`, tests
`tests/fileindex_test.cpp`): tracked plus untracked-but-not-ignored files from `git ls-files`
(capped at 20000), the changed set from `git status --porcelain`, and a bounded directory walk
outside a repository. **It is asynchronous** — each step runs from QProcess signals with a timeout
of its own, so `refresh()` returns at once and the popup fills in as answers arrive (it shows
"Indexing files…" until the first ones do); typing `@` used to block the window for as long as five
seconds in a large monorepo. An index is reused for 15 s, the previous results stay on screen while
a new one is built, and changing directory mid-flight supersedes the refresh rather than letting a
late answer overwrite it.

**Slash commands.** A submission is offered to `tryRunSlashCommand` (the built-ins, listed in
`Pane::slashCommands()`), then to `tryRunAliasSlash` (`/name`, issue G8DK), before the router sees
anything. A `/command` that is neither is answered by Relay rather than by Bash
(`reportUnknownSlashCommand`, `src/SlashCommands.*`): one `✗ Unknown command: /foo · did you mean
/fork? · type / for every command, /help for the keys` line, with the suggestions an optimal
string alignment away from a real name (a transposition counts as one slip) or a name the user was
part way through typing. The route label says the same thing before Enter does. What counts as an
attempt at a command is `relay::slash::attemptedName`: one name-shaped word after the `/`, on one
line, that is not a path — `/usr/bin/foo`, `/etc/hosts` and an existing single segment such as
`/tmp` are paths and still run in the shell. `/shell ` and `/agent ` are names in the same list, so
they keep falling through to the router. `/help` shows the same card `?` shows in an empty prompt
box (issue #Q4SD, owner report: an unknown command answered with `bash: /nosuchthing: command not
found`).

**Routing assist (protocol 11).** When a preview `route` has `needs_assist` and the mode is auto,
the label shows the local guess, then "AUTO · checking…" after 150 ms; 300 ms after typing stops
the pane sends `route_assist {id: "assist-N", text, cwd, timeout_ms: 4000}` for the current text
only. `route_assisted` updates the label ("AGENT · guessed: reason (82%)") and is cached per
text. On submit, a cached answer replaces the route; otherwise the decision is held for at most
400 ms (`m_assistHold`) and then dispatched with the local guess. A failed or timed-out assist
leaves the local guess ("model check unavailable"). Prefixes, Ctrl+Enter and Ctrl+Shift+Enter
send a non-auto mode and never ask.
Submission sends `route` to the worker with the text, mode, live alias/function names, the
shell's `PATH` and cwd. The worker's `router.classify` (`backend/relay_core/router.py`) never
executes input:

1. Control characters (other than newline and tab) are rejected. Limit 128 KiB.
2. A leading `/shell ` or `/agent ` forces a destination.
3. Fixed modes run the validity check in both directions: terminal mode returns `shell` plus
   validity; agent mode returns `agent` plus `valid`/`invalid_reason` (a runnable command
   submitted in agent mode is valid). Either may set `agent_signal: true` — the text reads like
   a request for the agent, not a broken command (protocol 11.2).
4. Auto mode: text matching the natural-language pattern (`why`, `how`, `please`,
   `explain`, `find the`, …) goes to the agent, unless the first word is a live alias or
   function and the text is runnable.
5. Otherwise `check_runnable`: `bash --noprofile --norc -n` in a clean environment (2 s
   timeout), then every command word in pipelines, lists, subshells and command substitutions
   must resolve to a builtin, keyword, live alias/function, `PATH` executable, or executable
   path relative to the terminal cwd. Heredocs, `case`, arithmetic and arrays fall back to
   checking the first word only.
6. Runnable text returns `shell`. Anything else returns `agent` with `invalid_reason`
   (for example `command not found: foo`), which the GUI prints as the reason.

The router no longer produces `ambiguous`. `Pane::dispatch` still treats a legacy
`ambiguous` decision like `agent`.

GUI dispatch (`Pane::dispatch`):

| Decision and mode | Action |
|---|---|
| `shell`, a program owns the terminal, not terminal mode | send to the agent with a note |
| `shell`, terminal mode, invalid, `agent_signal` | wrong-mode hint (below): nothing runs, the text stays, Ctrl+I then Enter resubmits |
| `shell`, terminal mode, invalid | start the fix loop (section 7) without running |
| `shell`, terminal mode, valid | run in the terminal and watch the exit status |
| `shell`, auto, invalid | send to the agent with the reason |
| `shell`, auto, valid | run in the terminal |
| `agent` | `submitAgent` |

**Wrong-mode hints (protocol 11.2).** A submission that errors and clearly belongs in the other
input mode flashes the mode chip in the suggested mode's colour (`Pane::flashModeChip`, Theme's
`stripChip[flash]` rules) and shows a shortcut hint naming `input.toggle`. Terminal mode: an
invalid command with `agent_signal` skips the fix loop entirely — nothing runs, the composer
keeps the text, one `✗ … · this reads like a request for the agent, not a command` line prints,
and Ctrl+I then Enter resubmits it to the agent; a command that runs and fails with `agent_signal`
gets the hint alongside the normal fix attempt. Agent mode: a runnable command submitted as a
prompt is remembered for the turn, and when the agent's own `run_command` of that same text exits
non-zero (whitespace-collapsed match, a leading `cd <dir> && ` stripped), the hint fires once.
Both the flash and the toast go through `Pane::hint()`, so the per-hint limit, the cooldown and
the global "Shortcut hints" setting apply, and an unbound `input.toggle` gets nothing.

Routing is a convenience, not a security classifier. Natural language can be valid Bash.

## 6. Shell bridge: staging commands safely

`shell/integration.bash` is passed as `--rcfile`; user dotfiles are never edited. It sources
`~/.bashrc` (unless `--clean-shell`), raises its own `oom_score_adj` to 300, changes to
`RELAY_START_DIR`, defines `relay open`, and installs hooks:

- `PROMPT_COMMAND` becomes `(__relay_prompt_begin, <user entries>, __relay_prompt_end)`,
  keeping scalar or array forms and the original exit status. `prompt_end` emits `ready` with
  alias and function names.
- A DEBUG trap emits `running` for the first command after a prompt. If a DEBUG trap already
  exists, the script emits `unsupported` and stops; Relay falls back to native input.
- `bind -x` in emacs, vi-insert and vi-move keymaps: Ctrl+X Ctrl+R runs `__relay_load`,
  Ctrl+X Ctrl+P runs the no-op `__relay_redraw`.

The GUI polls `state.json` every 80 ms and accepts only events with its session token and a
new sequence value. If no event arrives within 5 s, the pane switches to native input. The poll
is a `stat()` unless the file has changed (`shell/event.py` replaces it, so a new event is a new
inode), and a pane that is off screen with nothing in flight — a background tab — polls every
400 ms instead (`Pane::tunePoll`); it returns to 80 ms when shown, when it is given a command and
while its queue has items. Measured idle, eight tabs: 1.80 % of a core before, 0.55 % after.

The same tick also asks whether the shell is ready (`Pane::refreshShellReady` → `readlineReady`),
and that used to cost a `/proc` walk per pane: `open`/`ioctl(TCGETS)`/`close` on
`/proc/<shell>/fd/0` plus an open, two `statx` and a read of `/proc/<shell>/stat`. The engine
already holds the PTY **master**, and on Linux both ends of a pty share one line discipline, so
`tcgetattr()` on the master reports the slave's `ICANON`/`ECHO`
(`TerminalBackend::termiosFlags()`, the `LineDiscipline` capability, `Pty::termiosFlags()`), and
`TIOCGPGRP` on the master — which is not subject to the controlling-terminal rule that makes
`tcgetpgrp()` on the slave fail with `ENOTTY` — gives the foreground group
(`foregroundProcessId()`). Both `/proc` routes are kept as the fallback for an engine that
answers `valid = false`. Measured with `strace -tt` over a 10 s idle window, eight visible
panes: 13,091 system calls before, 6,300 after. `openat` 2240 → 160 (every
`/proc/<pid>/fd/0` and `/proc/<pid>/stat` open is gone; what is left is cgroup accounting),
`close` 2240 → 160, `statx` 2400 → 400, `read` 2420 → 420; `ioctl` 2160 → 3160, the whole poll
now being 1080 `TCGETS` and 2080 `TIOCGPGRP` on descriptors the engine already holds. Idle CPU,
eight visible split panes: 1.60 % and 1.65 % of a core before, 1.25 % and 1.30 % after; eight
tabs, 0.55 % → 0.40 %.

Sending a command (`Pane::runInTerminal`):

1. Readiness: a `ready` event was seen, nothing is loading, the terminal is in noncanonical mode
   (Readline active; `PROMPT_COMMAND` runs before that), and the shell owns the foreground
   group. Both answers come from the PTY master the engine already holds —
   `TerminalBackend::termiosFlags()` and `foregroundProcessId()`, one `ioctl` each. An engine
   that cannot answer falls back to `/proc/<shell>/fd/0` for the mode and `/proc/<shell>/stat`
   field `tpgid` for the group (on the slave, `tcgetpgrp()` fails with `ENOTTY` because the PTY
   is not Relay's controlling terminal; on the master it does not).
2. The UTF-8 text is written atomically to `input.txt` (0600).
3. Only Ctrl+X Ctrl+R is sent to the PTY. `__relay_load` reads the file into `READLINE_LINE`
   and emits `loaded` with the file's SHA-256.
4. Enter is sent only if the hash matches. No acknowledgement within 2.5 s means no Enter:
   the pane switches to native input and says so.

The event file protects against accidental cross-session events and output spoofing, not
against hostile processes running as the same user.

## 7. Terminal-mode fix loop

Applies only to terminal mode (Ctrl+Shift+Enter, `/shell `, or the Terminal picker).

- An invalid command is not run. The agent is asked to fix it (attempt 1) — unless it carries
  `agent_signal` (it reads like a request, not a command): then the wrong-mode hint fires instead,
  nothing runs and the composer keeps the text (section 5, "Wrong-mode hints"). Owner decision,
  2026-09-18: this replaces the earlier rule that Ctrl+Shift+Enter sends every invalid line to the
  fix loop — a request is not a broken command, and "fixing" it into one is worse than asking.
- A valid command runs. At the next `ready` event: exit 0 ends the loop, exit 130 (Ctrl+C)
  ends it silently, any other status starts a fix turn. A failing run that carried `agent_signal`
  also shows the wrong-mode hint.
- The fix prompt carries the command, terminal cwd and problem, and tells the agent it cannot
  see terminal output. The reply must end with a fenced `relay-run` block.
- Fix prompts are submitted through the agent queue, so a busy agent queues them.
- When the fix turn ends with `done`, Relay takes the last `relay-run` block and stages it
  through section 6, 150 ms later. At most 3 attempts (`kMaxFixAttempts`).
- Auto-mode commands are never auto-fixed.

### 7.1 The agent hands a command to the terminal (`run_in_terminal`)

The fix loop is Relay asking the agent for a command. This is the other direction: the agent, in
an ordinary turn, needs a command run that its own `run_command` cannot run, because that is a
separate Bash with no terminal, no stdin and no ssh agent (`ssh -t`, `sudo`, logins). Protocol
section 22; worker side `backend/relay_core/terminal_handoff.py`; card #D8J3.

- The tool exists only when the pane offers it: `startAgentEntry` sends
  `context.terminal_handoff` from the `agent/terminal_handoff` setting (`agent`, `prefill`, or
  `off`, which sends nothing). Never on a fix turn, and never on a prompt from a paired device.
- The agent picks `run` or `prefill` per call. What happens is `relay::input::handoffAction`
  (`src/InputPolicy`), from the state at that instant: the setting caps it, a run that cannot
  happen (a program owns the terminal, native input, a queued command) becomes a prefill, a prompt
  box with the user's text in it is never overwritten, and three runs in a row with nothing typed
  by the user stop the chain.
- `run` goes through `runInTerminal` (section 6) like any command; the pane answers
  `terminal_command_result {action: "started"}` from the `loaded` branch, once the hash matched
  and Enter was sent. `prefill` puts the command in the prompt box under the one-shot `! terminal`
  chip; wiping the box drops the chip and the hand-over with it.
- A handed-over command is not watched by the fix loop. Its output is captured even when the
  conversation index is off (`beginCommandCapture(..., forAgent)`; the index still gets only what
  its settings allow). At the next `ready` event `finishHandoff` puts a Relay-written prompt at
  the front of the agent queue: the command, its exit status and the last 4000 characters of
  output, fenced and labelled as data (`relay::input::handoffReport`). Exit 130 sends nothing.
  This is the one place the agent is shown terminal output it did not produce itself.
- When a run leaves a program in the foreground (ssh, a REPL), section 9.2 applies unchanged: the
  banner offers "Let the agent drive", and consent stays the user's gesture.
- The capture keeps the first 64 KiB a command prints (`kCommandCaptureCap`), so the "last 4000
  characters" of a very long output are the end of that, not of everything.

## 8. Inline agent output

There is no agent pane. Output goes through the pane's backend
(`TerminalBackend::writeToDisplay`, capability `DisplayInjection`; section 16). Bytes reach
the emulator like program output and never reach the shell, its history or its input: the
engine writes them into its parser.

`Pane::printInline`:

- Text is sanitized: C0 and C1 controls other than newline and tab are dropped.
- On the first block, the idle prompt line is erased (`\r\x1b[2K`). Each kind of text has its
  own 24-bit color (`Ink`: user prompt, agent text, tool line, tool output, diff add/remove,
  error, note).
- `closeInline` sends Ctrl+X Ctrl+P, so Readline redraws the prompt. While more queued turns
  are pending, the prompt is not redrawn between turns.
- Tool calls print one line each; see "Tool-call lines" below.
- **One blank line between blocks of different kinds** (#5AWD): the user's ✦ line, the
  `▸ model` header, the agent's prose and the ▸ tool rows are the kinds (`relay::gaps::Block`,
  `src/TranscriptGaps.h`, tested headless). `Pane::beginBlock` prints the gap before a block
  whose kind differs from the last one printed — never before the first, never between two of
  the same kind (a run of tool rows stays single-spaced), never right after the header, which
  sits on top of what follows it — and a ✦ line is set off from the previous turn even across
  the closed block and the shell prompt in between. Notes, errors, diffs and tool output carry
  no kind and stay attached. For a tool row the gap goes *before* `LineCursor::start()` /
  `result()` are asked, because it prints through `printInline`, whose `endCallRun()` tells the
  cursor something else printed and would stop the result rewriting its running row in place.
- If the D-Bus session is not found, output goes to stderr.

### Tool-call lines

Every agent tool call is **one row** — `▸ ran pytest · 212 lines · exit 1 · 8 s` — and a click
unfolds its detail underneath it, inside the terminal (card #TK9C; the wording comes from the
backend's `label`, protocol section 23; the fold layer is docs/ENGINE.md, "Folds").

- `tool_started` draws the row as `running pytest…` with **no trailing newline**, wrapped in an
  OSC 8 anchor over `relay://call/<pane>/<turn>/<call>`. The anchor starts at column 0 with a
  `▸ ` placeholder the view overpaints with ▸ or ▾ — on the ghostty core an anchor that starts
  further right is never found. While the command streams, the row is rewritten with a live
  counter (about ten times a second).
- `tool_result` rewrites the row in place with the finished line: the title in the muted tool ink
  (the error ink, with a `✗`, when it failed) and the stats muted after it, cut to the pane's
  columns with `…` so the row never wraps. **If anything else printed in between** (prose, a
  note, a steer, the streamed output when *Show tool output* is on), the row is finished where it
  stands and the result prints its own.
- **Consecutive reads and listings merge**: the row keeps the cursor until a call arrives that
  cannot join it, and becomes `read 6 files · 4,100 lines`. Its anchor is `<first call>+<n>` and
  its fold lists the members, each linking to the file it read.
- A **diff of at most 12 changed lines** prints under the row with no click at all; a larger one
  opens the diff pane (`ToolPane::Kind::Diff` over `relay::DiffView`, a splitter pane, one per
  tab). What else a click opens is the label's `open.type` (section 23.6): a file in a preview
  pane, a subagent tab, a card. Those lines anchor **`relay://open-call/…`** instead, which the
  fold layer leaves alone and `Pane::openOutputTarget` (and `WindowManager::handleOpen`, for a
  link from outside) routes.
- The fold's content comes from `tool_output_get` under a `fold-` request id, so it never also
  opens a pane, and is built from the reply's `detail` sections: a command with a `$` lead, output
  with its escapes stripped, a diff in the add/remove inks on a tint, capped at 5,000 rows with a
  final "open in pane" row. A turn the worker no longer keeps gets a one-row fold that says so.
- **While a program owns the terminal** nothing can be rewritten, so only the finished line is
  printed, as plain text with no anchor.
- Everything the pane decides first — the URI, the row and its cut, the rewrite-or-new-row state
  machine, the fold's rows — is in `src/CallLines.{h,cpp}` and tested headless
  (`tests/calllines_test.cpp`); `src/Pane.h` only writes the bytes.
- Hints: `call.fold` ("Click a ▸ line to unfold it here · Ctrl+Shift+Return unfolds the nearest")
  after a turn that printed tool lines, and `diff.hunks` ("n and p step through the hunks") when a
  diff pane opens.

**While a program owns the terminal** (vim, a build, a REPL), printing would corrupt its
screen. Output is buffered, and also shown live in the **transcript panel** above the composer
(`Pane::appendTranscript`): header "Agent · model — output will also print in the terminal
when <program> exits", at most about 40% of the pane height, × hides it until the program
exits. On the next `ready` event the buffer prints into the terminal and the panel resets.

**Thinking and turn summaries (protocol 11).** `thinking_delta` text streams into a **fold** in
the terminal's own grid, under an anchor row of its own — `▸ ✦ thinking…`, column 0, hyperlinked
to `relay://call/<pane token>/<turn id>/thinking` (`thinking-2`, … for a model that resumes
reasoning after its answer) — opened with the first delta, updated coalesced (~4 Hz, the last
12 000 characters rendered as markdown, with an "open in pane" row to `relay://turn/…`; a click on
a settled anchor answers from the pane's buffer, no worker round trip). Its height is capped in the
rows the view paints, not in lines (#K48R, owner's numbers from Warp): the **last 6** rows while it
streams, under `… N earlier lines`; the **first 18** of a settled fold opened by hand, over
`… N more lines · open in pane`. The rows are wrapped to the pane's width first
(`relay::wrapFoldLines`), so a single long paragraph cannot escape the cap. A reader who folds it
away mid-stream is not asked again: the flush notices
`foldExpanded()` disagreeing and stops pushing — `setFoldContent` opens what it sets, which would
reopen over their click. `thinking_done` settles the fold, collapses it unless the reader toggled
it or the setting is `always`, and rewrites the anchor row in place to `▸ ✦ thought for N s` (a
stream that stopped mid-reasoning, `chars: 0`, gets `✦ thinking stopped` and still a fold of what
arrived). How much of this runs at all is `agent/thinking_display` — `collapse` (default),
`always`, `never`, which leaves the legacy single `✦ thought for N s` Note line and no fold; a
settings file that still has the `agent/show_thinking` bool is migrated in place on first read.
Alt+R (`agent.thinkingPanel`) toggles the latest fold, live while it streams and the last turn's
afterwards, and every refusal toasts. The text is kept per turn whatever the display mode —
`m_turnThinking` feeds the turn pane and the Activity pane too: the turn view holds the reasoning
and redraws it with the log, so the `turn_transcript` reply that lands after the pane opens no
longer wipes it (#K48R), and the fold's 4 Hz flush keeps an open one current while the block
streams. The capped fold's "open in pane" row goes to the Activity pane (below), on that turn's
block. `turn_summary` (sent just before `done`) is stored
per pane (last 50) and, when the turn used tools, prints `✦ N tool calls · T s` wrapped in an
OSC 8 hyperlink to `relay://turn/<pane token>/<turn id>`. The live `tool_output {text}` stream and
the stored reply `tool_output {stored: true, …}` share a name; the GUI branches on `stored`.

**The Activity pane** (card #QT8C; named "Activity" by #4X53, which left every identifier — the
action `agent.internalsPane`, the pane type `internals`, the classes, the layout node — spelled
"internals"; Alt+Shift+R, the palette's "Activity", or the reasoning fold's "open in pane"). One `ToolPane(Kind::Internals)` per
terminal pane, inserted beside it like the turn and diff panes, hosting `relay::AgentInternalsView`
(`src/AgentInternalsView.*`): a scrolling log of the pane's reasoning and tool calls, live and in
order — a muted rule per turn carrying the request's first line, each reasoning block as
`✦ thinking… / ✦ thought for N s` over the whole text as Markdown (no cap: this pane is where the
block lives), each tool call as its § 23 row, running → settled, a run of reads merged into one
row, which a click folds open in the log through the same `tool_output_get` round trip the
terminal's folds use (`int-` request ids); a big diff still opens the diff pane. It is pinned to
the bottom while output arrives; scrolling up unpins, End re-pins. **While it is open the terminal
prints neither**: `Pane` routes `thinking_delta`/`thinking_done` and `tool_started`/`tool_output`/
`tool_result` to the view (`attachInternals`), and instead of drawing a row it writes what the row
would have been — anchor, title, stats — to a `relay::internals::Ledger`
(`src/InternalsLedger.*`, pure QtCore, `tests/internalsledger_test.cpp`), keeping `rememberCall`,
`m_turnThinking` and `m_thinkingBlocks` exactly as a live row would. **Closing the pane reprints
them** (owner, 2026-09-19: "i did mean that the hidden rows should be reprinted on close"):
`detachInternals` → `reprintHiddenRows()` writes every hidden turn at the cursor — a muted rule
with its request, then its rows, settled and collapsed with their live anchors, so a click unfolds
a reprinted row exactly like a live one. They land below whatever printed while the pane was open,
hence the rules; a turn still running gets its rows so far and the live rows continue under them;
the reprint waits for `inlineReady()` (`flushInline` drains it once a program gives the screen
back); the ledger hands its rows over once, so open-and-close-again prints nothing twice; and it
keeps at most the last 50 turns (the worker's own detail bound), older ones collapsing to one
`… N earlier turns` row. Opening the pane mid-block settles the inline fold's row to
`✦ thinking moved to the Activity pane` and the view continues the block; closing it mid-block
starts a fresh inline anchor for what follows. Closing the owner takes the pane along with nothing
reprinted (the `destroyed` connection's context is the owner). The pane is saved in the layout as
`{"internals": {cwd, owner}}` and restored beside the pane whose scrollback id it names, empty
until the next event.

**`relay://` links.** A click inside a pane is handled in-process (`Pane::openOutputTarget`).
A `relay://` link opened anywhere else — a browser, an editor, a file manager — reaches the
desktop's scheme handler, so `registerUrlHandler()` (1.5 s after start, idempotent,
`RELAY_NO_URL_HANDLER=1` skips it) writes
`$XDG_DATA_HOME/applications/org.relayterminal.Relay.url-handler.desktop` (`Exec=python3
relay-open %u`, template in `data/`), runs `xdg-mime default … x-scheme-handler/relay`,
`update-desktop-database` and `kbuildsycoca5`, and says so once in the status bar. `relay-open`
forwards `relay://` URLs as `{url}` over the open socket; a helper launched by the desktop has no
`RELAY_OPEN_SOCKET`, so `WindowManager` also writes the address to
`$XDG_RUNTIME_DIR/relay/open-socket` (0600). `handleOpen` finds the pane by token and
`openTurnPane()` inserts a `ToolPane(TurnTranscriptView)` (`src/TurnTranscript.*`): tool rows
from the summary, the transcript from `turn_transcript_get`; Enter on a row sends
`tool_output_get` and the result is written to a 0600 temp file (`.diff` or `.log`) and opened in
a preview pane.

**Rewind.** `/rewind` and Esc Esc open *Rewind chat* (`rewind {restore: "conversation"}`, files
never touched; "Fork from here"). `/rewind-code` (palette "Rewind code…") opens *Rewind code*
with "Rewind code…" (`files`) and "Code and chat…" (`both`); both first list the files changed by
that turn and later ones in a confirmation where Enter restores. Conflicts are only known after
the restore (the backend skips files changed since and reports them in `rewound`).

## 9. Human and agent control

**The prompt box is the only keyboard input** (Warp-style). The terminal widget only holds the
keyboard in native mode, which the user enters on purpose. The rules live in the Pane, not in a
backend, not in the engine; the decisions themselves are
pure functions in `src/InputPolicy.{h,cpp}` (library `relay-input`, tests
`tests/inputpolicy_test.cpp`).

`relay::input::State` is what the Pane observes (`Pane::inputState`): the terminal's line
discipline (`TerminalMode::{Unknown,Raw,Echoing,Secret}` from termios on `/proc/<shell>/fd/0`),
whether a foreground process group other than the shell is running, whether a process of it is
blocked in `read()` on the tty, whether the alternate screen is active, and whether the pane is in
native mode.

| Situation | Behavior (`Pane::pollShell`, `takeControl`, `showPrompt`) |
|---|---|
| Clicking the terminal | Selects text, scrolls, follows links. It never takes the keyboard: the backend's focus widget is set to `Qt::NoFocus` while the composer is shown, and a `FocusIn` on the terminal surface hands the focus back to the prompt box (widgets meant to be typed in, such as the engine's Find bar, keep it). A plain click shows a hint naming Ctrl+H |
| Full-screen program (alternate screen) or `ssh`/`mosh`/`telnet` | The prompt box keeps the keyboard. A floating **"Take control (Ctrl+H)"** button appears over the terminal (`updateTakeControl`, `relay::input::offerTakeControl`). Policy `human` restores the old automatic hand-over |
| Program exits (`ready`) | Automatic human control ends; the composer returns; masked input and the button are cleared |
| Ctrl+H from the composer, or the button | Human control: composer hidden, keys go to the terminal. Started from a full-screen or remote program, the prompt box comes back when the program exits |
| Ctrl+Shift+H | Composer back. While a program runs, submissions go to the agent |
| Ctrl+Shift+J (`program.delegate`), the banner button, the palette | Hands the running program to the agent (section 9.2). With text in the prompt box it sends that request too. Pressed again, or Ctrl+H, takes it back |
| F12 (`terminal.native`) | Toggles native input, unchanged, for people who want the old behaviour |
| A guest holds the pane's keyboard (`#W5N2`, protocol 10.3) | One driver per pane, and it is the same token: "the agent is driving" and "alice is driving" are one state. The owner's physical keystroke always takes it back, without asking — `Pane::takeBackFromGuest()` sends `control_take` and is called from exactly the two places `endDelegation()` is, `setNative()` and the key filter, which never swallows the key that did it |

Policy lives in QSettings: `control/default` (`agent`, the default — the prompt box keeps the
keyboard — or `human`, the old automatic hand-over) and `control/programs` (basename → `human` or
`agent`), set from the palette.

**Where a submitted line goes** (`relay::input::targetFor`, applied in `Pane::sendLineToProgram`
before the router is asked). An agent submission (`*`, Ctrl+Enter, agent mode) always reaches the
agent. Otherwise, while a foreground program is running and the terminal is in canonical (line)
mode, the line is written to that program's stdin:

- `Secret` (`ICANON` on, `ECHO` off) — a password prompt: masked input, see below;
- `Echoing` **and** a process of the command blocked in `read()` on the tty — an ordinary
  question such as `apt`'s `[Y/n]`: the line is sent with a short "Sent to apt" status.

Anything else keeps the existing behaviour: run in the shell now, or queue until the terminal is
free. `sudo`, `doas`, `pkexec`, `su` and processes of another user do not expose
`/proc/<pid>/syscall`, so Relay cannot see them waiting; only their password prompts are detected,
and everything else queues (the composer row says so).

**Password prompts.** Once a second, and every 250 ms while a command runs, `checkPasswordPrompt`
opens `/proc/<shell>/fd/0` and checks termios: `ICANON` on and `ECHO` off (full-screen programs
and Readline turn `ICANON` off, so they do not match). A match switches the prompt box to masked
input: a separate `QLineEdit` with `QLineEdit::Password`, a `password for <program>` chip, and the
composer's own editor hidden. Enter writes the line plus a newline to the backend
(`relay::input::Secret::take`).

Security rules, each covered by `tests/inputpolicy_test.cpp` where testable:

- the password lives only in that field and in `relay::input::Secret`. It is never passed to
  `RichEditor::remember`, the queue, the request ledger, the session file, logs, route assist,
  suggestions or any model prompt — `relay::input::retainable` is false for it, and the masked
  field is not the composer's document, so nothing it holds reaches `route`/`route_assist`;
- after the write, `Secret::wipe` overwrites the characters in place and the field is overwritten
  and cleared (`QLineEdit::setText` also drops its undo history);
- masked input ends when echo returns for two polls, when the command exits, on Esc, or when the
  user takes control; the chip disappears with it;
- Relay prints nothing about it in the terminal (no `printInline`); the status line only names the
  program.

**Ctrl+I at a password prompt** (`input.toggle`) leaves masked input and switches the pane to agent
mode, so the user can ask the agent to paste the password. That path is the normal agent path: not
masked, and subject to the usual control rules. If the window is inactive, a password prompt
flashes the taskbar and runs `notify-send`; commands that finish after more than 30 s notify the
same way.

**Program context for the agent.** A prompt submitted while a program runs carries
`"context": {"foreground_program", "terminal_cwd"}`. The worker validates it
(`agent.validate_context`) and prepends a labelled note saying the agent cannot see or type
into that program and that `run_command` uses a separate shell.

### 9.1 Reading the screen: what is the program asking?

`src/ScreenPrompt.{h,cpp}` (library `relay-screen`, tests `tests/screenprompt_test.cpp` over
recorded screens in `tests/fixtures/screen/`) answers "is the foreground program waiting for me
to type something?" from the last rows of the screen. It is a pure function: rows in, a
`Detection {kind, question, options, defaultAnswer, confidence, masked}` out, with the `/proc`
signals of `relay::input::State` as one input rather than the whole answer — `sudo` runs its
child in its own pseudo-terminal, so for the very case this exists for nothing Relay may inspect
is blocked in `read()`.

The rules, in order, over the **last non-blank row** (with the row above it when the question
wrapped), after escape sequences and control bytes are stripped:

| Kind | Matched by |
|---|---|
| (none) | the alternate screen: a full-screen program has no "last line" |
| `password` | `password` / `passphrase` … `:` at end of line, or canonical input with echo off |
| `yes_no` | a bracketed option list at the end: `[Y/n]`, `(yes/no)`, `(yes/no/[fingerprint])`. The capitalized alternative is the default |
| `choice` | a line ending in “selection”, “choice”, “option” or “number” plus `:`, stronger with numbered rows above |
| `press_key` | "Press ENTER/RETURN/any key", `--More--`, `(END)` |
| `shell_prompt` | a trailing `$ # % ❯ ➜ »` with something host- or path-shaped in front: the command ended, and this is **not** a question |
| `free_text` | anything else ending in `: ? > #` — `read -p`, `input()`, `>>> `, `relay=# ` |

Confidence starts from the pattern (0.80 for a password or an option list, 0.70 press-key, 0.45
free text), gains 0.15 for canonical input with echo, 0.15 for a process blocked in `read()`, and
loses 0.30 when nothing is running. At 0.60 the pane acts on it. That is what keeps a `grep` hit
quoting `[Y/n]`, or a question that scrolled past during a download, from raising a hint.

Where it is used: the floating banner over the terminal ("apt is asking: Do you want to continue?
[Y/n]"), the composer row's line, and `relay::input::State::screenAsking` / `screenMasked`, which
let `lineRequested` send a submitted line to a program the `/proc` rules cannot see. The pane
polls it four times a second while a command runs and needs two agreeing ticks before the hint
appears. **Only an engine that reports `TerminalBackend::ScreenText` gets any of this**; a pane
without it keeps the `/proc`-only behaviour and the banner says "… is asking for input" with no
question.

### 9.2 Handing a program to the agent, and taking it back

The agent can type into the program in the **visible** pane, and only after the user hands it
over. `Ctrl+Shift+J` (`program.delegate`), the banner's "Let the agent drive" button and the
palette action turn it on; with text in the prompt box the same key also sends that text to the
agent. The pane then prints `✦ <program> handed to the agent · Ctrl+H takes it back`, the banner
becomes "Agent driving apt · 3 keystroke(s) · apt is asking: …" with a "Take over" button, and
every prompt submitted for that program carries `context.program_control` (protocol section 21),
including the screen.

Every write is a round trip: the worker's `type_into_program` emits `program_input`, the pane
checks `relay::input::agentTypeRefusal` *again* at that instant, performs it
(`TerminalBackend::sendText`, or a fixed name→bytes table for named keys) and answers with the
screen the keystroke produced. The pane prints `✦ typed: y   · <the agent's intent line>` for
every one, so nothing the agent types is invisible.

It ends — for good, not paused — when the user takes control (Ctrl+H, the button, F12), when a
password prompt appears, or when the program exits; each reason is sent to the worker so the
agent is told the true one. The agent never types into a masked prompt: the worker refuses before
the pane is even asked, and the pane refuses again. Panes whose engine cannot read the screen
cannot delegate at all and say so.

The agent's `run_command` is still a separate non-interactive Bash process; it has nothing to do
with the pane's terminal.

## 10. File panes and `relay open`

`src/FilePanes.{h,cpp}` builds the static library `relay-filepanes`: plain Qt widgets with no
KDE requirement.

- `relay::FileExplorer`: one folder through `QFileSystemModel`; folders first, hidden-file
  toggle, type-to-filter. Enter or double-click opens (folders navigate, files call
  `onOpenFile`); Backspace or Alt+Up goes up.
- `relay::FilePreview::open(path)` picks a viewer by MIME type: text and code in a read-only
  `QPlainTextEdit` (KSyntaxHighlighting "Breeze Dark" when built in), Markdown rendered or
  source, images (fit or 100%), PDF when Qt PDF is built in, otherwise a file-info panel with
  Open externally. Text is capped at 2 MiB with a notice; images over 64 MiB are refused.
  `goToLine` scrolls and highlights.
- The same `open()` takes `ssh://<host>/<path>` — a file on the host a terminal pane is logged
  into (card #S5SH, `docs/SSH-AND-MOSH.md` section 9). It is fetched over that pane's own ssh
  connection by `relay::remote::RemoteFile` (`src/RemoteFiles.{h,cpp}`, library
  `relay-remotefiles`), titled `host:/path` in the pane and in the tab, marked with a chip
  naming the host, and — unlike a local preview — **editable**: Ctrl+S or the Save button writes
  it back over ssh, a `●` marks unsaved edits, and a save whose file changed on the host offers
  Overwrite / Reload. A remote file always uses the text viewer (there is nowhere to type in a
  rendered document), is refused over 8 MiB or if it is binary, and nothing here can be handed
  to this machine's applications, so ↗ and "Open externally" are off.

Optional dependencies are detected at configure time (`RELAY_HAVE_SYNTAX_HIGHLIGHTING`,
`RELAY_HAVE_QTPDF`). The Qt6 `.deb` and AUR builds leave PDF off
(`packaging/deb/build-deb.sh`, `packaging/arch/*/PKGBUILD`).

`RelayWindow::openPath` reuses an existing explorer or preview in the tab. A new preview
opens beside an explorer if there is one, otherwise beside the anchor. Tool panes split,
close, restore and navigate like terminal panes.

Ways to open a path:

| Source | Path |
|---|---|
| Click the pane's directory line | `Pane::onOpenPath` → explorer |
| Palette: Open folder in explorer / Open file… | `files.explorer`, `files.open` |
| `relay open PATH` in a pane shell | shell function → `scripts/relay-open` → socket request `{path, line, token}`; the token selects the pane |
| Click or Ctrl+click a path in an engine pane's output | `relay::links` (`src/OutputLinks.*`) → `TerminalView::linkActivated` → `Pane::openOutputTarget` |
| `Ctrl+Shift+L` then Enter (engine panes) | the keyboard walk over the same links |
| Click a `#K7Q2` in an engine pane's output | the same path, with `relay://card/<id>` as the target → `RelayWindow::openBoardCard` |
| Click a path printed by a host a pane is logged into | `Pane::openRemoteOutputPath` → `ssh://host/path` → the same preview pane, fetched over ssh (#S5SH) |

### Clickable paths in terminal output

`src/OutputLinks.{h,cpp}` (library `relay-outputlinks`, namespace `relay::links`) holds the
rules, with no terminal and an injectable filesystem probe, so every format is unit-tested
(`tests/outputlinks_test.cpp`). `candidates()` finds the spans of one logical line: URLs of any
scheme (left as URLs), Python traceback frames (`File "x.py", line 12`), `file(line,column)`
(tsc/MSVC), quoted names with spaces, and bare tokens (`ls` names, `file:line[:column]` from
gcc/clang/grep/cargo, pytest node ids, stack frames inside brackets, backslash-escaped spaces).
`resolve()` expands `~`, resolves a relative path against the pane's directory (OSC 7 when the
shell integration is on, else `/proc/<pid>/cwd`) and asks the probe: **a path that does not
exist is not a link**. Which machine "exist" means is the host's to say: `TerminalBackend::
setLinkProbe(probe, directory)` (`TerminalView::setLinkProbe`) replaces both the probe and that
directory, and a pane logged into another machine answers from `relay::remote::PathProbe` — a
cache filled by one batched `test -e` per two dozen candidates over the same ssh connection, at
most one batch in flight, dropped when the login ends. A probe is called from a mouse-move, so
an answer that has not come back yet reads as "nothing there"; when the batch lands the backend
is told (`linkProbeAnswered()`) and the view re-reads the cell the pointer is on. `--flags`, bare numbers, version strings and `FOO=bar` are rejected
before the probe.

`candidates()` also finds `#K7Q2` card references (Switchboard design section 5): four
Crockford-base32 characters with at least one letter, opening a word, the whole word. Those are
resolved against a `CardLookup` the host hands in rather than against the filesystem —
`Pane::lookupOutputCard` answers from the pane's `relay::board::Model` — so, exactly as with a
missing path, **an id the pane's board does not know is not a link**, and a `#` comment in shell
output never becomes one. The target is `relay://card/<id>`, which `Pane::openOutputTarget`
already routes like the other `relay://` links.

The Relay engine uses it in `engine/view/TerminalView.cpp` for the hover underline and tooltip,
plain click (only in the active pane, so the click that moves the focus cannot open a file),
Ctrl+click, the right-click menu ("Open …", "Open in the system editor", "Copy path"; on a card
"Open #K7Q2 …", "Copy #K7Q2", "#K7Q2 → prompt") and the ordered link list behind
`Ctrl+Shift+L`. `Pane::openOutputTarget` routes the result: a folder to an explorer pane, a file
to a preview pane at `line`, a URL to `QDesktopServices`, a card to this tab's Switchboard
(opening the pane first when the tab has none), selected and scrolled into view.

`relay-open` falls back to `xdg-open` when Relay is not reachable.

## 10a. The Switchboard pane

The Switchboard **is** a folder in the project shown as a board: one card per issue, plan or
memory, a thread per card, and an agent that writes to it. The folder is `.switchboard/` (hidden,
so the cards do not clutter the project's listing and a ripgrep-based agent does not match every
card on every code search) on a board made from 2026-09-19 on, `switchboard/` on one made between
2026-09-18 and then, and `issues/` on one filed before either — `board.BOARD_FOLDERS` /
`relay::projects::boardFolders()`, read in that order, and the only one a project ever keeps is the
one it already has: nothing moves by itself, except the explicit "Hide this board's folder" /
"Show this board's folder" action (protocol 19.17). Its `board.yaml` is the marker, and that marker
is the switch — everything here is inert without it. Design:
`docs/SWITCHBOARD-DESIGN.md`; bytes: `docs/SWITCHBOARD-FORMAT.md`; protocol:
`docs/AGENT-SESSIONS-PROTOCOL.md` section 19.

**Which board, and whose** (card #JN7X, `src/Projects.h`, `src/BoardWorkspace.h`). A tab is
attached to at most one project and **starts attached to none**, which is the quiet, normal state:
no board tools, no board policy in the prompt, no tip and no offer. A pane's *candidate* project is
derived fresh from its live terminal directory (`projects::candidateFor`) and is an offer, not an
attachment; the pane's own `workspace()` is never consulted, because it is frozen at creation and
inherited from the directory Relay was launched in — the reason one project's board used to appear
in every pane of every window. Only an explicit project action attaches: opening the Switchboard,
`/card`, picking a card with `#`, Execute-from-card. `RelayWindow::attachTab()` is the one funnel —
the only writer of the tab → project map, the only caller of `projects::Registry::remember()` and
the only place the tab's panes are re-pointed — and `set_board` (protocol 19.11) re-points a pane's
worker **without ending its conversation**. "Detach this tab from <project>" is in the palette while
a tab is attached; it closes nothing.

- **`src/BoardModel.{h,cpp}`** (`relay-board`): the pure logic — the rows the worker sends, the tab
  and column a card falls into, the filter language (`label:`, `status:`, `@assignee`,
  `waiting:me`, `#ID`, free text) and the fuzzy ranking the `#` picker uses. No widgets, so
  `tests/boardmodel_test.cpp` drives it directly.
- **`src/BoardPane.{h,cpp}`**: `relay::BoardView`, a `ToolPane` leaf (`ToolPane::Kind::Board`).
  A tab bar with counts, a filter field, horizontally scrolling columns of cards with drag and
  drop between them, quick add, and a card detail view on the right: the rendered body, the
  `## Tasks` checklist, the links, the thread and a reply box (`RichEditor`) that either asks the
  Switchboard agent or appends a plain comment. A `QFileSystemWatcher` on the board folder (the
  one the `board` event named, else `projects::boardDirOf()`) turns any write — this window, a pane
  agent, an editor, a `git pull` — into one debounced `board_refresh`.
- **`src/BoardWorker.{h,cpp}`**: one `backend/worker.py` per **window**, configured with
  `agent_role: "switchboard"`, so card threads never enter a pane's conversation. It is started
  lazily on the first open and answers every `board_*` message of protocol 17.

Opening: **Ctrl+Shift+S** (`board.open`) splits it in beside the anchor pane, focuses the one the
tab already has, or, pressed on it, returns to the last terminal pane. Also the palette
("Switchboard") and `/switchboard`. `/card <text>` adds a card to the Inbox verbatim without
opening anything. Both attach the tab to the pane's candidate project; a candidate with no board
yet gets one quiet status line and **nothing is created** (the init question is protocol 19.12).
The layout node is `{"board": {"workspace", "tab"}}`, and a tab attached to a project is saved as
`{"project": "…", "node": <tab node>}` — an unattached one keeps the bare node shape, so the layout
file needs no schema bump (`relay::windowstate::tabNode`/`tabProject`).

Inside the pane: arrows select, Enter opens a card, Esc closes it, `n` adds one, `m` moves it,
`/` filters, `c` replies, `y` copies `#ID`, `t` sends `#ID` to the composer, `o` opens the card
file, Ctrl+PgUp/PgDn switch tabs, and Alt+Shift+arrows move a card between columns or within one.

From the terminal: `#` after a space opens a card picker in agent or auto mode (in terminal mode
`#` stays a Bash comment), a resolved `#K7Q2` travels with the prompt as `ask {cards: […]}`, and
every agent card write prints one line in the pane that caused it.

Plans and memories are card types, not work cards: the Plans tab has its own statuses
(draft → approved → executing → done) and the Memory tab is one list per topic.

## 11. Agent backend

### Worker protocol

`backend/worker.py`: one JSON object per line, 2 MiB maximum per message. The GUI kills a
worker whose unread output exceeds 8 MiB and ignores its stderr. The worker raises its own
`oom_score_adj` to 500.

| Request `type` | Purpose |
|---|---|
| `route` | classify composer text (section 5) |
| `configure` | provider, key or `use_stored_key`, workspace, extras, `max_tokens`, `keybindings` catalog, optional `skills`. Refused while a turn runs. |
| `keybindings` | replace the action catalog, keeping the conversation |
| `presets` | list presets with `has_stored_key` and Warp's default preset |
| `store_key`, `import_warp` | keyring operations (section 12) |
| `ask` | `text`, `when` (`now`/`queue`/`interrupt`), optional `context` |
| `cancel`, `resume_queue`, `queue_remove`, `queue_clear` | queue control |
| `reset` | new conversation; refused while a turn runs |
| `shutdown` | exit |

Events: `ready`, `route`, `configured`, `presets`, `key_stored`, `warp_imported`,
`keybindings_updated`, `queued`, `queue_changed`, `interrupting`, `agent_started`,
`agent_finished`, `status`, `delta`, `usage`, `tool_started`, `tool_output`, `tool_result`,
`done`, `cancelled`, `error`, `reset`, `session_title` (section 18). Errors carry `agent_busy`.

### Conversation index and search

`backend/relay_core/conv_index.py`, protocol section 14. An SQLite FTS5 database at
`$XDG_DATA_HOME/relay/index.db` (0600, in the 0700 directory that holds the sessions) with one
`conversations` row per saved conversation and one `entries` row per user prompt, assistant reply,
tool call, capped tool output, Relay-run terminal command and captured command output. `entries` is
mirrored into an external-content FTS5 table by triggers.

The index is a **cache**, never the source of truth: `SessionStore.save` refreshes a session's rows
on every autosave — including the throttled mid-turn ones, so a conversation being had is already
searchable — `reconcile()` (run once per worker, on its first conversation command) picks up
files the index missed and drops rows whose file is gone, and `index_rebuild` recreates everything
from the session JSON. A corrupt database is deleted and recreated, in the constructor and again if
SQLite reports corruption mid-query; a v1 database is migrated in place to v2 (subagent threads),
since terminal history cannot be rebuilt. User titles and pins live in `<id>.meta.json`, not only in
the index. The guest sources are a cache of files Relay does not own, so they have a reconcile of
their own: `guest_sessions.reconcile()` runs on a worker thread behind any `conversations` answer
that named `claude` or `codex`, never in front of it, and at most once every few seconds.

Subagent threads (protocol section 25) are rows too (`source: "subagent"`), each with its
`owner_session` (the session that started it) and `parent_thread`, read from
`<session>.threads/<thread-id>.json`, which `SubagentManager` writes when a subagent starts and
whenever one of its runs ends. Searches include them only when asked (`include_threads`).

Only sessions under
`$XDG_DATA_HOME/relay/sessions` are indexed, so a pane with a custom `session_dir` (and every test)
stays out of it; `RELAY_INDEX=off` disables it.

Terminal history is a synthetic conversation per workspace (`term-<16 hex>`, `source: "terminal"`).
The pane sends `terminal_history` when the shell reports the prompt again after a command Relay
staged: the command line, its exit status, the directory, and the output captured through
`TerminalBackend::onOutput` between "command loaded" and "shell ready" (enabled only for that
window, cut at the next `OSC 133;A` so the redrawn prompt is not part of the output, control
sequences stripped by `relay::conversations::stripAnsi`). Commands typed straight into the terminal
in native mode never pass through Relay and are not indexed.

The GUI side is `src/Conversations.{h,cpp}`: the **session manager pane**
(`relay::conversations::SessionManager`, a `ToolPane` of kind `Sessions`, `paneType` `sessions`;
cards #CCKY, #R6J0), which replaced both the conversation dialog and the resume picker, and the
Ctrl+F find bar, which searches the terminal through `TerminalBackend::find()` and counts matches
in the pane's conversation with `conversation_get {query}`. `/resume`, `/conversations`,
Ctrl+Shift+Y and the palette rows all reach `RelayWindow::openSessionsFor(pane, query)`: one
manager per tab, bound to the pane that asked (its queries go to that pane's worker). Enter resumes
in that pane (`resume`, or `load_state` with a session reference when the session belongs to
another workspace); Shift+Enter opens it in a new pane through the same path as a fork; a session
already open in some pane is focused there instead (`paneWithSession`). The "Subagent threads" box
(off by default) lists threads under their owner sessions; Enter on one opens its history in the
ⓘ pane. Other features add tabs beside the list with `RelayWindow::addSessionsTab(id, label,
factory)` and open one with `RelayWindow::openSessions(tab)`.

What a row shows and what the box understands (card #SM4R, 2026-09-18): the title on one line, the
tags and the agent-written summary (protocol §18.4; the first prompt until there is one) on the
next; `→` unfolds a quick look built from `conversation_get`'s `overview`, fetched once per session.
The box's operators (`file:`, `project:`, `-word`, … — protocol §14) are parsed by the worker, which
returns them as `parsed` so the pane draws a chip per operator, and `facets` fill the model and
branch menus. "Continue" heads an unfiltered list with the project's pinned, unfinished and recently
closed sessions (the window feeds the manager what is open and what was closed through
`setOpenSessions` / `setClosedSessions`; the manager never looks at a window). Summarise, on a row
or for every session in a scope, always goes through the pane's worker and never starts on its own.

The **ⓘ pane** is `src/SessionInfo.{h,cpp}` (`relay::sessioninfo::InfoView`, `ToolPane` kind
`Info`, `paneType` `info`): opened by **Alt+I** (`agent.info`), the painted ⓘ button in an agent
pane's header row, `/status` or `/info`, beside that pane, one per pane. It renders the worker's
`session_info` (protocol section 25): model and provider, context, provider-reported tokens and
cost, the session file, times, turns, instructions, and the history — the turns in order with each
subagent thread as a link at the turn that started it. A thread link shows that thread's own
history in the same view with "↑ owner session" (and "↑ parent thread"); a thread the worker still
holds also links to `RelayWindow::openSubagentTab`. `ToolPane` hosts both views through
`relay::PaneView` (`src/PaneView.h`): a title, focus and the header inset.

### Model roles and the Main / Flash / Lite / Local tiers

`backend/relay_core/roles.py` and the tier table in `presets.py`, protocol sections 13 and 13.7.
Thirteen roles — `main`, `terminal_use`, `subagent`, `switchboard`, `flash`, `local`, `planning`,
`summaries`, `suggestions`, `chores`, `audit`, `vision`, `route_assist` — but only **four** knobs,
because every tiered role follows a tier:

| Tier | Roles | Default |
|---|---|---|
| Main | `main`, `subagent`, `switchboard` | the pane's own model |
| Flash | `terminal_use`, `flash`, `summaries`, `suggestions` | `TIER_DEFAULTS[<main preset>]["flash"]` |
| Lite | `chores`, `audit` | `TIER_DEFAULTS[<main preset>]["lite"]` |
| Local | `local` | the first endpoint in the local registry; no provider preset, so
  `presets.PROVIDER_TIERS` stays three wide (`main`, `flash`, `lite`) |

`vision`, `route_assist` and `planning` are outside the tiers (`roles.py`'s tier map gives all three
`None`): vision uses the provider's image model, plan mode runs on the pane's own model pushed to max
reasoning (protocol 13.11), and route
assist is pinned to `google/gemini-3.5-flash-lite` because routing has a sub-second budget (0.5–0.6 s
measured, against 2.3–4.9 s for Gemini 3.8 Flash), so the Lite row must not move it.

- `RoleResolver` turns a role into a `ProviderConfig`, resolving its key through the keystore (the main
  agent's in-memory key is reused when a role lands on the main preset).
- **Two kinds of "no key".** A tier whose provider has no key steps one tier towards Main
  (Lite → Flash → Main) and reports an inline `note`; that is expected, so it never reaches
  `model_roles.warnings`. A role the user pinned to a provider whose key is missing falls back to the
  main agent and does produce a `warning`. Neither is ever a hard failure.
- QSettings: `provider/preset` is the default provider, `tiers/<flash|lite>/{preset,model,effort}` are
  the tier overrides, and `roles/<role>/tier` (or `roles/<role>/{preset,model,effort}` for a pinned
  endpoint) is the per-job override. `Pane::rolesObject()` and `Pane::tiersObject()` turn them into the
  protocol objects; both go out together in `configure` and `set_agent_options`.
- Used by: subagents that inherit (`SubagentFactory.base()`), compaction and recaps (`summaries`),
  next-command/next-prompt suggestions (`suggestions`), the request audit (`audit`), routing assist
  (`route_assist`), and panes that run the Flash agent themselves (`configure {agent_role}` /
  `set_agent_role`).
- **Naming and what is offered** (2026-09-18, `#P7QK`). A *preset* is a plan you hold a key for, so the
  keys modal lists one row per plan and uses `label` ("Kimi · K3"). The roles modal picks a **provider**
  whose model the tier decides, so it uses `provider` — Kimi, Z.AI (GLM), OpenRouter, OpenAI (ChatGPT),
  Anthropic (Claude), Google (Gemini), MiniMax — and appends `· <plan>` only when two presets of one
  company are both offered. Its lists hold providers with a stored key, always including the one in
  use, and fall back to everything marked "(no key)" only when nothing has a key at all.
- **A tier that names only a provider** runs that provider's model *for that tier*
  (`presets.provider_tier_model`): Flash on Z.AI is `glm-5.3-flash`, not `glm-5.3`. When a provider's
  own entry for the tier points elsewhere (every Lite is Gemini through OpenRouter) the named provider
  wins and the nearest tier that stays on it is used. So Main on Kimi with Flash on the GLM Coding Plan
  is two picks and no typing, and changing the default provider keeps an override that names a
  different provider.
- GUI: `src/ModelSettings.*` (the `relay-modelsettings` library, so the dialogs are testable
  headlessly — `tests/modelsettings_test.cpp`) — `RolesDialog` (default provider, the three tier rows,
  an Advanced disclosure with one row per job showing the model it resolves to). Reached from Options › Models,
  the palette (`agent.modelRoles`) and the ⚙ entry at the bottom of the pane's model box. Plus
  "New panes use the Flash agent" (off by default; the first pane keeps the Main agent), the pane's model
  chip (role and effective model, all roles in its tooltip), "Flash agent for this pane"
  (`agent.flashAgent`, Alt+F) and the `/main` and `/flash` slash commands.
- The pane's model box (`Pane::refreshPickers`) is one list of three groups: the stored presets, then
  a **Main agent** / **Flash agent** row per pane role (`role:<id>`, the live one ticked or selected;
  `Pane::chooseAgentRole`), then the ⚙ gear (`gear:modelOptions`). Only the first group is a model to
  switch to — `selectModel` refuses the other two ids, so the box's `activated` handler must act on
  them before calling it, and rebuild the chip afterwards because Qt has already moved the box to the
  clicked row.
- Difficulty-based routing between the Main and Flash agent is deliberately not implemented yet.

### Provider transport

`backend/relay_core/provider.py`, standard library only.

- `POST <base_url>/chat/completions` with `stream: true`; JSON (non-stream) responses are also accepted.
- HTTPS required, except plain HTTP to `localhost`, `127.0.0.1` or `::1`. No credentials,
  query or fragment in the URL. Redirects are refused.
- Plain HTTP to a loopback host is a **local model server** (`ProviderConfig.local`, card `#24XJ`):
  no key is looked up or sent, the first token gets its own deadline (300 s) apart from the idle
  stall, a 503 "Loading model" is waited out, `stream_options.include_usage` and
  `parallel_tool_calls: false` are sent, tool-call envelope quirks are repaired, `<think>` tags
  become reasoning, and a context overflow is named without quoting the body. Saved endpoints
  (`local:<id>`), the probe and the worker messages are `backend/relay_core/localmodels.py`; a
  hosted provider's request is unchanged. See [LOCAL-MODELS.md](LOCAL-MODELS.md).
- A request refused with HTTP 408, 409, 429, 500, 502, 503, 504 or 529 by a provider that is not a
  local model server is sent again, the way Claude Code's transport retries: after the delay a
  `Retry-After` header names (seconds, milliseconds or an HTTP date, capped at a minute; a
  non-finite value is ignored), else an exponential backoff from 0.5 s doubling to 8 s with
  jitter. Other statuses — 501, 505, every other 4xx — are final at once. Two caps bound the loop:
  at most six retries, and a wall-clock budget for the whole call (twice the first-token deadline;
  20 s for a side call, 30 s for the key test), so six minute-long `Retry-After` waits cannot hold
  a pane while the stall watchdog sees progress. The status arrives before anything streams, so a
  retry repeats nothing the user has seen; each wait emits `provider_retry {reason: "http"}` and a
  `status`, and the refusal becomes the failure when either cap is reached. Every caller of
  `complete()` inherits it — pane turns, side calls, the key test. The hosted gateway decides from
  its own error body (a rate limit is waited out until its window reopens; a spent allowance is
  never waited out; a refusal it marks `retried` — one it already failed over across its own
  upstreams — is final here, because the gateway owns those retries and asking again would re-run
  its whole chain, which is also why `gateway/proxy.RETRYABLE_STATUSES` is kept equal to
  `HTTP_RETRY_STATUSES`) and its token refresh continues the same count and budget, and a local model
  server is excluded: its 5xx are deterministic and its loading 503 has its own fixed wait. A provider that still
  fails a step hands the turn to another one (`agent.py` `_begin_failover`, card #G9VE): the same
  tier's model on the next keyed preset — never a second key on the same host, never after part of
  an answer has been streamed — then Relay Free where the pane allows it, at most two, for that turn
  only. The move converts
  the history to the new provider's dialect and follows its context window, and the restore, before
  the turn's terminal event, puts all of it back, so the pane keeps the model the user chose; if the
  chain ends in failure the turn reports the *first* provider's error, not the last one's. Options ›
  Models can turn it off (`agent/failover`). **Relay Free is the one target that needs permission**
  (owner, 2026-09-19): every other candidate is a provider the user set up with a key they stored,
  and Relay's hosted service is another company's terms and a shared allowance, so `failover_hosted`
  (Options › Models, off by default) gates it and a pane already on Relay Free needs no tick.
  **Subagents fail over on the pane's chain**: `subagents.py` hands each one the pane's role
  resolver, its preset and both switches, which it did not before — without a resolver
  `_begin_failover` cannot know which presets are keyed and refuses every move. **A routed step —
  plan mode's or an image turn's — drops back to the pane's own model instead of failing the turn**
  (`_drop_routing`, `provider_retry {reason: "route_dropped"}`): a pinned planning model whose
  provider is down used to end the turn with the pane's own model sitting there able to answer, and
  the ordinary chain now starts only if that model fails too.
- Extra request keys are limited to `thinking`, `reasoning`, `reasoning_effort`,
  `temperature`, `top_p`. `max_tokens` is **0 or 256–131072**, and 0 — the default, and every fallback when `provider/max_tokens` is unset — means *automatic*: the model's own documented output cap (`presets.max_output`; GLM-5.3 and Kimi K3 131072, GPT-6 Astra 128000, Gemini 3.1 Pro **65536**, an aggregator, the hosted gateway or an endpoint Relay cannot name 32768, a local server a quarter of its served window). A pinned number is kept but never sent above that cap, because a request over it is refused rather than trimmed. Output caps are published per model and are not a share of the context window: Gemini has a larger window than GLM-5.3 and half the output.
- Limits: 8 MiB request and response, 2 MiB per SSE event, 16 tool calls per response,
  30 s socket timeout. Cancel closes the response from another thread.
- Tool-call fragments are assembled by index. `reasoning_content` and OpenRouter's
  `reasoning` are kept in history for later tool turns, not displayed.
- A stream without `[DONE]` or a `stop`/`tool_calls` finish, or with `length` or
  `content_filter`, is an error; partial tool calls never run. HTTP error bodies are not echoed.

Presets (`backend/relay_core/presets.py`; the advanced dialog in `src/Pane.h` keeps a copy that
`tests/test_presets.py` checks for drift). Every endpoint and model id was verified against the
provider's own documentation on 2026-09-17, and the doc URL sits beside the entry it supports.

| Id | Group | Base URL | Model | Effort style |
|---|---|---|---|---|
| `glm-coding` | subscription | `https://api.z.ai/api/coding/paas/v4` | `glm-5.3` | `glm` |
| `kimi-code` | subscription | `https://api.kimi.ai/coding/v1` | `k3` (also `k3-256k`, `kimi-for-coding`, `kimi-for-coding-highspeed`) | `kimi` |
| `minimax` | subscription | `https://api.minimax.io/v1` | `MiniMax-M3` | `none` |
| `openrouter` | aggregator | `https://openrouter.ai/api/v1` | `deepseek/deepseek-v4.1-flash` | `openrouter` |
| `glm` | pay-as-you-go | `https://api.z.ai/api/paas/v4` | `glm-5.3` | `glm` |
| `kimi` | pay-as-you-go | `https://api.moonshot.ai/v1` | `kimi-k3` | `kimi` |
| `openai` | pay-as-you-go | `https://api.openai.com/v1` | `gpt-6-astra` | `openai` |
| `anthropic` | pay-as-you-go | `https://api.anthropic.com/v1` | `claude-opus-5` | `none` |
| `gemini` | pay-as-you-go | `https://generativelanguage.googleapis.com/v1beta/openai` | `gemini-3.1-pro-preview` | `gemini` |

Provider quirks the effort styles encode: Z.AI rejects `thinking.type: "disabled"` on GLM-5.3 and
GLM-5.3-Flash, so the Flash tier asks for `reasoning_effort: low` with thinking still enabled;
Kimi documents `reasoning_effort` for `kimi-k3` only, so the high-speed models carry none; Gemini
rejects `reasoning_effort: "minimal"` on 3.8 Flash, so `max` maps to `high`; Anthropic's
OpenAI-compatible layer ignores `reasoning_effort` and no longer accepts `thinking` on Claude 5, and
MiniMax has no `reasoning_effort` at all — both use the `none` style, which sends no effort field and
leaves the model's own default. MiniMax's Coding Plan was renamed the Token Plan and shares the
pay-as-you-go base URL; only the key differs, and the two kinds are not interchangeable.

The advanced Provider dialog (Options › Models › Advanced provider settings) still accepts a custom
base URL, model and extras, requires an existing workspace and a consent checkbox for sending prompts
and tool results to the provider. Saving makes no network call. Switching model starts a new
conversation.

### Agent loop

`backend/relay_core/agent.py`. One conversation per worker. The system prompt tells the model
that tools run without confirmation, that tool output is untrusted, and that `run_command` is a
separate non-interactive shell. Per turn: at most 256 model requests and 150 tool calls by default
(`max_steps`/`max_tool_calls`, configurable); hitting a limit ends the turn with
`done {stop_reason: "limit"}`, which does not pause the queue. On cancel or error the user's prompt
and delivered steers stay in history; only a half-finished tool-call group is completed with
"not completed" results, and a note says the request is unfinished and state must be reinspected.
Context accounting and compaction: `context.py` (protocol section 4 and 12.7).

**Requests and todos** (`requests.py`, `todos.py`; protocol section 12). Every prompt becomes a
ledger entry `R<n>` when it is submitted (queue, steer, interrupt, requeue, Relay-origin), saved in
the session and never summarized. The model keeps a todo list with `update_todos` (linked to
request ids); linked todos set request statuses. Steers are framed with their id and keep their
attachments. When the model stops with open todos for the turn, the worker re-prompts at most twice
(`completion_check`), then `done {open_items}`. After 8 steps without a todo update, a short
reminder is added; and once a turn passes 4 tool calls having never called `update_todos`, one nudge
asks for a list if the request has several parts (`NO_LIST_TOOL_CALLS`, card `D8VN`) — the stale
reminder needs open todos, so nothing otherwise noticed a model that never starts a list, and such a
turn is neither completion-checked nor counted as tasks. Compaction inserts a deterministic carried block after the summary (requests
verbatim, todos, plan, files, subagents, recent user messages up to ~20K tokens), and only
`relay_kind: "prompt"` messages count as turn starts. An optional audit side call
(`audit_requests`, route-assist model) only flags possibly unaddressed asks. Subagents have none of
this (no ledger or todos).

**Tasks UI** (`src/RequestLedger.*`, `src/RequestsPanel.*`, library `relay-requests`, tests
`tests/requests_test.cpp`). `RequestLedgerModel` holds the latest `requests`/`todos` lists and
derives **tasks**, all in the GUI (no protocol change). **A task is one of the model's todos and
nothing else.** A user request is never a task and is never shown: the ledger is internal machinery
(it links todos, survives compaction, and drives re-asks and the completion check), and a prompt is
not a plan. Until 2026-09-17 a request with no linked todos counted as a task itself, so every
one-ask turn reported `Tasks 0/1` → `Tasks 1/1` — a turn-completion indicator in task vocabulary,
since a todo-less request is marked `done` by `finish_turn()` purely because its turn ended normally
(`backend/relay_core/requests.py:178`). `deriveAll()` keeps the old request pseudo-tasks for one
purpose only: the batch walk, so a turn that wrote no todos still closes a task list.

Outcomes: todo `completed` → completed; `blocked` → failed; `deferred` → deferred; `cancelled` →
cancelled. Open todos are *active* while any request is `in_progress` or a linked request waits in
the queue (not delivered, or re-asked/requeued: a new `queue_item` since last delivery), otherwise
*unfinished* (the turn ended by error, cancel, the step limit or an exhausted completion check);
open todos of a request the user marked done or cancelled follow it. Settled todos the model later
drops from its list are kept in the count (so `5/5` does not shrink); dropped open todos disappear.
**Batches:** a new request starts a new task list when the current list has tasks, none active, no
request in progress and it did not arrive in the same turn; unfinished (and re-asked) tasks of
earlier lists move into the current one. After a restart or `/resume` the same walk runs over the
loaded ledger; a ledger whose highest id went down or whose ids changed text (new chat, other
session, rewind) resets the batches.

The `requestsChip` shows `Tasks c/t` (property `state`: `running`, `done` green, `attention` amber
with the suffix "(1 failed, 1 deferred, 1 cancelled, 1 unfinished)" once settled), hosted in the
queue strip header while the strip is visible and in the composer row otherwise. **It is hidden
whenever the model wrote no todo list**, which the prompt tells it to skip for a single simple ask
(`backend/relay_core/todos.py`), so simple turns carry no task UI at all. `turnEndLine()` gives the
`✦ Tasks …` line printed on `done`/`cancelled`/`error` (skipped for a list of one completed task, so
also skipped when there is no list). `RequestsPanel` floats over the right of the terminal and lists
the current batch's todos, then a folded `Earlier · c/t (…)` row; the selected task's full text,
status and note show below. Marking done, cancelling, reopening and re-asking were request
operations and went with the ledger. **Tasks and subagents** (card #QHR1, protocol 12.4): a todo a
subagent has shows `✦ a2` on its row and in the detail; Enter or a double click opens that
subagent's tab (`Pane::openSubagent`), and S or the row's context menu "Run as subagent" sends
`todo_subagent` for a todo that is not completed or cancelled (deferred and blocked ones can be
retried) and not already with a running subagent. The worker starts a
background `general` subagent on the todo and the verbatim requests it serves; the todo then
follows the subagent (in_progress, then completed / blocked / pending; while it runs the task counts
as active, not unfinished, after the main turn ends) and the subagent's strip row,
tab and ✦ lines read "T3 · …" (`SubagentRow::todoId`). The mouse paths show the hints
`tasks.subagent.open.mouse` (→ Enter) and `tasks.subagent.run.mouse` (→ S). Any number of tasks
may be in progress at once, the agent's own and its subagents' (owner, 2026-09-18: no "only one task
in progress" rule); the panel and chip count each as active. `request_set`, `request_get` and `request_reask`
remain in the protocol and in the worker, unused by the GUI. Toggle: `agent.requests` (Ctrl+Shift+K
in the Relay preset; unbound in the Warp, VS Code and Konsole presets, where the key clears blocks,
deletes a line, or clears scrollback), `/tasks`, `/requests`, `/todos`, the chip. **What a person
reads says "task", never "todo"** (card #SHE3, owner 2026-09-19: "i would rather call todos tasks";
"you can still call it todos internally"): the tool-call row is "updating tasks / updated tasks", the
fold's section is `tasks`, and `update_todos`' own messages say task. The wire is unchanged — the
`update_todos` tool, the `todos` and `todo_subagent` messages, `todos.py` and the C++ identifiers
keep their names — and `/todos` still works but is a **hidden** `SlashCommand`: left out of the `/`
popup and never completed from a prefix, so the palette never teaches the retired word, while the
name stays known (an alias cannot take it and it is never reported as an unknown command).
`openItemsLine()`
and `auditLine()` name todos and the user's own quoted words, never `R<n>` ids. `done {stop_reason:
"limit"}` prints a `relay://continue/<pane>` link handled by `WindowManager::handleOpen`; Continue
sends an ordinary ask. `max_steps`, `max_tool_calls` and `audit_requests` live in QSettings
`agent/*`, go into `configure` and are sent with `set_agent_options` when changed.

**The open task list under the prompt** (owner, 2026-09-19). The current task list also shows in the
strip beneath the composer, beside the running agents — the same widget, `SubagentsPanel`
(library `relay-subagents`, which therefore links `relay-requests`; the pure layout is tested in
`tests/striplayout_test.cpp`). The strip is hidden when there is neither a listed subagent nor an
open task, so it costs the terminal no rows on a simple turn. It draws at most `kMaxVisible` = 5
task rows out of the current batch, in list order, through a window **centred on the marginal
task** — the first `in_progress` todo, else the first that is not completed, done or cancelled, else
the tail of the list — with the marginal row third of five, so two rows of context show above and
below it. Completed tasks of the same list show as `✓` rows whenever the window has room: the list
reads as a checklist, and "centre on the marginal task" only means anything if the settled ones are
in it. With subagents and tasks both, the rows split at half width: **subagents on the left, tasks
on the right**, one row per (subagent, task) pair, a violet `theme::Agent` connector on a linked
pair, and `↳` in place of the name on the second and later rows of one subagent. `Left`/`Right` move
between the two columns and the vertical chain (prompt → strip → jobs list) is unchanged; `Enter`
opens the task's subagent when it has one and is listed, otherwise the task list on that task
(`Pane::openTask`); `S` sends `todo_subagent` for a `delegable()` todo, exactly as the floating
panel's S does; `x` and `m` stay subagent-only. The `main` row stays in both modes — it is where the
keys are taught. **No protocol change**, and the link is still one subagent per task at a time
(#QHR1): the N-rows-per-subagent pairing is built generically so the strip is already right the day
`todo_id` becomes a list, but today N is always 1. The real multi-row case is the inverse — several
finished subagent rows pointing at one re-run todo, each with its own row and the task cell drawn on
the first.

### Tools

`backend/relay_core/tools.py`. **There is no approval step.** Each call is validated and
prepared, `tool_started` carries a preview, and it executes immediately.

| Tool | Behavior |
|---|---|
| `run_command` | `/bin/bash --noprofile --norc -c` in the workspace (or a workspace-relative `cwd`), new session, stdin `/dev/null`. Timeout 1–120 s, default 30. Output streamed as `tool_output`, 32 KiB returned. The process group gets SIGTERM then SIGKILL. Environment scrubbed: names containing KEY/TOKEN/SECRET/PASSWORD/CREDENTIAL/COOKIE, `RELAY_*`, `BASH_ENV`, `ENV`, `PYTHONPATH`, `LD_PRELOAD`, `LD_LIBRARY_PATH`, `SSH_AUTH_SOCK`, `BASH_FUNC_*`. Sets `TERM=dumb`, `PAGER=cat`, `GIT_TERMINAL_PROMPT=0`. |
| `read_file` | UTF-8 regular file, 128 KiB, no NUL bytes |
| `list_directory` | at most 200 entries; never recursive, so it is allowed in any directory, `$HOME` and `/` included |
| `write_file` | parent must exist; unified diff in the preview; the file's SHA-256 is rechecked before an atomic replace that keeps its mode |
| `edit_file` | replaces one exact string in an existing file (`old_string` → `new_string`, `replace_all` for every occurrence); refuses a file that does not exist, a string it cannot find, and one that occurs more than once without `replace_all`; same guards, SHA-256 recheck, atomic replace and checkpoint undo as `write_file`; neither is offered in plan mode |
| `set_keybinding` | offered when the GUI sent a catalog (section 4) |
| `load_skill`, `read_skill_file` | offered when at least one skill is indexed |
| `ask_user` | asks the user 1–4 questions and **blocks the turn** until the pane answers (`backend/relay_core/questions.py`, protocol 27, card #MQ9C). `options` is optional: with them a number is the answer and `0` skips, without them the question is open and whatever is typed is the answer; `/skip` passes. The pane prints the card in the amber "needs human" ink and goes to the `NeedsYou` state. Offered in both modes, never to a subagent, which cannot reach the user; plan mode's prompt additionally tells the planner to use it before `write_plan` rather than guess |

**The recursive-walk cost guard (card #2Y96).** A pane's workspace is the directory the pane is
in, so a pane standing in `$HOME` or `/` gets a very wide sandbox. The owner accepted that on
2026-09-19 — the agent works here without per-action approvals, and narrowing the sandbox by depth
would be theatre — and what Relay guards instead is the **cost**: `run_command` refuses a recursive
search or listing whose root is the home directory, `/`, or a directory the home sits under
(`/home`), because crawling it takes minutes and returns nothing usable. The refusal names a
narrower path to pass (`<cwd>/<subdirectory>`). Recognised walkers are `find`, `rg`/`ag`/`ack`,
`fd`, `tree`, `du`, and `grep`/`ls` with `-r`/`-R`; the root is the command's own path argument, or
the cwd when it has none. An explicit path below a wide root always runs, a non-recursive
`list_directory` of `$HOME` or `/` is untouched, and the board's `search_files` shares the guard
(`relay_core/board_tools.py`). Like the command denylist this is honoured, not unevadable — a root
hidden in a variable runs, bounded by the entry, byte and time ceilings as before
(`tools.walk_cost_refusal`).

File tools accept absolute paths and `..`, but every path is resolved and must land
inside the workspace. Symlinks anywhere in the workspace part of the path, paths that
resolve outside it, and `.ssh`, `.gnupg`, `.git`, `.env*`, `id_rsa`, `id_ed25519`, `*.pem`,
`*.key` are refused. These checks reduce mistakes; they are not a sandbox. `run_command`
has the user's full filesystem and network permissions.

Remaining controls without approvals: the system prompt, file-tool guards, environment
scrubbing, timeouts, output caps, step and tool limits, the Stop agent action, and the inline
preview of every action. Stop does not undo completed actions.

### Skills

`backend/relay_core/skills.py`. At configure time the worker indexes
`<dir>/<name>/SKILL.md` files with `name`/`description` frontmatter. Default directory:
`~/.warp/skills`; `configure.skills` can set `enabled`, up to 8 absolute `dirs`, and
`project: true` for `<workspace>/.warp/skills`. The GUI does not send `skills` today, so the
default applies. A list of skill ids and descriptions (6 KiB cap) is appended to the system
prompt as lower-priority guidance. `load_skill` returns up to 64 KiB of `SKILL.md` plus the
folder's file list; `read_skill_file` reads a text file inside the folder. Symlinks, `..` and
binary files are refused. Skipped folders are reported in `configured.skills_skipped`.

GUI: `src/SkillsDialog.*` (non-modal, from `/skills`, the palette or Agent options) lists
`skills_list` items with a checkbox per skill (unchecked names go to QSettings `skills/exclude`,
sent in `configure.skills.exclude` for new sessions), "overridden" for `shadowed_by`. Refine sends
`refine_skills` and opens the first refined `SKILL.md` in an editable pane. Import sends
`import_skills_preview`, shows a modal review (checkbox per skill, files as children) and sends
`import_skills_confirm` with the checked names. Check for updates reads the repository URL from
the imported skill's `../.relay-import.json` and sends `skills_check_updates`. Dialog requests
use ids `skills-N` so worker `error` events route to the dialog's status line.

### Queue and interrupt

`backend/relay_core/queue.py` (`TurnSupervisor`) runs every turn on one dispatcher thread.

- `queue` appends; `now` is refused while busy; `interrupt` goes ahead of ordinary queued
  prompts (FIFO among interrupts) and stops the running turn.
- `cancel` stops the turn, drops waiting interrupts and **pauses** the queue. A failed turn also
  pauses it. `resume_queue` continues. `now` and `interrupt` still run while paused.
- At most 32 queued prompts. `configure` and `reset` clear the queue when idle.
- Each accepted prompt is recorded in the agent's request ledger before it is queued
  (`queued.ledger_id`); removing or clearing queued prompts marks them `cancelled_by_user`.

GUI (`Pane::submitAgent`, `rebuildQueueStrip`): every agent prompt is sent with `when: "queue"`
(or `now` while paused). The prompt is echoed when its turn starts (`agent_started`), not when
queued. Busy state follows `agent_started`/`agent_finished`. The queue strip floats over the
bottom of the terminal: running prompt, numbered queued prompts with ×, Clear, and
"PAUSED · Resume". The palette offers Clear agent queue and Resume agent queue when relevant.
Full protocol: [QUEUE-INTERRUPT.md](QUEUE-INTERRUPT.md).

**An agent submit does not wait behind shell work** (2026-09-19, #N8VK). The one list is two
resources: an agent prompt contends with the agent alone, a command with the terminal.
`src/QueueSubmit.{h,cpp}` (`relay::queuesubmit::decide`) holds the start rule — an agent prompt
submitted while the agent itself is free starts its turn at once, bypassing the list exactly as
an interrupt does, and the queued items keep their order; only a busy (or just-started) agent
turn sends a prompt to the back. `submitAgent`, `startFix`, `finishHandoff` and `runBoardTask`
all go through it. The router is asked only for `auto`: an explicit agent submit (Ctrl+Enter,
the `*` prefix, AGENT mode) is dispatched locally, with no `route` round trip to wait on.

**And the shell does not wait on the agent worker at all** (2026-09-19, the #N8VK follow-on). The
router lives in the worker; the terminal does not. `relay::input::withoutRouter(mode)`
(`src/InputPolicy.{h,cpp}`) is the rule for a submit while the worker is down — the pane's banner
reading "The agent worker exited": a line already addressed to the terminal (the `!` prefix,
TERMINAL mode, Ctrl+Shift+Enter) is dispatched locally and runs, a line addressed to the agent
takes the agent's own path, and only `auto` is refused, because only `auto` has a question the
router alone can answer. That refusal now says what is wrong and names both ways on —
`relay::input::noRouterText`, which quotes the banner's own Restart agent and the key that sends
this very line to the terminal — instead of the old "Local router is not ready; use the native
terminal or restart Relay", which took the whole prompt box away from someone whose shell was
working perfectly well. `Pane`'s worker-exit handler also drops a `route` left in flight
(`m_pendingSubmit`, `m_previewId`, `m_heldDecision`): no reply can ever release that
one-submission guard, and left behind it silently swallowed every later terminal submit.

**One list, in delivery order** (2026-09-18, #C4M8). Under "▸ running", `m_queueList` holds every
row the pane will deliver: first the steers still waiting for the running turn's next tool call
(`m_steering`, drawn "↪ next tool call ✦" in the agent colour), then the queued prompts and
commands (`m_entries`). `QueueRowDelegate` draws each by its kind role; each row has a stable id,
`steer:<request id>` or `entry:<queue id>`. `Pane::queueRows()` returns the rows (id, kind,
preview, state) in order and `Pane::removeRow(id)` removes a queued row or withdraws a steer, for a
later remote path. The same keys reach every row: Up on the empty prompt box selects the **top** row,
Shift+Delete and the row's × remove a queued row and withdraw a steer (`queue_remove`, the row greys
to "withdrawing…" until `steer_removed`). A steer is shown in the prompt box when selected; Enter or
the first edit takes it back (withdraw, text stays in the box as the user's draft, Enter queues it
again), Ctrl+Down moves it back to the head of the queue (withdraw, then prepend on
`steer_removed`), Ctrl+Enter sends it now. Ctrl+Up on the head queued agent prompt while the agent
works makes it a steer (`steerQueuedEntry`, the path Enter-Enter uses). Steers do not drag; a
queued row dropped above them goes to the head of the queue, and an agent prompt dropped there
while the agent works becomes a steer, as Ctrl+Up would make it. If the turn takes a steer before
its withdraw arrives, the transcript line stands and the status line says so; a copy put back in the
prompt box for editing is cleared if untouched and kept if edited.

**Editing a queued item.** Up on an empty prompt box selects the top row and puts its text
in the prompt box, where it is edited in place; `Pane::selectQueueEntry` / `saveQueueEdit` /
`leaveQueueSelection` own that, and `src/QueueNav.{h,cpp}` (`relay::queuenav::decide`) holds the key
rules so they can be tested without a widget — chiefly when Up and Down move between items and when
they belong to a multi-line item's own text (only the first and last line reach past it, as in any
shell history). Enter saves, Esc drops the edit, Ctrl+Up/Down reorder, Shift+Delete removes
(plain Delete and Backspace are the text's now). The highlighted row keeps the stored text until the
edit is saved, so the row and the box differ only while an edit is in flight.

While the **top** item is highlighted the queue holds: `queueHeldBySelection()` is derived from the
selection rather than stored, so leaving the selection releases the queue with no second piece of
state to keep in step, and it covers an item drifting to the front while it is being edited.
`pumpQueue` and `moreTurnsPending` go through `queueBlocked()` (a real pause, or this hold).
Anything that changes the queue under a selection — the head starting, a removal, a drag — passes
the selected item's id through `keepSelectionOn`, which follows that item to its new index or, if it
has gone, gives the prompt box back empty.

### Image context

`src/Images.{h,cpp}` and protocol section 17 (issue EM1E). Four inputs, one path: pasting or dropping
a picture into the prompt box writes it to `$XDG_CACHE_HOME/relay/images` and inserts an `@path`
token, an image file named by a drop or by `@path` is attached where it is, and
`agent.screenshotPane` (Ctrl+Shift+G, Actions › "Screenshot this pane") grabs the pane as drawn.
Everything after that is the existing `ask {attachments}` plumbing.

The worker recognises an image by its first bytes, sends it as an OpenAI-compatible `image_url`
content part with an inlined base64 data URL (never an http URL), and caps it at 3 MiB per image,
4 images and 6 MiB per turn. A turn whose images the pane's model cannot read runs on the **vision
model** for that turn only and then goes back, which `vision_route` / `vision_route_ended` say in the
pane and in the model chip; with no vision model it is refused (`vision_unavailable` plus `error`)
rather than sent and rejected. The vision model is the `vision` role, chosen in its own row beside
the Main / Flash / Lite tiers in Options › Models; its default is the provider's own image model,
which on GLM is `glm-5.3-flash`. When the turn ends, each image is replaced in the conversation by a
one-line description and its path, and an image is estimated as a flat `context.IMAGE_TOKENS` so it
cannot compact its own turn.

### Aliases: saved commands and prompts

`src/Aliases.*` and `backend/relay_core/aliases.py` (issue `#G8DK`, protocol section 20). An alias
is a saved terminal command or agent prompt with `{{parameter}}` placeholders, stored as one
Switchboard card per alias: **global** aliases in the global Switchboard
(`$XDG_CONFIG_HOME/relay/switchboard/aliases/`), **local** ones in the repository Switchboard
(`issues/aliases/`, or `.relay/aliases/` before a board exists). A local alias hides a global one
of the same name.

It runs three ways — the "Aliases…" palette submenu, `/name` in the composer, and the name typed on
its own in terminal mode — and all three send one `alias_run`, so the substitution happens once, in
the worker. A template whose parameters all have values runs straight away; one with a blank lands
in the prompt box with the blank selected and Tab moving between the fields. Nothing runs without
passing through the prompt box, and a line edited past recognition stops being an alias and goes to
the router as itself (`relay::aliases::reparse`).

Substitution is **quote-aware**: a value is escaped for the shell context it lands in, so a value
holding `;`, `$(…)` or a backtick is one literal word, never new syntax. Importing Warp workflows
(from a read-only copy of `warp.sqlite`, and from workflow YAML) and shell aliases (read, never
sourced) always shows a preview of exactly what would be stored, with the origin and any warning;
the worker writes from its own copy of that preview. The agent may propose an alias for a command
run three times or more — a suggestion only, logged in `worker.log`.

## 11a. Guest agents: Claude Code and Codex in a pane

A **guest** is a CLI agent process (Claude Code, Codex) running in an ordinary terminal pane — not
a Relay worker, not a BYOK preset — *or*, since Tier A (protocol section 29, owner 2026-09-19), the
same CLI driven headless as the pane's agent through its own harness, which is the one case where
Relay does more than observe: it runs the guest's sanctioned headless mode and nothing else. Relay's posture is the same as Warp's guest terminal agent
("Relay does not try to turn Claude Code into a second worker backend; it observes, and it touches
a guest only through the guest's own sanctioned surfaces", issue `GT7X`): no reverse-engineered
internals, no keystroke automation, and any contact beyond observation is a step Relay offers and
the user confirms — Relay never reaches into a project on its own. That still holds under the
launch-time configuration of §26.9 (owner, 2026-09-19: no per-project setup): starting a guest
writes one file, in the pane's own runtime directory, and the only files a launch *changes* are the
ones the retired installers wrote into, from which it removes exactly Relay's own marked entries
and nothing else. The reference spec is
`docs/AGENT-SESSIONS-PROTOCOL.md` section 26, whose contracts (§26.3–26.9) are binding; every
deviation from this section is written down there. Guests are **local-only**: none of the pane's
`guest_model` / `guest_context_pct` / `guest_busy` state or the five guest event kinds crosses the
wire to a remote host (`GUEST_CHANNEL_EVENTS` in `remote/wire.py`, one regression test). The module
root is `backend/relay_core/guest.py`; everything else is `guest_*.py` (no exceptions).

- **Detection** (`guest.py`, and the pane's foreground watch): one `GuestSpec` per tool and
  `classify_command()` over the live foreground argv — mirrored in C++ where the pane already
  classifies its foreground process, one rule in two languages kept side by side — so a pane
  *knows* it is running claude or codex (`Pane::guest()`) without scraping the screen.
  `detect_installations()` answers what is installed from the tool itself (binary, version,
  config dir), and an unreadable install is "installed, details unknown", never a guess.
  The model picker's own test is narrower and just as concrete: a guest is offered when its
  binary is on `PATH` (`QStandardPaths::findExecutable` over the same `guestSpecs()` table).
- **One event channel** (§26.3): everything a guest phase learns reaches its pane as one
  envelope — `{"token", "sequence", "event", "guest", "data"}`, the event one of `hook`,
  `statusline`, `state`, `bridge`, `slash` — written by the single `shell/guest-event.py`
  helper as **one file per event** in the pane's `guest-events/` spool directory
  (`<time_ns>-<pid>-<counter>.json`, `mkstemp`ed then `os.replace`d into place), which the
  pane lists beside `state.json` on the same tick, handles in name order and then deletes,
  in `Pane::pollGuestEvents`. It was a single `guest.json` slot until the review of 51587e3:
  a statusline tick landing on top of a permission question replaced the question, and the
  shim then waited out its whole timeout for an answer nobody had been shown. The
  pane accepts only its own pane token and a fresh sequence, and a missing `RELAY_GUEST_EVENT`
  makes the helper a no-op that writes nowhere, so a hook entry read by a claude with no Relay
  around it is inert — which is why the launch file is harmless wherever it is read, and why the
  stopgap's leftovers were harmless until a launch removed them. All five kinds are withheld from the wire, and
  any future one is refused by the same regression test until an explicit owner decision adds
  it to `GUEST_CHANNEL_EVENTS`. A helper Relay starts *itself* (the slash scan, the codex tail
  of §26.6, the launch helper of §26.9) is handed this pane's spool, token and runtime dir explicitly, in
  `Pane::guestHelperEnvironment()`: `startTerminal` publishes them with `qputenv`, which writes
  the GUI's own environment, so anything that inherits it names whichever pane started its shell
  last — pane A's events landed on pane B's spool under pane B's token, and pane B took them.
- **Claude hooks and the statusline shim** (§26.4, `guest_install.py` + `guest_hook.py`): the
  hooks `PermissionRequest`, `UserPromptSubmit`, `Stop` and `Notification` calling
  `"${RELAY_PYTHON:-python3}" "$RELAY_BACKEND_DIR/relay_core/guest_hook.py" <event>
  --relay-guest` behind a `$RELAY_GUEST_EVENT` guard (§26.4; the `-m relay_core.guest_hook` form
  expanded to an empty command and exited 127), plus a `statusLine` shim that feeds the pane's
  guest chip while claude still renders its own line. Since 2026-09-19 they are not *installed*
  anywhere: `guest_install.relay_entries()` is the one place they are spelled, and the launch of
  §26.9 writes that object into the pane's own runtime directory and hands it to one claude with
  `--settings`. A statusline the user wrote themselves is still kept — the launch file leaves
  `statusLine` out when any file that claude reads carries a line that is not Relay's, and the
  chip then simply has nothing to show. `PreToolUse` is deliberately **not** requested, because
  it fires before every tool call including the ones the user's own rules already allow. A
  `PermissionRequest` is answered as a Relay question on the pane, never auto-approved — but a
  guest Relay launched runs with `--dangerously-skip-permissions` and so never fires one; the
  question bar is for a claude the user started themselves. What is left of the installer is the
  migration: every launch removes exactly the `--relay-guest` marked entries the retired Options
  page wrote into `.claude/settings.local.json`, `~/.claude/settings.json` and
  `~/.codex/config.toml`, because those entries beside the launch file would run every hook twice.
- **Codex: `notify` and the rollout tail** (§26.6, `guest_codex.py`): Codex has no IDE bridge, so
  what a picked codex gets is the rollout tail, the finished-turn `notify`, the sessions sources
  and the same bypass rule (`--dangerously-bypass-approvals-and-sandbox`); what it does not get is
  diffs in Relay or `openFile`, documented rather than faked. Its two settings —
  `notify` (run at the end of a turn with the JSON payload as one argv item) and
  `[tui] notification_condition = "always"` (Codex otherwise stays quiet in the focused terminal)
  — are `-c key=value` overrides on that one codex's command line since 2026-09-19, so nothing
  lives in `~/.codex/config.toml` and nothing fires in anyone else's terminal. The TOML
  *document* writer stays as the migration's half (the retired entries are removed at every
  launch, byte for byte on everything it does not own) and as the tested inverse that holds it
  to that. Codex 0.155.1 does have a stable `hooks` feature with Claude Code's schema; Relay adds
  none, because busy and the finished turn are already covered and a `PermissionRequest` cannot
  fire under the bypass flag — they are the route if a Codex permission bar is ever wanted
  (§26.6). What the pane knows about a codex turn comes from
  `notify` — forwarded as a `hook` named `notify`, which the pane turns into the finished-turn
  notification — and from `guest_codex.py tail`, one short-lived helper the pane starts when a
  codex turns up in its foreground and ends when it leaves: it follows the newest rollout under
  `~/.codex/sessions/YYYY/MM/DD/` whose own cwd is this pane's and emits `state` and
  `statusline` exactly as claude's shim does, which is what makes the codex chip and the
  composer's "queued until it is ready" true for codex too. The app-server daemon stays Tier A.
- **The Claude IDE bridge** (§26.5, `guest_bridge.py`, one sidecar per GUI run): Relay plays
  the *editor* side of Claude Code's IDE integration — JSON-RPC 2.0 over a loopback-only
  WebSocket, discovered upstream's own way: `CLAUDE_CODE_SSE_PORT` and
  `ENABLE_IDE_INTEGRATION` (`guest.bridge_env`), and the `~/.claude/ide/<port>.lock` file a
  claude started anywhere reads to find the same server. Those two variables are **not** in the
  shell any more: they are assignments on the launched guest's own command line (§26.2), so no
  other program in that shell, and no shell started later, is handed a port that may have gone
  away. There is no setting either — the bridge is always on, started lazily by the first guest
  launch and stopped 60 s after the last guest pane's guest has left, the grace period being what
  keeps an `/exit`-and-relaunch from paying for a sidecar start. A pane registers at shell start
  while the bridge is running and again when its guest arrives, so a claude's first request
  routes. It serves the twelve IDE tools; `getDiagnostics` answers
  `[]` — Relay has no LSP source, documented rather than faked. `openDiff` is the blocking
  one: the unified diff travels the one channel as a `bridge` event, the pane opens Relay's
  diff view beside itself, and **Accept / Reject in that view's own header** (`DiffView::
  setDecision`) is the answer — the call returns only then: `FILE_SAVED`, after the *sidecar*
  writes the file, so the GUI never writes a user file, or `DIFF_REJECTED`, which is also what
  an unmatched, timed-out or abandoned request gets, because a guest left hanging is worse than
  a guest told no. The pane's banner ("claude proposes changes to …") is a pointer at that pane
  and nothing more: any other banner may replace it, and dismissing it decides nothing. Every
  path an openDiff names must `realpath`-resolve inside the one pane's workspace, checked again
  at the moment of the write.
- **Sessions** (§26.7, `guest_sessions.py`): Claude's `~/.claude/projects/<cwd-slug>/`
  transcripts and Codex's `~/.codex/sessions/` rollouts are two more Conversations sources
  (`claude`, `codex`) beside Relay's own sessions and subagent threads. A record is
  `{source, id, title, mtime, workspace, message_count, resume_command}`, and
  `resume_command` is always the tool's own (`claude -r <id>`, `codex resume <id>`, and their
  fork variants) — resuming is the tool's affair, run in a pane like any other command, in the
  session's own `resume_cwd` because both guests resolve an id against the directory they start
  in. Enter runs it in the focused pane, Shift+Enter in a new pane created in that directory
  (`RelayWindow::openGuestPane`), Ctrl+Enter the row's fork. All three go through
  `Pane::launchGuest(guest, extra, cwd)` (§26.9) with the row's argv tail as `extra`, so a
  resumed guest is configured exactly like a picked one — same launch settings file, same bypass
  flag, same bridge variables on the command line. The index is a cache as everywhere else:
  `_conversations()` annotates the guest rows with those fields and sets
  `guest_sessions.reconcile()` going on a background thread, whose second `conversations` event
  replaces the list only when the rescan actually changed something. Rename / pin / delete stay
  index-only (`ConversationIndex.rename` / `set_pinned` / `delete_session(remove_files=False)`,
  and `update_guest` merges a name and a pin back over a re-index): Relay never edits the tool's
  files, so a deleted row is Relay's copy and the session is listed again at the next scan.
- **Composer and input** (`guest_slash.py`, `src/Pane.h`): the pane builds the guest's slash
  catalog from a static `relay_core.guest_slash` scan (claude: the built-ins plus the skills
  and legacy-command locations it documents; codex: the stable TUI set), refreshed by live
  `slash` events, and shows those commands in the `/` menu marked as the guest's own. Picking
  one sends it to the pane as plain text — queued while the guest is busy ("Queued · sent to
  <guest> when it is ready"). The composer itself is unchanged in a guest pane (§26.8): the
  prompt box, the mode chip and auto-detection behave as they do anywhere, and only delivery
  differs — a line decided to be a command is typed into the guest as `!<command>`, which both
  TUIs run in their own shell mode, and a line decided to be a prompt is typed as the prompt.
- **The picker** (§26.9, `Pane::chooseGuest` → `Pane::launchGuest`, `guest_launch.py`): there is
  no Guests page and no setup step. "Claude Code" and "Codex" are rows in the pane's model box, a
  group after the presets, shown when the binary is on `PATH`, with `guest:<id>` as their item
  data and `/model claude` / `/model codex` as the same choice from the prompt box — no tier, no
  API key, no worker, so a guest works in a pane with no provider configured at all. Picking one
  stops a running worker turn, leaves a running guest first, asks the bridge for its port, then
  runs `python -m relay_core.guest_launch <guest> --runtime-dir … --cwd … --port …` with
  `Pane::guestHelperEnvironment()` and types the `command` it prints into the shell like any
  other terminal command — visibly, because it is exactly what the user could type themselves.
  Leaving a guest for a preset or a role types the guest's own `/exit` and applies the model when
  the shell is back at its prompt; a guest that is *working* is not interrupted, the switch is
  refused with a status line instead ("<Guest> is working — stop its turn first (Esc in the
  terminal), then switch"). Whether Esc should be sent instead is an open question for the owner.

- **Tier A: the guest as the pane's agent** (§29, `guest_harness.py` + `guest_harness_claude.py`
  + `guest_harness_codex.py` + `guest_harness_provider.py`): the owner un-deferred the headless
  harnesses on 2026-09-19. A guest is a worker preset (`guest:<id>`, no key, no tier); configuring
  it gives the ordinary `Agent` a `HarnessProvider` whose `complete()` runs one turn of
  `claude -p --input-format stream-json --output-format stream-json` or of `codex app-server`
  and forwards the guest's events in Relay's own vocabulary (`delta`, `tool_started`,
  `tool_result` with a diff, `context`, `question`), so the transcript, the call lines, the chips
  and the question card are Relay's, the pane's shell stays the user's terminal, and nothing is
  typed into a TUI. The model box lists the worker's row when the harness is usable and falls
  back to the Tier B launch otherwise; a hand-typed `claude` is still served the Tier B way.
  Adapters are held to recorded transcripts replayed through a fake process; no test starts a
  real guest.

The pane's guest state rides the ordinary `program_state` (`guest_model`, `guest_context_pct`,
`guest_busy`) so the title bar, tab labels and remote clients that are allowed to see process
state can render it; it is stripped from wire frames like the rest of the guest surface.

## 12. Keys, keyring and imports

`backend/relay_core/keystore.py`. A model server on this machine has no key and no entry here: its
id is `local:<slug>`, which the keystore refuses, and it is absent from the keys modal
([LOCAL-MODELS.md](LOCAL-MODELS.md)).

- Lookup order for a preset: environment variable `RELAY_<PRESET>_API_KEY` (for example
  `RELAY_GLM_CODING_API_KEY`), then the Secret Service keyring through `secret-tool`
  (`service=org.relayterminal.Relay provider=<preset>`), which works with GNOME Keyring and
  KWallet's Secret Service provider.
- Keys go to `secret-tool` on stdin, never in argv, files, QSettings or logs.
- At startup each pane asks for `presets`. If any preset has a stored key, it configures one
  without the key crossing the GUI pipe (`use_stored_key`): the saved preset, else Warp's
  default agent model, else the first stored key. A "custom" configuration whose base URL
  matches a preset also uses that preset's stored key.
- A key typed into the keys modal is sent over the private pipe (`store_key`) and written straight
  to the keyring; it is never held in the GUI, echoed back or written to QSettings. The advanced
  provider dialog still has the older path, where a typed key stays in worker memory and only reaches
  the keyring if "Save entered key to the desktop keyring" is ticked.
- **Keys modal** (`src/ModelSettings.h`, `KeysDialog`): one row per preset, grouped Subscriptions /
  Aggregator / Pay-as-you-go, showing whether the key is in the keyring, comes from
  `RELAY_<PRESET>_API_KEY` or is missing (`presets.key_source`), with Add/Replace, Remove
  (`remove_key`, hidden for an environment key Relay cannot delete), a link to the provider's key page
  and **Test**.
- **Test** (`backend/relay_core/keytest.py`, `test_key` → `key_tested`) makes one two-word, no-tools
  call with a 256-token budget on a background thread and reports ok or the HTTP status. The key is
  read inside the worker; provider error bodies are never echoed, because they can quote the request.
- **Warp import** reads `agents.custom_endpoints` from `~/.config/warp-terminal/settings.toml`
  (TOML 1.1 inline tables are normalized for Python's TOML 1.0 parser), reads keys from Warp's
  keyring entry (`service=dev.warp.Warp key=AiCustomEndpointKeys`), matches endpoints to presets
  by base URL, and stores each key under the Relay preset. It never returns key material.
- **Claude Code / Codex import** (`import_agent_tools`) reads `~/.claude/settings.json`
  (`env.ANTHROPIC_API_KEY`) and `~/.codex/auth.json` (`OPENAI_API_KEY`) and stores what it finds under
  the `anthropic` and `openai` presets. Both tools sign in with OAuth by default, and an OAuth token is
  not an API key — it does not work on the OpenAI-compatible endpoints Relay talks to — so only a plain
  key is imported and anything else is reported as skipped.
- CLI: `scripts/relay-agent.py --import-warp`, `--list`, or an interactive session on the same
  backend.

Non-secret provider settings live in QSettings (`provider/preset`, `base`, `model`, `extra`,
`max_tokens`) in `~/.config/RelayTerminal/relay.conf`, alongside the tier and role overrides
(`tiers/<tier>/…`, `roles/<role>/…`).

### Actions pane and Options pane

`src/SettingsPane.{h,cpp}` (`relay-settings`, `tests/settingspane_test.cpp`): one widget with a
`Mode`, hosted by a `ToolPane` of kind `Settings` whose title and `paneType` property ("actions" /
"options", which the pane chrome colours and labels a header by) follow the mode. The owner's line
between the two (2026-09-18): **an option persists** — a default, written to QSettings, true in every
pane after a restart — and **an action is something you do now**, to this pane, conversation or
window, and may do again or undo an hour later. "Default reasoning effort" is an option; "Reasoning
effort" for this pane is an action. A verb never sits in Options as a button row; a button row there
opens the editor of something that persists (API keys, Model roles, Instructions, Skills,
keybindings.json).

- **Actions** — Ctrl+Shift+A (`palette.open`), Ctrl+? (`help.shortcuts`). A search box over one
  filterable list, no tabs: `rootItems()` as described in section 5.
- **Options** — Ctrl+Shift+O and Ctrl+, (`app.settings`), and the gear in the title bar. One sub-tab
  per section (General, Appearance, Models, Terminal, Agent, Voice, Privacy, Keyboard) and the rows as
  real controls.

Each mode is its own pane, beside the focused pane in the splitter layout like the explorer and the
Switchboard — a full pane, not a strip over the right edge (owner, 2026-09-18). **Both can be open
at once** (owner, 2026-09-18: "you cant have the options menu and actions menu both open
simultaneously"); until then it was one pane whose mode the other key swapped, so a setting could
not be read beside the action that used it. `RelayWindow::openSettingsPane(mode)` looks up the pane
in *that* mode (`settingsPaneIn(page, mode)`) and opens one when there is none, so a key never
reaches into the other mode's pane; `settingsPanesIn(page)` is for the callers that mean both, such
as the redraw after a keymap or theme change. Choosing Options… in Actions opens Options beside the
list, which stays. Pressed while its own pane has the focus, the key closes it; so do Esc on an
empty search and the ✕. Both title-bar buttons are lit while both panes are open, and each closes
only its own. Closing
returns focus exactly where it was (vim in the terminal, or the prompt box). The pane is transient:
`node()` is empty, so it is never saved with the layout, and closing it when it is the last leaf of
the last tab puts a terminal pane beside it first rather than closing the window.

Options rows: a toggle row flips when clicked anywhere on it, choices are combo boxes, numbers are
spin boxes that write once per finished edit, text writes on `editingFinished`. Headings inside a
section (`SettingRow::Heading`) group long tabs (Agent: Instructions and skills / Turn limits; Voice:
Capture / Model; General: Diagnostics).
`RelayWindow::settingsSections()` builds the catalog of `SettingRow`s, each carrying its own reader
and writer, so QSettings stays the single source of truth and the pane knows nothing about how a
value is used; `searchableActions()` hands it the action catalog with the hidden search words folded
in. Changing a row rebuilds the pane, and the rebuild keeps the tab, the scroll offset, the highlighted
row and the focused control, so a long tab does not jump back to the top.

Search, in either mode, covers both catalogs so it never dead-ends, best match first, each row
saying where it lives ("General · …"), with the pane's own kind ranked first (the other kind's scores
are halved). In Options an action found this way runs like any other. In Actions an option is not
drawn as its control: it is an "Options › Appearance › Theme" row, and Enter swaps the pane to
Options on that tab with that row highlighted (`revealOption()`).

A submenu can also answer the search text with rows of its own (`ActionItem::typed`), listed
before every scored hit. **SSH** (card #S5SH, [SSH-AND-MOSH.md](SSH-AND-MOSH.md) section 8) uses it:

- **Connect to host…** (`menu:ssh`, action `ssh.connect`, no default key) lists the recently used
  hosts (QSettings `ssh/recent`, most recent first, at most 20) and then every concrete `Host` of
  `~/.ssh/config` and its `Include`s, each with the `user@hostname:port` its block writes
  (`src/SshConfig.*`, `relay-sshconfig`, `tests/sshconfig_test.cpp`). Typing `user@host` or
  `ssh <host>` offers exactly that host too. Choosing one opens a new tab whose pane runs
  `ssh <host>` once its shell is at the prompt (`Pane::queueCommand`, the pane's command queue).
- **Split on the same host** (`ssh.splitSameHost`, no default key; also "New pane on <host>" in the
  terminal's right-click menu) is listed while the focused pane is in an ssh or mosh login. It reads
  the session's argv from `/proc/<pid>/cmdline`, so quoting survives, drops the connection-sharing
  options Relay's own wrapper added (the new pane's wrapper adds them back), refuses anything that is
  not a login (`-O`, `-G`, `-N`, `-f`, telnet, …), and runs the line in a new pane to the right.
- **Options › Terminal › SSH**: `ssh/enhance` = `auto` (default) | `ask` | `off`, and the host lists
  `ssh/hosts_never` and `ssh/hosts_always` (QStringLists, edited as one comma-separated line).
- Hints: a new tab (`tab.new`) in which an ssh starts within 12 s teaches `ssh.connect` when it has a
  key (none by default, so no hint then); a split from a remote pane in which the same host is
  reached by hand within 60 s teaches Split on the same host, by its key or by name.

Keyboard, from the search box: ↑ ↓ (and Ctrl+N/P) move a highlight, Enter runs or changes the
highlighted row (runs an action, flips a toggle, opens a choice, focuses a field, clicks a button),
← → switch Options tabs while the search is empty, Esc clears the search and then closes; Esc
in a control goes back to the search first. What was taken from the reference apps is written at the
top of `src/SettingsPane.h`: Warp's search-first sections with instant apply, Claude Code's Enter/Esc
panel that returns to the prompt, JetBrains' and VS Code's action list kept apart from settings.

## 13. Per-pane isolation

`namespace isolation` in `src/Isolation.h`, probed once with `systemd-run --user --scope -- true`.

| Unit | Properties (defaults) |
|---|---|
| `relay-pane-<token8>-shell-<n>.scope` | `MemoryMax=8G`, `MemoryHigh=6G`, `MemorySwapMax=2G`, `KillSignal=SIGHUP`, `TimeoutStopSec=5`, `OOMPolicy=continue` |
| `relay-pane-<token8>-agent-<n>.scope` | `MemoryMax=2G`, `MemorySwapMax=512M`, `TimeoutStopSec=5`, `OOMPolicy=stop` |

`--scope` execs in place, so the PIDs Relay tracks are Bash's and Python's own. Settings in
`relay.conf` `[isolation]`: `enabled`, `shell_memory_max`, `shell_memory_high`, `shell_swap_max`,
`shell_oom_policy` (`continue` or `stop`), `agent_memory_max`, `agent_swap_max`. Invalid sizes
fall back to defaults. Without a systemd user manager, panes start unisolated and the status
bar says so once.

**Programs that leave their pane, and what can be done about it (card #Y4RX, 2026-09-19).** Two
programs are not in any pane's scope, however the pane started them: **tmux** moves its server into
`tmux-spawn-<uuid>.scope` and **Chrome** puts each app instance in
`app-com.google.Chrome-<pid>.scope`, both directly under `app.slice`. A child can ask the user's
systemd for a transient scope of its own over D-Bus, and the scope it gets is a *sibling* of the
pane's rather than a child, so nothing Relay does from inside the pane's scope contains it. The
consequences are plain and are not fixed: the memory those programs use counts against no pane's
limit, and an out-of-memory kill in `app.slice` can land on any process there — including one
another pane depends on, which is the cross-pane blast radius per-pane isolation exists to prevent.
Both were seen on 2026-09-19, with two `app.slice`-level OOM kills beside the two properly
pane-scoped ones.

What Relay offers is an opt-in mitigation, **off by default**: Options › Terminal › "Cap programs
that leave their pane (tmux, Chrome)" writes systemd user drop-ins on the two unit-name *prefixes* —
`~/.config/systemd/user/tmux-spawn-.scope.d/relay.conf` and
`~/.config/systemd/user/app-com.google.Chrome-.scope.d/relay.conf` — setting `MemoryMax=` and
`MemorySwapMax=` to the same values a pane's agent gets (`isolation/agent_memory_max`,
`isolation/agent_swap_max`, so "Auto" is the RAM-derived default), then runs `systemctl --user
daemon-reload`. A truncated-prefix drop-in is systemd's own documented mechanism: `man systemd.unit`
specifies that for a dashed unit name `foo-bar-baz.service` the directories `foo-bar-.service.d/`
and `foo-.service.d/` are searched too, which is why one file covers every uuid and every pid.
Verified on this machine (systemd 255) against a throwaway `systemd-run --user --scope
--unit=tmux-spawn-…` scope: it reported the drop-in's `MemoryMax` and named the file in
`DropInPaths`. **The cap is machine-wide for those two programs, not per pane** — the unit name is
all systemd gives us to match on, and nothing in it says which pane, or whether Relay was involved,
so a tmux the user starts outside Relay is capped too. Turning the option off removes exactly the
files Relay wrote: each carries a `# relay-managed: cap-escapees` marker line, a file without it is
never written over or deleted (it is reported in the status bar instead), and the `.d` directory
goes only if Relay's file was all that was in it. `src/EscapeeCaps.{h,cpp}`, tested headless against
a temporary config root (`tests/escapeecaps_test.cpp`).

Detection, once a second: an increase in the shell scope's `memory.events` `oom_kill` shows
"A command in this pane was stopped because it ran out of memory"; a dead shell PID or
`Result=oom-kill` shows a banner with Restart shell (Ctrl+Shift+R), which replaces the terminal
in the same pane. A stopped worker shows Restart agent. Failed scopes are `reset-failed`.
Scrollback is capped at 20,000 lines.

## 13a. Logs

Relay writes a rotating diagnostics log to `~/.local/share/relay/logs/` (`$XDG_DATA_HOME`):
`relay.log` from the window (`src/Logging.cpp`) and `worker.log` from every per-pane worker
(`backend/relay_core/logs.py`), 5 MiB x 3 backups, files `0600` in a `0700` directory. Before this
the process wrote only to stderr, which a desktop launcher throws away, so a stalled turn left
nothing to look at (issue `#SQAM`).

Both files share one line format, `<ISO-8601 UTC> <LEVEL> <logger> pane=<id> <event> key=value ...`,
and the same pane id, so the two sides of one pane line up. Workers share `worker.log`; each record
is written under an advisory lock on a hidden `.worker.log.lock` and a handler whose file was
rotated by another worker reopens it.

**Nothing about content is logged**: no prompts, model answers, reasoning, tool arguments, tool
output, file contents, terminal output, API keys or password-mode input. Identifiers, model and
host, event types, counts, durations and error types only; `scrub()` masks credential-shaped text
in every record as a second line of defence. There is still no telemetry: the files never leave the
machine.

**Which build this is** (owner, 2026-09-19: "where does relay say what build it is? put that in
settings", then "do 2026-09-10.14H.01 (where XXH is the 24-H time)"). `scripts/build-id.py` runs as
a `POST_BUILD` step of the `relay` target, so it numbers exactly the relinks of the app: the local
date, the 24-hour hour, and a count that starts again each hour — `2026-09-19.14H.01`. It keeps the
count in `.relay-build-seq.json` and writes the id to `relay.build-id`, both beside the binary; the
file is installed with the binary, and a binary without one falls back to its own file time and a
`.--` count. `relay::buildinfo::capture()` reads it once at start-up — the build *this process* is,
which cannot change under it — while `idOnDisk()` re-reads it, which is what a fresh launch would
get. Options > General > Diagnostics shows the first, with the version, the time the process
started and its path, and adds a line when the two differ: a rebuild never reaches a running Relay,
nor the windows it opens, because "New window" is in-process; quitting and reopening, or a launch
from the taskbar (which runs the desktop entry's `Exec`), starts the new binary. The id is also a
`build=` field on the `gui_start` log line.

Actions > Diagnostics has "Open log folder" and "Log detail" (`off | error | info | debug |
verbose`, setting `logging/level`, passed to workers as `RELAY_LOG_LEVEL`). **`verbose` also writes
prompt text** and is the only level that does; it is off by default and says so in the menu.

Related: Actions > Diagnostics > "Stop a silent model after..." sets `stall_timeout_s`, the idle
deadline that ends a turn whose model has gone quiet, and "Wait longer for the first token" sets
`first_token_timeout_s`, a longer budget for the first chunk alone — prefill on a large prompt is
not a stalled stream (docs/AGENT-SESSIONS-PROTOCOL.md section 15.1).

## 14. Theme

One file per theme, in TOML, is the single source of truth for colour: the UI tokens the
stylesheet is built from, the composer's syntax colours and the 16-colour ANSI terminal palette,
all in one place (issue `0JA7`).

| Piece | Where |
|---|---|
| Built-in themes | `data/theme/themes/*.toml` — `relay-dark`, `relay-light`, `solarized-dark`, `gruvbox-dark` |
| User themes | `~/.config/relay/themes/*.toml`; a file of the same id replaces the built-in one |
| Reader, token contract, discovery | `src/ThemeFile.{h,cpp}` (`relay-theme`, `tests/theme_test.cpp`) |
| Live palette, stylesheet, the switch | `src/Theme.{h,cpp}` |
| Picker | Options › Appearance (built in `src/RelayWindow.h`; the Options pane renders and searches it) |

A theme file has `[theme]` (name, variant `dark`/`light`, description), `[ui]`, `[syntax]`,
`[terminal]` (background, foreground, cursor and a 16-entry `palette`) and `[flags]`. Missing
tokens fall back to Relay Dark, so a short user theme still renders; unknown tables, keys and
flags are kept in `ThemeSpec::extra`/`flags` rather than rejected, so a later token or per-theme
boolean needs no format change.

**The tokens are variables, not constants.** `relay::theme::Background`, `Text`, `Accent`,
`Success`, `Warning`, `Error`, `Shell`, `Agent` and the `Syntax*` colours are assigned by
`setActiveTheme()`, so painting code that reads them at paint time (`ChromeButton`,
`SubagentsPanel`, `RequestsPanel`, the turn transcript, `InputHighlighter`) follows along.
`setActiveTheme()` rebuilds the `QPalette` and the stylesheet, regenerates the terminal schemes,
re-polishes every window and emits `theme::notifier()->themeChanged()`. Anything that *caches* a
colour — a per-widget stylesheet, a palette copied onto a label — has to move into the global
stylesheet or rebuild on that signal. Setting: `theme/name`.

`polishWindow()` still tags unnamed widgets by object name.

### What the colours mean

Chrome carries a theme's personality; **the meaning colours do not move**. Dark Copper says so in
its own header — "warning and error are Relay Dark's, unchanged" — while its accent, borders and
surfaces go copper, and IBM Beige takes Windows 95's navy the same way. A light theme cannot keep
the dark values, so it **derives** rather than inverts: same hue, luminance dropped until it clears
4.5:1 on the darkest ground it is painted on (the beige amber becomes ochre `#684800` at 4.65:1,
the tightest in the set).

There are three channels, and one meaning per colour:

| | Means | Where it shows |
|---|---|---|
| `shell` (cyan) | the terminal — as a **destination** and as **terminal work** | mode chip, caret, syntax, the `!` prefix chip, `Ink::User`, the Running glyph and its live dot, the Sessions band |
| `agent` (violet) | the agent — destination and agent work | the same list for the agent: the `*` prefix chip, `Ink::UserAgent`, the plan chip, Working/Subagents, every agent pane's band, a cleanup while it runs |
| `success` (green) | finished | the Done glyph, `**Done:**`, diff additions, the Options band |
| `warning` (amber) | **something is waiting on a person** | the NeedsYou glyph and tab icon, the question card (`Ink::Ask`), `**Need:**`, the notification, the work chip's attention state, a context or quota chip near its limit, the board's problems, the composer hint *only* while a program waits for input |
| `error` (red) | failed, or the pane is typing into another machine | the Failed glyph, `**Problem:**`, diff removals, the ssh band |
| `action` (red-orange) | the Actions pane | its band, glyph and title-bar button |
| `tool` (brass) | this pane is a tool | the Switchboard's band and its neighbours' |
| `link` (dark green) | **you can open this** | a path, folder, URL or `#card` in program output (at rest, since 2026-09-19), the hover underline and the keyboard walk, a fold's "open x.py" row, OSC 8 hyperlinks, the agent's Markdown links (ANSI 2), the composer's path token, `QPalette::Link` |

**Amber has one job.** Until 2026-09-19 it also drew a tool pane's header band, the `!` terminal
prefix chip and a Switchboard cleanup *while it ran* — none of which is waiting on anybody — so the
one signal that must never be missed was the busiest colour in the app. The band took `[ui] tool`
(brass: the same family, dulled, derived from each theme's own amber by `brassFrom()` when a file
is silent, so it inherits the amber's contrast); the cleanup went violet, because it is the agent
working; and the composer hint now wears the colour of the state it is describing. The `[syntax]`
palette is a separate language — a shell flag is amber there and means nothing about state.

**The prefix chips were wrong.** `! terminal` was amber and `* agent` was cyan: the chip landed in
`004a74f` on 2026-09-17, hours before `b473d45` defined `shell`/`agent` as the destination pair, and
nothing reconciled them. Fixed 2026-09-19 — they are destinations, so they wear the destination
pair like the mode chip, the caret and the syntax.

**Green means you can open it** (owner, 2026-09-19: "clickable things need to be understood from
colors", then "dark green, like Warp"). Before, a path in the output was plain until the pointer
found it, a filename in the agent's prose was amber (inline code), the composer painted one teal, a
fold row painted one in the engine's private blue, and the Sessions page painted a URL in the accent
— five colours for one fact. `[ui] link` is that fact's colour: a dark green, as dark as 4.5:1
allows on each theme's grounds (on a dark theme that is a mid green; a true forest green cannot
reach 4.5:1 on charcoal), held ΔE ≥ 20 from `success` and from ANSI 2 and 10 so "you can open it"
and "it finished" stay two colours. The engine's `ColorScheme::link` is set from it, so the hover
underline, the walk and every OSC 8 or fold link agree; `[syntax] path` is the same value in every
shipped theme; the Markdown renderer's link is `4;32` (ANSI 2, which `[ui] link` defaults to when a theme is silent —
and its inline code is bold with no hue, where it was amber: a filename in backticks is the commonest
openable thing in a reply, and amber both hid that and gave the "waiting on you" colour a second job —
derived by `linkFrom()` and lifted until it clears 4.5:1 on every ground, never inherited). In
program output the colour is painted **at rest** on every path, URL and card reference that
resolves (`TerminalView::setLinksColouredAtRest`, Options › Terminal › "Colour paths and links in
output", on by default). Two limits, both deliberate: only a cell whose ink is *plain* — the default
foreground or an achromatic one, which is what the agent's bright-white prose and a tool line's grey
are — is recoloured, so `git status` red and `ls` blue, which already say something, are left alone;
and never on the alternate screen, which a full-screen program owns. The colour is a fourth channel here as
everywhere: hover still underlines, and the walk still selects.

**The line you typed sits on a band, and the band follows the theme** (owner, 2026-09-19, after
Claude Code's grey band behind each prompt; then "use the 'relaying...' violet or cyan color as the
user-box highlight"; then "can we change the design that the background highlights shift with theme
changes"). The line carries a *role*, not a colour: `Pane::printInline` marks every row of a `User`
or `UserAgent` line with the private `OSC 7772;shell` / `OSC 7772;agent`, which the libvterm fork
keeps in the line's `relay_marks` beside the OSC 133 bits (`MarkUserShell`, `MarkUserAgent`), so it
scrolls into history and reflows with the line. `TerminalView::paintRow` fills a marked row across
the grid with `ColorScheme::userAgentBand` / `userShellBand` and paints every cell that brought no
colour of its own in the matching ink; `EngineBackend::applyThemeColors` sets those from the live
theme — the destination colour itself with `chipInk()` on it, the pair the prefix chips wear — and
runs again on `themeChanged()`. Nothing is rewritten: the same rows are repainted in the new
theme's colours, scrollback included. A shell command is echoed by the shell, not by Relay, so with
shell integration on its row (OSC 133;A) sits on `promptBand`, a tint of the shell colour, since
that row's ink is the shell's own PS1. Options › Terminal › "Band behind what you typed": channel,
the theme's raised surface, or none (no band, the destination colour as the ink). GhosttyCore
parses OSC 7772 too, though libghostty-vt itself ignores it: the SequenceScanner (which already
splits `feed()` for the OSC 133 events) hands the role to the adapter, which keeps every marked
row as a tracked grid ref — the same mechanism the selection anchor uses — and ORs the bits into
that row wherever the viewport or the paged history shows it, so the role scrolls, reflows and
trims with the line under Ghostty exactly as `relay_marks` do under libvterm.

**A tab owns its theme** (owner, 2026-09-19: "add an option, on by default, that themes are tab
specific … the theme that you have in the options menu is the default for when relay opens and new
tabs … you can see the theme visually in the tab picker … add a /theme command"). The tokens, the
`QPalette` and the stylesheet are the application's, so "per tab" means *the tab in front of the
window in front decides*: each tab page carries a `relayTheme` property, and
`RelayWindow::applyTabTheme()` calls `theme::setActiveTheme(id, persist = false)` on every tab
change and window activation. Two windows therefore never show two themes at once; the one you are
in wins. Options › Appearance › Theme is the **default** — `theme/name`, what Relay opens on and
what a new tab starts with (`theme::startupThemeId()`); choosing it there also rethemes the tab you
are in. `/light`, `/dark` and the new `/theme [name]` (a picker over every theme when bare) retheme
the current tab and, by default, become that default too — the owner's call: a command is as much a
choice as the picker. "/light, /dark and /theme also set the default" (`theme/commands_set_default`,
on) turns that off, and then a command changes one tab only. Either way the first such choice pins
the other tabs to what they were showing, so they do not follow. "Start each new tab on the next theme" (off by default)
walks the theme list instead of inheriting the default. A tab's own theme is saved with the layout
in the tab wrapper — `{"node", "theme"[, "project"]}`, only when it differs from the default
(`windowstate::tabTheme()`) — and each tab wears it as a swatch on the tab bar: the theme's terminal
ground over its accent, outlined in its own strong border, painted over the bar by
`paintTabSwatches()` from `theme::specFor(id)` without activating anything. With "Each tab keeps its
own theme" off, every choice is the application's and is stored, as before. What a tab has already
printed in 24-bit colour keeps the colours of the theme it was printed under, which per-tab themes
make rarer: a tab's scrollback now mostly lives under one theme.

**A hostname is one mark.** The pane header's ⇄ chip and the file preview's host chip are the same
chip since 2026-09-19 — the error hue's fill and near-solid line, the text colour for the name —
so "this is on another machine" reads the same whether it is your typing or a file that lives
there. The preview chip used to be the accent, which in Dark Copper is copper and means nothing.

**Colour is never the only channel.** Every pane state has its own silhouette (ring, the Relay mark,
a chevron, a tick, a filled disc with a cut cross, a diamond with an exclamation), so the state reads
in greyscale; only *live* states move, and they move by `pulseScale` — a scale, never an opacity, so
the ink keeps its contrast at every step — while news states stay still. The ssh band is held apart
from the Actions red-orange by strength, a hatch texture, a glyph and the host's name, not by hue
alone. That redundancy is why the warm end of the wheel can carry four meanings at once.

**The rules are tests, not taste** (`tests/theme_test.cpp`): every text token ≥ 4.5:1 on every
ground in every shipped theme; `action` at hue 12–30 and ≥ dE 20 from both `error` and `warning`;
`tool` never equal to `warning` and ≥ dE 10 from it; Dark Copper's structural copper at least twice
as dim as its warning, so a border can never read as a lit flag; and the destination pair always two
distinguishable colours.

**Scrollback cannot be recoloured.** Anything printed into the terminal keeps the colour it was
written in, so `Pane::printInline` uses 24-bit RGB and accepts the freeze — except `Ink::Ask`, which
is written with the *indexed* palette (bold yellow) because a question is the one piece of inline
output still actionable after a theme switch. `MarkdownAnsi` is indexed throughout for the same
reason. The rule: **inline output that stays actionable uses the indexed palette.** The second way out is a row *role*: what a line
is, kept in the line's marks and painted from the live scheme (the band behind what the user typed,
above) — the right tool when the meaning is "this row is X" rather than "this word is blue".

**The terminal.** Nothing is generated and nothing is checked in: `EngineBackend::applyThemeColors()`
reads the active `ThemeSpec` straight into the view, on `themeChanged()`, so a running pane
recolours in place. `data/theme/terminal.conf` holds what is not colour — font, line spacing,
margin, cursor — in one place. `[terminal] background_end` shades the ground from the top
colour to that one down the pane (`docs/THEMES.md`); without it the ground is flat. A theme file
*added* while Relay is running reaches new panes only after a restart ("Reload themes" says so).
Relay writes nothing to `~/.config` or `~/.local/share` for the terminal.

### Legible text

Owner, 2026-09-18: "some of the fonts seem hard to read, eg in subagent panes". What made them hard
was the same few things everywhere — 7–8pt labels, grey on a surface lighter than the one the grey
was chosen for, and italic, muted and monospace on the same line. These rules hold for every piece
of text a person reads, in every shipped theme; new UI follows them.

1. **Sizes.** Three, in `src/Theme.h`: `BodyPt` 10 (the application font; `applyTheme()` raises
   Qt's generic 9pt default to it and leaves a desktop's larger choice alone), `SecondaryPt` 9.5
   (a setting's detail, a notification's body, a card's meta line, a banner) and `FloorPt` 9
   (chips, key hints, counts, timestamps, engraved headings). Nothing is smaller — not in the
   stylesheet, and not in a `QPainter` label: take a derived font through `theme::legible()`.
   Stylesheet sizes are in `pt`, never `px`. Transcripts (the subagent tab, the program-running
   overlay, the turn log) are the terminal's mono face at 10pt.
2. **Contrast.** Text is at least 4.5:1 on the ground it is actually drawn on — `background`,
   `surface` and `surface_raised`, a band's tint, a chip's fill — in every theme, dark and light.
   `text_muted` is for genuinely secondary text and still clears 4.5:1. When a token fails on
   some surface, fix the token in that theme's file, not the widget. Read colours from the live
   tokens; a hard-coded Relay Dark value is under 2:1 on Relay Light's paper. Disabled controls
   are exempt (WCAG), a control that is merely *off* is not: an unchecked checkbox's label stays
   `@muted`. `tests/theme_test.cpp` checks the tokens for every shipped theme; the pane bands
   check their own label (`tests/panestatus_test.cpp`).
3. **One marker at a time.** A note is muted *or* carries a glyph — not both plus italic. Italic
   only where it means something (emphasis in Markdown), and then at the normal text colour; never
   italic + muted + monospace for anything a person has to read. Faint (SGR 2) and a fold's dim
   rows should fade toward the ground only as far as 4.5:1; the engine's view still draws them at
   60% alpha (2.9:1 for the muted grey on Relay Dark), which is the engine's to change.
4. **Case.** Engraved headings (Switchboard sections, Options groups) may stay in letter-spaced
   capitals because they are at or above the floor and one or two words long. A name — a pane's
   header band, a chip that says what something is — is in sentence case.

`tests/buttonfit_test.cpp` fails on any `font-size` in the application stylesheet under `FloorPt`
or in `px`.

## 15. Packaging layout

Installed tree (`CMakeLists.txt` `install()`):

| Path | Content |
|---|---|
| `bin/relay` | the application |
| `share/relay/backend/`, `share/relay/shell/` | worker, `relay_core`, Bash integration |
| `share/relay/scripts/` | `relay-open`, `relay-agent.py` |
| `share/relay/theme/` | `themes/*.toml` (the built-in colour themes), `terminal.conf`, icons used by the stylesheet |
| `share/applications/org.relayterminal.Relay.desktop` | desktop entry |
| `share/metainfo/org.relayterminal.Relay.metainfo.xml` | AppStream metadata |
| `share/icons/hicolor/…` | PNG and SVG icons |
| `share/doc/relay/` | `README.md`, `copyright` |

| Piece | File |
|---|---|
| `.deb` (CPack) | `packaging/cpack.cmake`; runtime deps `python3 (>= 3.10)`, `bash`; recommends `libsecret-tools`, `xdg-utils` |
| Per-distro build in a container | `packaging/deb/build-deb.sh` (Ubuntu 24.04 Qt5; Debian 13, Ubuntu 25.10/26.04 Qt6) |
| Install + smoke test | `packaging/deb/smoke-test.sh`, `packaging/smoke-installed.sh` (installed files, `--version`, worker `ready`, GUI start under Xvfb offscreen and xcb) |
| Local matrix | `packaging/deb/docker-build-all.sh` |
| Arch | `packaging/arch/relay-terminal/PKGBUILD` (release tarball), `relay-terminal-git` |
| CI | `.github/workflows/ci.yml`: Ubuntu 24.04 Qt5 build, ctest, install layout, desktop/AppStream validation; Debian 13 Qt6 build, tests and `.deb` |
| Release | `.github/workflows/release.yml` on `v*` tags: source tarball, 6 `.deb` jobs (3 distros × amd64/arm64) with smoke tests, `SHA256SUMS`, GitHub pre-release; AUR job present but disabled |
| Website | `site/` static page; `.github/workflows/pages.yml` runs only when `RELAY_PAGES_ENABLED=true` |

Version: `project(Relay VERSION …)` in `CMakeLists.txt` is passed to the app as `RELAY_VERSION`;
the worker reports `relay_core.__version__`, which `tests/test_version.py` checks against CMake.
Procedure: [RELEASING.md](RELEASING.md).

## 16. The terminal: `TerminalBackend` and Relay's engine

A pane never touches a terminal implementation directly. It holds a
`relay::TerminalBackend *` (`engine/TerminalBackend.h`): process control, input, inline
display writes, introspection (screen text, scrollback, alternate screen, title, cwd),
geometry and focus, clipboard, scrolling, search, and callbacks for links, title, cwd,
alternate screen, bell, OSC 133 prompt marks and exit. `capabilities()` says which of those
are real, so the pane only offers what its engine supports.

| Implementation | File | Notes |
|---|---|---|
| `relay::EngineBackend` | `src/EngineBackend.{h,cpp}` | Relay's own engine (`engine/`, [ENGINE.md](ENGINE.md)) — `relay::VTermBackend` plus the font and spacing from `data/theme/terminal.conf`, colour from the active theme, and the copy-on-select setting. Reports every capability |

KonsolePart was the other implementation, and the default, until the owner retired it on
2026-09-18 (commit "Retire KonsolePart"). The interface keeps its capability bits: a pane still
only offers what `capabilities()` reports, and two features degrade honestly without
`ScreenText` (section 9.1) — the input detection falls back to the `/proc` signals alone, and a
program cannot be handed to the agent.

`src/TerminalBackends.{h,cpp}` holds the settings and the right-click menu (unit-tested in
`tests/backends_test.cpp`); `src/BackendFactory.cpp` is the only file that constructs the
backend. What is still chosen is the emulator core underneath:

- `--engine-core=ghostty|libvterm`, or `RELAY_ENGINE_CORE` when the flag is absent
- restored sessions keep each pane's core (`"engine_core"` in the saved pane state); an
  `"engine"` key from a session saved before the retirement is ignored

`engine/` is always built and linked into `relay` (`RELAY_HAVE_ENGINE`), and
`relay-engine-tests` runs under `ctest`. `-DRELAY_BUILD_ENGINE=ON` adds the manual harness
(`relay-vterm-spike`) and the benchmark. Without a libghostty-vt prefix the engine builds only
the vendored libvterm core, which needs no Zig.

### Shell integration: OSC 7 and OSC 133

`shell/relay-integration.bash` (and `relay-integration.zsh`) emit OSC 7 for the working
directory and OSC 133 A/B/C/D for prompt, command and output boundaries with the exit code.
It is **opt-in**: source it from `~/.bashrc`, or turn on "Shell integration (OSC 7/133)" in
the palette (`terminal/shell_integration`), which sets `RELAY_SHELL_INTEGRATION=1` for new
panes so `shell/integration.bash` sources it last. The engine turns those marks into cwd
tracking and the palette's "Jump to previous/next prompt".

Over ssh (card #S5SH, [SSH-AND-MOSH.md](SSH-AND-MOSH.md)) two more shell files take part. With
`RELAY_SSH_WRAP=1` and `RELAY_SSH_DIR` set by the GUI, `shell/integration.bash` defines `ssh()` and
`mosh()` wrappers that add OpenSSH connection sharing (`ControlMaster=auto`, a `%C` socket in
`RELAY_SSH_DIR`, `ControlPersist=600`) unless the arguments or the user's own ssh config already
decide it, so the pane's agent can run commands over the user's login. `shell/remote-integration.sh`
is what the GUI types into the remote bash or zsh once per login, as one gzip+base64 line with a
leading space: OSC 7 with the remote hostname, OSC 133 A/B/C/D, history-ignore-space and the
`Ctrl+X Ctrl+P` redraw binding; on any other shell it does nothing. Inside a remote tmux or screen
it wraps every sequence it sends in that multiplexer's DCS passthrough, decided once at load, and
tells the user in one line when tmux lacks `set -g allow-passthrough on`. Both are tested in
`tests/test_ssh_shell.py`, in real bash and zsh on a pty, in a real tmux and over ssh.

Status and the remaining parity gaps: [ENGINE.md](ENGINE.md) and
`issues/features/needs_qa_llm/2026-09-17-engine-integration.md`.

## 17. Fragile dependencies and limits

Retiring KonsolePart (2026-09-18) removed this section's contents: Relay no longer depends on
any private Konsole API, KDE Frameworks Parts or a runtime plugin. What remains are the limits
of the platform and of the engine itself.

- Linux only: `/proc/<pid>/fd/0`, `/proc/<pid>/stat`, `/proc/<pid>/cmdline`, cgroup files,
  `systemd-run`. CMake refuses to build the app on other systems.
- Rich integration is Bash only. Zsh, Fish, SSH and tmux sessions work through native input.
- The GUI process holds every pane's screen and scrollback, so a crash in the engine takes
  down every pane. Per-pane process isolation is tracked separately.
- The window layer is one translation unit. `src/main.cpp` includes `src/Pane.h`, `src/RelayWindow.h`
  and the rest, and nothing else does; every class there defines its members in the class body. That
  is deliberate — splitting the file (2026-09-18) was about reading it, not about build times — but
  it means a one-line change to `Pane` still recompiles all ~13,000 lines. De-inlining `Pane` into a
  `Pane.cpp` is the separate, riskier job that would fix that.

## 18. Source map

| Path | Content |
|---|---|
| `src/main.cpp` | the app's includes, the `relay://` handler registration, the quit signal, `main()`. The window layer below is `#include`d from here and nowhere else, so it is still one translation unit — the headers hold classes whose members are all defined in the class body, and de-inlining them is a separate job |
| `src/AppPaths.h` | `dataRoot()` (where the backend, shell and scripts are) and `relayFuzzyScore()` (how the palette and the `@` picker rank rows), plus the `RELAY_VERSION` / `RELAY_DATA_DIR` / `RELAY_SOURCE_DIR` fallbacks |
| `src/Keymap.h` | every window-level shortcut as a named action: defaults, `keybindings.json` overrides, the presets, and the reload (section 12) |
| `src/Isolation.h` | per-pane systemd scopes: whether they are available, the memory limits, and what systemd says killed one (section 2) |
| `src/EscapeeCaps.{h,cpp}` | the opt-in, off-by-default cap on tmux and Chrome, which scope themselves out of their pane: the prefix drop-ins Relay writes and removes (section 13) |
| `src/CopyOnSelect.h` | copy on highlight: the one `terminal/copy_on_select` reading and the event filter every read-only text surface installs (section 4, "Copy on highlight"). Header-only, because those surfaces are spread across a dozen libraries |
| `src/Pane.h` | `Pane` — the terminal pane: its backend, Bash bridge, composer, queue, agent worker and conversation — and `QueueRowDelegate`, which draws the queue rows. `Pane` never names a window; it calls up through `std::function` callbacks |
| `src/PaneChrome.h` | `ToolPane` (explorer, preview, plan, transcript, Switchboard, settings) and `PaneChrome`, the button row and drag grip in a pane's corner |
| `src/WindowChrome.h` | `ChromeButton`, the painted header glyphs Relay draws instead of taking the desktop's title bar, and `NotificationsPopup`, the list behind the bell |
| `src/RelayWindow.h` | `ClosedItem` and `WindowManager` (the windows, what was closed, `relay open PATH`, the saved layout), then `RelayWindow` — the tab row that is the title bar, the splitter tree, the palette, and shortcut routing. The two share a header because they name each other inline |
| `src/WindowManagerImpl.h` | the `WindowManager` members that need the complete `RelayWindow`: opening and restoring windows, and reading and writing the saved layout. Included after `RelayWindow.h` |
| `src/RichEditor.*` | composer editor |
| `src/FilePanes.*` | explorer and preview widgets |
| `src/BoardModel.*`, `src/BoardPane.*`, `src/BoardWorker.*` | the Switchboard: card rows, tabs, columns, filters; the pane and card detail; the per-window Switchboard worker |
| `src/BoardWorkspace.*` | which project's Switchboard a pane is looking at: the walk up to `/`, trying every folder of `projects::boardFolders()` (`.switchboard/board.yaml`, `switchboard/board.yaml`, `issues/board.yaml`) at each level |
| `src/Projects.*` | which project a pane is in (`candidateFor`, a filesystem walk with no `git` subprocess), where its board folder is or would be, and the removable registry of known projects in `state/projects.json` |
| `src/Theme.*` | live tokens, palette, stylesheet, the theme switch |
| `src/ThemeFile.*` | the theme file format: reader, token contract, discovery, generated colour scheme |
| `src/Hints.*` | shortcut hint limits and idle tips |
| `src/SlashCommands.*` | what counts as an attempt at a `/command`, the closest real names, and the line printed for an unknown one |
| `src/OutputLinks.*` | which spans of terminal output are files, folders, URLs or `#K7Q2` card references, what they resolve to, and the keyboard cursor over them |
| `src/Notifications.*` | notification centre behind the header bell |
| `src/TurnTranscript.*` | turn details pane (tool calls, transcript) |
| `src/SkillsDialog.*` | skills list, exclude, refine, import, updates |
| `src/ModelSettings.*` | the API-keys and model-roles modals |
| `src/SettingsPane.*` | the Actions pane and the Options pane: one widget, two modes |
| `src/AgentUi.*` | pickers and instructions dialog |
| `src/Conversations.*` | the session manager pane (`/resume`, `/conversations`, Ctrl+Shift+Y) and the Ctrl+F find bar |
| `src/SessionInfo.*`, `src/PaneView.h` | the ⓘ conversation info pane (`/status`) and its painted button; the interface `ToolPane` hosts both through |
| `src/FileIndex.*` | the `@` picker's file listing: the asynchronous git chain, the changed set, the non-git walk |
| `src/Logging.*` | the GUI's rotating `relay.log` (section 13a) |
| `src/RuntimeDirs.*` | the private `$TMPDIR/relay-XXXXXX` directories: the pid+starttime owner mark, and the startup sweep of the ones a crash left behind (section 2) |
| `src/PaneTitles.*` | pane titles and the tab labels made from them: tidying a title, the offline "same work" rule, joining and shortening |
| `src/PaneStatus.*` | pane types (`paneType`, the band's tints by type or group), pane states and their urgency order, the host of an ssh/mosh/telnet session |
| `src/SshConfig.*` | SSH: the concrete hosts of `~/.ssh/config` and its Includes, the recent hosts, and the ssh/mosh command line that Split on the same host re-runs |
| `src/InputPolicy.*` | who may type where: the prompt-box-only rules, passwords, and whether the agent may type into the program |
| `src/ScreenPrompt.*` | the screen-text classifier: is the foreground program waiting for input, and for what (section 9.1) |
| `src/Aliases.*` | aliases (saved commands and prompts): the composer's `{{parameter}}` fields and Tab, re-reading the values out of an edited line, and whether a typed line names an alias |
| `src/Voice.*` | voice transcription: capture tool and arguments, the hold key, the transcript's place in the composer, WAV repair |
| `src/RemoteShare.*` | sharing a pane with a phone: the sidecar process, the pane's frames going out, the keys coming back, and the QR/approval dialog (section 19) |
| `remote/`, `rendezvous/`, `app/` | the remote protocol and its Noise handshake, the ciphertext-only relay, and the phone's web client (`docs/REMOTE-PROTOCOL.md`) |
| `shell/integration.bash`, `shell/event.py` | Bash bridge |
| `backend/worker.py` | worker protocol loop |
| `backend/relay_core/` | `router`, `provider`, `presets` (providers and the Main/Flash/Lite tiers), `agent`, `tools`, `queue`, `requests` (ledger, audit), `todos`, `context` (compaction), `keystore`, `keytest` (the keys modal's Test button), `keybindings`, `skills`, `roles` (model roles), `titles` (pane titles and tab labels), `voice` (transcription), `program_input` (the agent typing into the visible pane), `conv_index` (conversation index and search), `logs` (rotating `worker.log`), `board` (card format), `board_tools` (the `board_*` agent tools and their guardrails), `board_protocol` (the Switchboard messages), `aliases` and `alias_import` (saved commands and prompts, and importing Warp workflows and shell aliases) |
| `scripts/` | `build.sh`, `test.sh`, `relay-open`, `relay-agent.py` |
| `src/EngineBackend.*` | the `TerminalBackend` implementation over `engine/` |
| `src/TerminalBackends.*`, `src/BackendFactory.cpp` | per-pane engine selection and the factory |
| `shell/relay-integration.bash`, `.zsh` | opt-in OSC 7 / OSC 133 marks |
| `shell/remote-integration.sh` | OSC 7 / OSC 133 for a remote bash or zsh, typed in over ssh by the GUI |
| `engine/` | Relay's terminal engine: cores, PTY, session, view, `TerminalBackend.h` |
| `data/` | colour themes, `terminal.conf`, icons |
| `packaging/`, `.github/workflows/`, `site/` | packages, CI, release, website |
| `tests/` | Python backend and PTY tests, Qt editor and file pane tests |
| `issues/` | file-based tracker, and the Switchboard's storage (`board.yaml`, cards, `threads/`) |

## 19. Sharing a pane with a phone

The share chip sits beside the microphone in the composer strip, and the palette action is
"Share this pane with a phone" (`pane.share`). Both call `Pane::toggleShare`.

The protocol, the cryptography and the phone's web client live in a Python sidecar,
`remote/gui_host.py`, started on demand and spoken to in line JSON exactly as the agent worker is
(`src/RemoteShare.cpp`). Relay links no crypto library, and the sidecar touches no widget.

| Direction | What crosses |
|---|---|
| GUI → sidecar | the pane's title, cwd and status; a screen frame whenever the view pulls one; each worker event; the answer to a pairing question; the transcript of a voice clip; a page of the pane's scrollback |
| sidecar → GUI | the pairing URL and its QR matrix; a pairing request to put to the person; the keystrokes a phone sent; a prompt a phone submitted; a voice clip to transcribe; a request for a page of scrollback |

**One prompt box.** A client has a single box, like Relay's own. What is typed arrives as
`compose`, and `Pane::submitRemote` routes it through the worker's router exactly as the composer
does — a command runs in the shell, anything else goes to the agent. The agent's reply needs no
channel of its own: Relay prints it into the pane's terminal (section 8), so it reaches the phone
through the screen stream that is already running. A client whose desktop sends no screen — the
agent companion — renders the reply itself instead. A remote prompt never touches the desktop's
composer, because the person at the keyboard may be mid-draft.

**(security)** Routing can reach the shell, so a client may only ask for it when its device was
allowed to type. A device paired for the agent gets `submitAgent` and nothing else; a device paired
for viewing cannot compose at all.

**Voice from the phone.** The phone records a clip and sends it in; the pane hands it to the same
worker `transcribe` request the microphone beside the prompt box uses, and the words go back to the
device that spoke (`Pane::transcribeForRemote`). The API key never leaves the desktop and the phone
never talks to a transcription provider — that is the whole reason the audio travels rather than
the key. The transcript is routed by the request id the clip carried, so it can no more land in the
desktop's composer than a remote prompt can, and the clip itself is unlinked as soon as the worker
has answered.

**Scrollback on the phone.** A client that drags the terminal down asks for a page by absolute
row, and `RemoteShare::sendHistoryPage` answers it from the pane's own core through
`VtCore::historyLines`, which is const: reading back must not move the viewport, because that is
the screen the person at the keyboard is looking at. The rows go out through the same
`screenjson::rowOf()` the live frames use, so a history row and a live row cannot end up
different shapes, and the page is capped so a phone pages rather than downloads the buffer.

Two rules decide the shape:

- **Screen state comes from the frame `TerminalView` already pulled** (`TerminalView::frame()`,
  `frameChanged()`). `VtCore::updateFrame` consumes the dirty state, so a second caller would stop
  the pane repainting. Only a pane with a frame can be shared, which since the engine became the
  only terminal is every pane.
- **Approving a device is a deliberate click**, and viewing and typing are separate grants. The
  dialog shows a five-digit code derived from the Noise handshake on both ends, and *Refuse* holds
  the focus, because allowing typing hands a phone the keyboard of a live shell.

**Inviting another person, and the Sharing pane** (`#W5N2`, protocol section 10). Pairing your own
phone is a moment and stays a dialog; being host to somebody else is not, so it is a pane. The
share window gains "Invite someone to this pane" — role (Viewer or Editor), expiry, uses, the link
as a read-only field with a Copy button and its QR, and one sentence saying what the role allows.
Everything after that lives in `src/SharingPane.{h,cpp}` (library `relay-sharing`), a `ToolPane` of
kind `Sharing` whose `paneType` is `sharing`:

- `relay::sharing::Model` holds no widgets. It reads the sidecar's section-10.5 lines — who is on
  each pane, which invites are live, what is waiting for the owner, who is driving — and expires a
  waiting request on the hub's own clock (2 min for a knock, 60 s for the keyboard, 10 min for a
  prompt). `tests/sharingpane_test.cpp` drives it without a hub and without a display.
- `SharingView` renders it and calls back; `RemoteShare` is the only place a line is ever sent.
- Refuse is first and holds the focus on every question, the editor button is absent on a
  viewer-only invite (admitting may lower a role, never raise it), and a guest's prompt is shown
  whole and wrapped, because approving it is approving exactly that text. There is no "approve
  always" in v1, by the protocol's own argument (section 10.5).

The pane opens from the share chip once a pane is shared, from the palette (`pane.sharing`), and by
itself when somebody knocks. **Opening it never takes the keyboard**: `RelayWindow::openSharingPane`
puts the focused widget back, now and again after the layout has run, because the next keystroke
would otherwise land on Admit. The bell carries the same news through `Pane::notifyFromWindow`,
which is the ordinary notification path — the desktop only hears about it while Relay is not the
window you are looking at.

While a pane is shared its header says so: "phone", "2 guests", or — when somebody else holds the
keyboard — "alice is typing" in the remote session's own hue, with the hatched band across the
title row, because it means the same thing as an ssh session does. Typing in such a pane takes
control straight back (`control_take`), from the two places that already do this for the agent:
`Pane::setNative()`, beside the `endDelegation()` that ends the agent's turn at the keyboard
(section 9, card #C1HH), and the key filter, which passes the keystroke on untouched.

Everything else — pairing, capabilities, the password-prompt refusal, revocation — is the protocol's
job and is described in [REMOTE-PROTOCOL.md](REMOTE-PROTOCOL.md).

# #2FQ9 — an artifact's agent popped out into a linked shell pane, and docked back

Implementer evidence (anthropic/claude-opus-5-5 via claude-code), 2026-09-26. Built from exactly the
tree `land.py` would land (tip + this card's hunks) in a verify slot, run under Xvfb against an
isolated HOME / XDG_* / runtime dir (`drive.sh`). The tab's agent in the sandbox resolved to the
Claude Code guest (`claude-sonnet-5`) rather than `stub-provider.py`, so the turns below are a real
model's; the stub stays as the configured endpoint for a run with no guest installed.

## Tests

- `ctest -R '^linkedshell$'` (new; `relay-consolemode-tests --linkedshell-only`): **passed** in the
  verify slot. Cases: a linked shell runs on the console's own surface and shared worker; `!` and
  Terminal mode go to the pty and never to the context, Enter still goes to the context; off stops the
  pty and keeps the transcript and re-locks the line to the agent; a second link restarts the engine
  session on the same surface; `exit` asks to dock back; a host fold does not hide a linked console
  and is applied on dock-back; a card-style hidden, unused transcript is shown while linked; a terminal
  pane is never linked.
- `ctest -R '^relay-engine-tests$'`: **passed** (TerminalSession restart, `stopProgram`).
- `ctest -R '^consolemode$'`: 11 failures, **identical** on `main` + nothing of this card (lines
  768/769/781, 1920-1927, 2049-2055) — pre-existing, filed as
  `.board/changes/2026-09-25-consolemode-suite-red-at-head-after-2m26-and-pbz4.md` and
  `…-consolemode-default-suite-ctrl-click-opens-the-e.md`.

## Live drive (screenshots in order)

| Shot | What it shows |
|---|---|
| `01-card-open.png` | Card #F9A1's page: "⤴ Ag…hell" (Agent + shell) beside the existing "⤴ O…ane" (Own pane). |
| `02-first-turn-docked.png` | First card turn, docked: `MARK-ALPHA what does this card need?`. |
| `03-card-agent-popped-out.png` | After "Agent + shell": the same console, transcript and all, in a leaf beside the Board with a shell prompt under it; the card page holds the placeholder (Show · Dock back) and its button reads "Dock agent". |
| `04-linked-shell-git-status.png` | `!git status` ran in the linked shell (`~/proj`, branch master). |
| `05-followup-while-linked.png` | Follow-up while linked: the agent names MARK-ALPHA from the docked turn **and** what git status showed; the card thread on the left records the owner ask and the agent's Discuss reply. |
| `06-docked-back.png` | Dock back from the placeholder: console back on the card page, transcript intact, button back to "Agent + shell". |
| `07-followup-after-dock.png` | Follow-up after docking: the agent lists `MARK-ALPHA, MARK-BETA, MARK-GAMMA`. |
| `08`–`09` | `notes.md` opened; its agent's head row has "⤴ Shell" beside fold. |
| `10-file-agent-popped-out.png` | The file agent popped out: its leaf ("✦ notes.md agent · linked shell"), the host's head row now "⤵ Dock", placeholder in the dock. |
| `11`–`12` | `!ls` in the linked shell, then `!exit`: the agent docked back under the file by itself, transcript kept. |
| `21-palette-search.png` → `22-palette-pops-card-agent.png` | Ctrl+/ palette lists "Pop this artifact's agent out into a linked shell pane, or dock it back"; Enter pops the card's agent out (bar "#F9A1", Dock back visible, chip `auto`). |
| `23-close-linked-pane-docks-back.png` | × on the linked pane docks the agent instead of closing it. |
| `30-fresh-card-popped-shell-visible.png` | A card with no conversation yet (its transcript hidden until used): the linked shell is visible. |
| `31-restored-after-restart.png` | Relay stopped with the agent out and relaunched (layout restore): the card page and its linked shell pane came back linked. `windows.json` had `"agent_linked": true` on the host leaf. |
| `32-closing-host-takes-agent.png` | Closing the Board pane with its agent out closes both; Relay keeps running. |
| `33`–`34` | A second tab with a linked agent, closed by its tab ×: clean, Relay alive, no `gui_crash` in `relay.log`. |

The sandbox card's thread after the three card turns (docked → linked → docked), all one conversation
(`turn=7ce03cd65cbf9e852e9eefe63d1f79e3/…`):

```
<!-- relay:entry 20260926T040438Z-6x author=owner kind=comment mode=discuss -->
MARK-ALPHA what does this card need?
<!-- relay:entry 20260926T040445Z-fa author=agent kind=comment mode=discuss model=claude-sonnet-5 turn=7ce03cd65cbf9e852e9eefe63d1f79e3/19c300073879465db38e2fc6db2a14f5 -->
This card is thin — … (docked)
<!-- relay:entry 20260926T040526Z-ny author=owner kind=comment mode=discuss -->
MARK-BETA In one line: which MARK- word did I send in my first message, and what did git status just show?
<!-- relay:entry 20260926T040530Z-yv author=agent kind=comment mode=discuss model=claude-sonnet-5 turn=7ce03cd65cbf9e852e9eefe63d1f79e3/143ecdb6194d46e1947945e0eafc34b9 -->
MARK-ALPHA was the word in your first message; git status showed one untracked entry, `.board/threads/`, on branch `master`.   (linked)
<!-- relay:entry 20260926T040604Z-09 author=owner kind=comment mode=discuss -->
MARK-GAMMA In one line: list every MARK- word I have sent in this conversation, in order.
<!-- relay:entry 20260926T040607Z-1h author=agent kind=comment mode=discuss model=claude-sonnet-5 turn=7ce03cd65cbf9e852e9eefe63d1f79e3/72dddb7e9cee4370a21bdc042e7f12f5 -->
MARK-ALPHA, MARK-BETA, MARK-GAMMA.   (docked again)
```

## Found by the drive and fixed before landing

The mode chip read "agent" after linking; the shell's first line ran onto the transcript's last line;
the bar's Dock back hid under the pane's corner buttons; the bar named the Board rather than the card;
a card with no conversation yet showed no shell (its transcript was still hidden). Each is fixed, and
the last has a test (`aLinkedShellShowsAnUnusedTranscript`).

## Incident

On the first take, `relay-drive` and `relay-open` reached the owner's own running Relay, because the
shell inherited `RELAY_OPEN_SOCKET` and those scripts read it before `XDG_RUNTIME_DIR`. Card #2FQ9's
thread lists what was sent. `drive.sh` now unsets the variable, and every later driver call named the
sandbox socket.

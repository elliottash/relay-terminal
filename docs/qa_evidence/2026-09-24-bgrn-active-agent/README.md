# #BGRN increment: an active agent moves to the background

Request (user, 2026-09-24): "make it where, if an agent is active, if you click run in
background opr ctrl alt enter, it puts it in the background"

## What changed

- `Pane::agentActive()` (`src/Pane.h`): a native Relay turn (`m_agentBusy`), a guest CLI
  turn (`m_guestBusy`) or live subagents (`m_subagents.liveCount() > 0`) all count as
  "an agent is active in this pane". Same predicate `dimmingAgentBusy()` already used.
- `runBackgroundPane` (`src/RelayWindow.h`): checks `agentActive()` *before* agent
  readiness and the draft check, so the button and Ctrl+Alt+Enter hand the live pane to
  `moveBackgroundPane` — a guest pane no longer needs a configured native agent, and no
  typed task is required or submitted. The composer draft is left untouched.
- `moveBackgroundPane`: accepts any active agent, not only a native busy turn. The
  live `backgroundTaskState()` guard ("Answer the agent's question before moving it to
  background") already covers guest questions, since it resolves facts fresh.
- Descriptions: `src/Keymap.h` ("Run in background: send the prompt, or move a live
  agent there") and the Actions list detail in `src/RelayWindow.cpp`.

## Build and tests (2026-09-24, this checkout)

- `scripts/relay-build` → `Built target relay` (full app binary with the change).
- `ctest -R "^(actionpalette|keymap|panestatus)$"` → 3/3 passed.
- `relay-consolemode-tests` fails to **link** in the shared working tree from another
  session's half-landed `panedir` code in `src/PaneRuntime.cpp` (undefined
  `relay::panedir::Directory::instance()`); not touched by this change, and the failure
  is absent from a tree of tip+these-paths (land.py's verify build of `relay` passed).

## Manual check for the owner (what "done" looks like)

1. Start a guest agent turn in a pane (e.g. `claude` doing work) — click the composer
   strip's ↗ Run in background, or press Ctrl+Alt+Enter: the pane leaves the layout and
   appears in the background tasks list, still "working"; nothing is submitted from the
   composer (a draft you typed stays there when you reopen the pane).
2. Same with a native Relay agent mid-turn (was already the behaviour) and with live
   subagents while the main turn is idle.
3. Idle agent + empty composer still says "Type a task before running in background.";
   idle agent + typed task still sends it as a background task.
4. An agent asking a question (native or guest permission prompt) still refuses with
   "Answer the agent's question before moving it to background."

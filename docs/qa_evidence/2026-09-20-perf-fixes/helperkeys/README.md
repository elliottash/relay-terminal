# The Actions-pane helper may rebind keys (#GMCF)

The owner, on the third open question of #GMCF (2026-09-20): **"3 yes"** — the helper's `configure`
carries the keybindings, so it may rebind keys.

Until now the tab helper's `configure` (`RelayWindow::startBoardWorker`) sent the `app` block and
no `keybindings` block. Two things followed from that, and neither was visible from the code that
promised otherwise:

* `app_action_list` answered the helper **without any keys**. Since the `set_keybinding` schema
  stopped carrying the 91-action listing (decision 1, `../keybind/README.md`), that tool result is
  where an action's current shortcut is found — and the Actions pane's brief
  (`board_chat.PANE_BRIEFS["actions"]`) promises the palette "with its keyboard shortcut beside
  it". The helper had nothing to put beside it.
* `set_keybinding` did not exist for the helper at all: the tool is offered only while the worker
  holds a catalogue.

## What changed

| | |
|---|---|
| `src/RelayWindow.h` | `startBoardWorker` inserts `keybindings: Keymap::instance().catalog()` — the pane's builder, not a second copy — and a new `sendHelperKeybindings()` re-sends it to every configured helper from the `Keymap` reload listener, beside the panes' `sendKeybindings()`. |
| `backend/relay_core/board_protocol.py` | `_build_page_agent` passes the worker's catalogue to the helper `Agent`, and `set_keybindings()` replaces it on the live helper when a reload arrives. |
| `backend/worker.py` | the `keybindings` message calls that, so the reload reaches both agents this worker runs. |
| `backend/relay_core/board_tools.py` | `ChatScope` allows `set_keybinding` (`CHAT_APP_TOOLS`). `CardScope` — a card's Discuss or Plan — still refuses it. |
| `backend/relay_core/agent.py` | the scope branch of `tools()` appends it, last, for `TAIL_TOOLS`' reason. |
| `backend/relay_core/board_chat.py` | the Actions brief says the tool is there and what it costs the person. |

The reload path is the pane's, unchanged: `set_keybinding` writes `keybindings.json` atomically,
`Keymap`'s watch on the file **and its directory** fires, `Keymap::reload()` re-reads it and calls
its listeners — which is what re-binds the live shortcuts, refreshes the panes and now re-sends
the catalogue to the helpers.

## What it costs the helper's request

`set_keybinding` is the tool decision 1 slimmed from 9,960 B to 825 B, so handing it to the helper
costs a fifth of what it would have in the morning:

```
actions in the catalogue: 92
helper tool list, no keybindings block: 23 tools   20291 B  ~5072 tok
helper tool list, with it:              24 tools   21087 B  ~5271 tok
difference: +796 B / ~+199 tok  (set_keybinding)
```

(`measure.py` beside this file.) The schema does not move when a key is rebound — that is what
`tests/test_keybindings.py` pins — so a rebind costs the helper's prompt cache nothing.

## Live, under Xvfb

```
docs/qa_evidence/2026-09-20-perf-fixes/helperkeys/drive.sh [relay-binary] [out-dir]
```

A real Relay on an isolated `HOME`/`XDG_*`/`TMPDIR` under a short path, `RELAY_KEYRING=off`, **no
provider key of any kind**: the one preset is `stub-provider.py` on a local port, which plays the
turn the decision is about (`app_action_list`, then `set_keybinding`) and dumps every request it
was sent. F1 opens the Actions pane, Alt+Q opens the helper panel on it, and the prompt typed into
the helper's composer is the only thing typed anywhere.

```
PASS the Actions pane is open ("actions" in 01-actions-pane.png)
PASS the helper panel opened on it (Alt+Q) ("helper" in 02-helper-panel.png)
PASS the helper's request carries set_keybinding (stub dump)
PASS the helper's request carries the app tools (stub dump)
PASS app_action_list answered with pane.close on its current key, Ctrl+W (stub dump)
PASS the rebind is in keybindings.json ({'pane.close': ['Ctrl+Alt+Shift+K']})
PASS the running app reloaded the keys ("shortcuts reloaded" in 04-notice.png)
PASS the helper said what it had done (05-answered.png)
PASS the Actions pane shows Close pane on the new key (06-actions-row.png)

9 passed, 0 failed
```

`notes.txt` is that run's own file. The shots kept here are `02-helper-panel.png` (the panel over
the Actions pane), `04-notice.png` (the app saying it reloaded), `05-answered.png` (the answer) and
`06-actions-row.png` (**the app's own idea of the shortcut**: the Actions row for Close pane, on
the new key, after the helper moved it).

## Tests

```
PYTHONPATH=$PWD/backend python3 -m unittest tests.test_board_chat.HelperKeybindingTest
ctest --test-dir build -R boardworkspace
```

* `tests/test_board_chat.HelperKeybindingTest` — the helper is built on the worker's own
  catalogue; its turn offers `set_keybinding` **last** and a card turn does not (and its scope
  refuses it); a helper with no catalogue is offered nothing to rebind; `app_action_list` from the
  helper carries the keys, including an action that has only a shortcut and no palette row; a
  `keybindings` reload reaches the live helper agent.
* `tests/boardworkspace_test.cpp::theHelpersConfigureCarriesTheKeybindings` — the GUI half, read
  as source text the way the rest of that file reads `RelayWindow.h`: the block is on the helper's
  `configure`, it comes from `Keymap::instance().catalog()` rather than a second copy,
  `sendHelperKeybindings()` exists, guards on `worker->configured()` and is called from the reload
  listener.

## Left for the owner

Nothing of this decision. One thing found next door: on `main`,
`tests/test_board_chat.BoardlessHelperTest::test_the_builder_makes_a_board_less_agent_that_keeps_the_app_tools`
fails — a helper in a tab with **no project attached** has no board, so it takes the ordinary pane
branch of `Agent.tools()`, where #GMCF decision 9 now defers the app tools behind `load_tools`.
It is reachable, one round trip later; whether the helper — the agent those tools exist for —
should defer them is decision 9's call, not this one's, so it is reported rather than changed.

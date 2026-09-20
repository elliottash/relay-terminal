# `set_keybinding` stops carrying the keybinding catalogue (#GMCF decision 1)

The owner approved decision 1 of the distillation proposal
(`../prompt-distillation/PROPOSAL.md`, sections 1.3, 2.7 and 6): drop the 91-action listing and
the 91-entry enum from the tool schema, let `app_action_list` be where an id is found, and make
the refusal for an unknown id name the closest ids.

## Before and after, on the real thing

Numbers from `measure.py` against a **captured `configure`** — the blocks a real window sends,
not a fixture (recipe at the bottom): 92 bindable actions, 49 of them bound; 137 option rows and
119 palette actions in the `app` block.

| | before | after |
|---|---:|---:|
| `set_keybinding` schema | **9,960 B / ~2,486 tok** | **825 B / ~207 tok** |
| whole tool list, pane without a Switchboard (24 tools) | 25,896 B / ~6,464 tok | **16,761 B / ~4,185 tok** (−35 %) |
| whole tool list, pane with a Switchboard (34 tools) | 39,084 B / ~9,771 tok | **29,949 B / ~7,487 tok** (−23 %) |

−9,135 bytes / ~2,279 tokens on **every** model call of every step of every pane turn. And the
schema no longer moves: rebinding a key used to rewrite the largest tool in the list, which
invalidates the provider's prompt cache and, on the Local tier, re-prefills the whole prefix
(PROPOSAL §1.4). `tests/test_keybindings.py` pins that the tool list is byte-identical across a
`keybindings` reload.

The board row is `promptsize.py`'s (`../prompt/promptsize.py`, which now builds the keybinding
catalogue and a real `app` block — before this it could not see this tool at all).

## What replaces the listing

The schema is four fixed lines, the same for every user, and `action` is a plain string. An id is
found two ways:

1. **`app_action_list`** answers from the keybinding catalogue as well as the palette: each row
   that is a bindable action carries its current `keys`, and the 31 registry entries with no
   palette row (the focus moves, the window cycle, `help.shortcuts`, `palette.open`…) are listed
   under section `Shortcuts`. Without this the tool would have been undiscoverable for a third of
   the registry — the palette does not contain it.

   ```
   app_action_list {"search": "focus pane"} →
     pane.focusLeft ['Alt+Left'], pane.focusRight ['Alt+Right'],
     pane.focusUp ['Alt+Up'], pane.focusDown ['Alt+Down']
   app_action_list {} → 60 of 148 rows (a third of the cap kept for the shortcut-only rows),
     and `note` says a search will show the rest
   ```

2. **The refusal**, for a model that guesses (`KeybindingCatalog.suggest`, difflib over the id,
   over the part after the dot, then over the descriptions):

   ```
   pane.split_right → Relay has no action 'pane.split_right'. Did you mean pane.splitRight,
                      pane.splitLeft, pane.splitUp, pane.focusRight, pane.moveRight?
                      app_action_list gives every action id with its current keys.
   focusleft        → Did you mean pane.focusLeft, pane.focusRight, pane.focusUp, …
   close the pane   → Did you mean agent.subagentPane, pane.close, agent.screenshotPane, …
   ```

Validation is unchanged and still server-side: `prepare` rejects an id that is not in the
catalogue exactly as the enum did (`tests/test_keybindings.py::test_the_action_is_still_checked_worker_side`).

No sibling tool embeds a per-user catalogue in its schema: every other `enum` in
`backend/relay_core/` is a module constant (`MODES`, `EFFORTS`, `CARD_TYPES`, `KEYS`,
`OPEN_TARGETS`, board statuses). The `agent` tool's subagent types are the user's, but capped,
and PROPOSAL §2.7 keeps them.

## Tests

* `tests/test_keybindings.py` — the schema carries no catalog and does not move when a key is
  rebound; the refusal names the closest ids, at the catalogue level and as the tool result the
  model receives; the worker-side check still refuses an unknown id; end-to-end, a stub provider
  calling the tool still writes `keybindings.json`
  (`test_agent_sets_keybinding`, `test_configure_with_catalog_and_tool_call_writes_file`).
* `tests/test_app_tools.py` — `app_action_list` carries the keys, lists the actions only a
  shortcut has, finds them by search, and follows a rebind.
* `tests/test_system_prompt.py::SizeTests` — the fixture is now a pane (keybindings + a real
  `app` block), the tool budget is 17.5 KB, and no schema may name more than one action id or any
  current key.

```
PYTHONPATH=$PWD/backend python3 -m unittest tests.test_keybindings tests.test_app_tools
PYTHONPATH=$PWD/backend python3 -m unittest tests.test_system_prompt.SizeTests
python3 docs/qa_evidence/2026-09-20-perf-fixes/keybind/measure.py [--configure <capture>]
```

## How the `configure` was captured

`RELAY_DATA_DIR` picks the backend Relay launches (`src/AppPaths.h`), so a directory whose
`backend/worker.py` is a tee in front of the real one records everything the GUI sends and
changes nothing:

```
mkdir -p $C/data/backend && ln -s <repo>/{shell,data} $C/data/ \
    && ln -s <repo>/backend/relay_core $C/data/backend/
# $C/data/backend/worker.py: read stdin, json.dump each line into $RELAY_CAPTURE,
#   write it on to `subprocess.Popen([sys.executable, "-S", "-u", "<repo>/backend/worker.py"])`
Xvfb :90 -screen 0 1600x1000x24 &
env -i HOME=$C/home PATH=/usr/bin:/bin DISPLAY=:90 XDG_*=$C/x/* TMPDIR=$C/tmp \
    RELAY_KEYRING=off RELAY_DATA_DIR=$C/data RELAY_CAPTURE=$C/out \
    ./build/relay --clean-shell --fresh
```

The capture itself is not committed: it is 72 KB of a throwaway profile's own settings. Nothing
was typed into the pane and no model was called.

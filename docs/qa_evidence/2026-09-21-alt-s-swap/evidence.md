# Alt+S model swap — implementer evidence

Implementation: `f8b21300a457b2819e26fb401b9a6f3b593f21d9`.

- The exact commit tree compiled successfully through `scripts/land.py`'s build gate and again via `scripts/relay-build --target relay` on a clean export of f8b21300. The shared checkout's first build failed in another session's ongoing Ink/theme refactor; none of those changes was included in this commit.
- `ctest --test-dir build -R '^modelcatalog$' --output-on-failure`: passed, including the existing swap-target, remembered-model, unavailable-model and empty-catalog cases.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_keybindings.py -q`: 23 tests passed. This includes validation of all GUI action IDs and default key spellings. Used unittest because pytest is not installed.
- The exact-tree Relay binary was exercised under Xvfb with isolated XDG config/data/cache/runtime directories. No prompt was submitted to a model. Typed a draft, pressed Alt+S, pressed Alt+S again, and then typed /swap. Screenshots record the composer and model box at each step.

The hotkey calls the same `Pane::swapModel()` operation as the slash command, including its existing busy-turn and model-availability behavior. It never clears or submits the composer. The slash path queues the live keymap hint under `model.swap.key`.

`tests_check` has no blocking findings. Its filename-based orphan warning does not recognize the relationship between Keymap/Pane and the existing modelcatalog/keybindings tests; no tests were changed.

Board format validation reports existing errors and warnings elsewhere in the board; none names S7WP.

Live result: before.png shows Claude Fable 5.1; swap.png shows Claude Opus 5; back.png returns to Claude Fable 5.1. All three retain “draft stays here”. slash.png records the typed /swap result after its toast faded. The isolated settings recorded one showing of model.swap.key, confirming the shortcut hint was displayed.

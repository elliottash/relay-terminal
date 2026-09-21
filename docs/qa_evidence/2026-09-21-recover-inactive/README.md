# Recovery verification — #GSJ7 and #Z00M

The owner authorized landing these inactive changes on 2026-09-21. Both were present in the shared working tree and active binary but absent from main. Only main and its original worktree exist; there are no extra local branches, worktrees or stashes to remove.

## Automated checks

- `RELAY_SESSION=recover-inactive scripts/relay-build --target relay relay-panes-tests relay-hints-tests relay-settings-tests` — passed.
- `ctest --test-dir build -R '^(panes|hints|settings)$' --output-on-failure` — 3/3 passed.
- `RELAY_ENGINE_TEST=ViewTest QT_QPA_PLATFORM=offscreen build/engine/relay-engine-tests ctrlZoomKeysAndWheel` — passed (3 Qt test results).
- Commit `eeced0c917c529e3e7c8749aa99df3ab67302b22` passed land.py's exact proposed-tree build gate. The active build wrapper passed again after landing.

Both cards passed `tests_check` after landing with no findings or open blocks. The board-wide validator still reports three pre-existing errors on #MDL1, outside this recovery.

## Live GUI

Isolated Xvfb session, 1400×900 window, isolated XDG configuration/data/cache/runtime, existing HOME unchanged. No agent request was sent. The shell UI remains usable despite guest-worker exit banners in this isolated profile.

- `01-split.png`: two initially equal panes.
- `02-dragged.png`: divider dragged to unequal widths; the toast teaches Alt+0. The focus-click hint was pre-exhausted in this profile so it could not consume the global hint cooldown.
- `03-equalized.png`: Alt+0 visibly restores equal widths. The layout JSON can retain the prior sizes until the next scheduled save; visual divider position is the verification here.
- `04-shortcut.png`: Actions search for “auto resize” finds Equalize pane sizes with Alt+0.
- `05-font-setting.png`: Options search finds the saved 18 pt font size.
- `06-font-changed.png`: editing the control changes the setting to 22 pt and updates open terminals.

The original font-size implementation's live-change, Ctrl+0 reset and restart checks are preserved separately in [its evidence](../2026-09-21-terminal-font-size/README.md).

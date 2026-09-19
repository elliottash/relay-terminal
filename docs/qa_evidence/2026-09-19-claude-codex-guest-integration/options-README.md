# Options › Guests — Xvfb evidence (GT7X, 2026-09-19)

`options-guests-drive.py` drives the Options › Guests section on a live `relay` under `xvfb-run`,
with the isolation the channel harness uses (fresh HOME/XDG_*/TMPDIR, `RELAY_DATA_DIR` pinned to
this worktree) plus the two fixtures the section's promises need: a project whose
`.claude/settings.json` already holds a hook of the user's own, and a `~/.codex/config.toml`
holding the user's own `notify`. Rows are driven by keyboard — the Options search box (type,
Enter) and the pane's own Left/Right tab keys — and every step checks both the screen (OCR of the
picture taken) and the file the row claims to touch.

Run: `xvfb-run -a python3 options-guests-drive.py` (from this directory; the binary is
`../../../build/relay`). The run's own log is the verification: every check prints PASS or FAIL,
and the exit status is non-zero on any FAIL.

## Pictures and what each is evidence of

| Picture | Evidence |
| --- | --- |
| `implementer-options-01-guests-section.png` | The section renders: the three rows (Claude Code in this project, Also in ~/.claude/settings.json, Codex notifications) are on screen after arriving at the tab. |
| `implementer-options-02-global-guarded.png` | The global row while the project row is off: the guard notice ("…the global entries only ever add to the project ones") is shown, and `guests/global_install` was never written to settings. |
| `implementer-options-03-project-on.png` | Row 1 turned on by keyboard: the notice names the settings file it wrote; the project's `.claude/settings.json` now carries the `--relay-guest` marker and still holds the user's own hook. |
| `implementer-options-04-codex-conflict.png` | Codex into a config with the user's own `notify`: the conflict message is shown and `~/.codex/config.toml` is byte-for-byte unchanged. |
| `implementer-options-05-project-off.png` | Row 1 turned off again: the marked entries came out of the settings file; the user's own hook stayed. |

## Result

All nine checks passed on the final run (after the fix this run found, see below): section render,
guard notice, guard not stored, marker installed, additivity, install notice, codex config
untouched, conflict notice, clean removal.

## What the run caught

The first run failed step 5 (off came out as a re-install): the done handler read `installed`
out of the `--on` answer, where it is the *list of entries the installer added*, while `--status`
carries a bool of the same name — so the row redrew unchecked after a successful install and the
next Enter installed again. The handler now takes the state from the action itself (`ok` means
the writes happened) and re-reads `--status` from the file right after; the global row never
touched the project cache. That fix is in the commit with the section, and this README documents
why the harness drives both directions of the same row.

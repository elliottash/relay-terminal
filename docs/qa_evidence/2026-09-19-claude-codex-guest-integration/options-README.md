# Options › Guests — Xvfb evidence (GT7X, 2026-09-19)

`options-guests-drive.py` drives the Options › Guests section on a live `relay` under `xvfb-run`,
with the isolation the channel harness uses (fresh HOME/XDG_*/TMPDIR, `RELAY_DATA_DIR` pinned to
this worktree) plus the fixtures the section's promises need: a project holding a hook of the
user's own in **both** `.claude/settings.local.json` (the per-developer file Relay writes) and
`.claude/settings.json` (the shared, source-controlled one Relay must never write, checked byte
for byte), and a `~/.codex/config.toml` holding the user's own `notify`. Rows are driven by keyboard — the Options search box (type,
Enter) and the pane's own Left/Right tab keys — and every step checks both the screen (OCR of the
picture taken) and the file the row claims to touch.

Run: `xvfb-run -a python3 options-guests-drive.py` (from this directory; the binary is
`../../../build/relay`). The run's own log is the verification: every check prints PASS or FAIL,
and the exit status is non-zero on any FAIL.

## Pictures and what each is evidence of

| Picture | Evidence |
| --- | --- |
| `options-01-guests-section.png` | The section renders: the four rows (Claude Code in this project, Also in ~/.claude/settings.json, Claude Code edits as Relay diffs, Codex notifications) are on screen after arriving at the tab. The bridge row is the only way to turn `guests/claude_bridge` on. |
| `options-02-global-guarded.png` | The global row while the project row is off: the guard notice ("…the global entries only ever add to the project ones") is shown, and `guests/global_install` was never written to settings. |
| `options-03-project-on.png` | Row 1 turned on by keyboard: the notice names the settings file it wrote; the project's `.claude/settings.local.json` now carries the `--relay-guest` marker on a `PermissionRequest` hook (and on no `PreToolUse`) and still holds the user's own hook, while the shared `.claude/settings.json` is byte-for-byte what it was. |
| `options-04-codex-conflict.png` | Codex into a config with the user's own `notify`: the conflict message is shown and `~/.codex/config.toml` is byte-for-byte unchanged. |
| `options-05-project-off.png` | Row 1 turned off again: the marked entries came out of the settings file; the user's own hook stayed, and the shared file was never written in either direction. |

## Result

Thirteen of the fourteen checks passed on the re-run of 2026-09-19 (the review pass below), and the
one FAIL was the harness's own: `03-permission-request` looked for `guest_hook.py PermissionRequest`
in the settings file, where the command's quotes are JSON-escaped and the event name follows
`guest_hook.py\"`. The same run's `03-no-pretooluse` and `03-marker` passed on the same lines, and
the file the run left behind carries exactly one marked group per event with `PermissionRequest`
among them, so what the check was asserting is true; the substring is corrected in the script and
was not re-driven. The other thirteen: section render, bridge row present, guard notice, guard not
stored, marker installed in `settings.local.json`, additivity, no `PreToolUse`, the shared
`settings.json` untouched, install notice, codex config untouched, conflict notice, clean removal,
shared file still untouched.

The pictures `options-0*.png` are from that re-run; the earlier `implementer-options-*.png` set
predates the move to `settings.local.json` and is kept only as the record of the run that found the
toggle bug described below.

## What the run caught

The first run failed step 5 (off came out as a re-install): the done handler read `installed`
out of the `--on` answer, where it is the *list of entries the installer added*, while `--status`
carries a bool of the same name — so the row redrew unchecked after a successful install and the
next Enter installed again. The handler now takes the state from the action itself (`ok` means
the writes happened) and re-reads `--status` from the file right after; the global row never
touched the project cache. That fix is in the commit with the section, and this README documents
why the harness drives both directions of the same row.

## What the review pass changed (2026-09-19)

The harness itself was asserting on the wrong file: it wrote its fixture into, and checked,
`.claude/settings.json`, while the installer had moved to `.claude/settings.local.json` (26.3) —
so the "install" check would now read a file Relay never touches. The fixture is in both files,
the assertions are on the one Relay writes, and the shared one is checked byte for byte at both
ends of the round trip. Two rows' text also named `settings.json` to the user, and there was no
row at all for the IDE bridge, so `guests/claude_bridge` — and with it `openDiff`, the whole of
26.5 — could only be turned on by editing `relay.conf` by hand. Both are fixed and driven here.

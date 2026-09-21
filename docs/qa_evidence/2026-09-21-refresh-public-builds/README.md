# Public build refresh — #R6BS

The owner authorized native Windows and accepted PowerShell 7 as its default. Work is committed to
main through land.py; the shared checkout's unrelated unfinished changes are excluded.

## Verified milestones

- Website refresh and corrected heading decoration deployed in e24a7d06, de16c983 and f1982f4e.
- Native ConPTY lifecycle smoke passed on Windows runner [35665510167](https://github.com/elliottash/relay-terminal/actions/runs/35665510167).
- Native identity, process telemetry and crash-log smoke passed in [35665925721](https://github.com/elliottash/relay-terminal/actions/runs/35665925721).
- PowerShell multiline load/hash acknowledgement/execute was driven through a real PSReadLine terminal locally. Native test caught a Windows file-sharing race; fixed 2cfcb3f0 and GUI reader closes promptly.
- Qt6 exact-tree builds and relevant theme, SSH, voice, board, model and console tests passed. Qt6 terminal palette tests were made deterministic after pixel evidence showed antialiasing rounding, not wrong colors (337a2d53).
- Targeted Python storage, locking, keys, sessions, routing, jobs, prompt and release-fixture tests passed. Native Windows-only cases run in windows.yml.

## Release gates still running

Full native desktop compilation, private runtime packaging, per-user installer launch/uninstall,
all six Linux package builds and installed smoke checks must pass before publishing beta.3 or
activating its website links. The release workflow now requires both platforms and includes the
Windows installer in SHA256SUMS. Prepared website download edits remain unpublished until then.

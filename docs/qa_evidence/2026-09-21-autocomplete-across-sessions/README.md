# #CMEM implementer evidence

Autocomplete now has a shared shell-command store at
`$XDG_DATA_HOME/relay/state/command-history.txt`, written when the integrated shell acknowledges
loading a command. The file uses the existing private bounded history format; a lock serializes
append/trim across Relay processes. Lookup caches file size/mtime and refreshes across writers.
Pane-local Up/Down history stays separate. Pane/directory suggestions keep priority, followed by
shared commands, then shell history. Clear prompt history removes the shared store too.

## Automated checks

- `scripts/relay-build --target relay-prompthistory-tests relay-editor-tests`: pass.
- `ctest --test-dir build -R '^(prompthistory|editor)$' --output-on-failure`: 2/2 pass.
- Also run through `TestsCommands.run_and_wait` to record Switchboard test history.
- New regressions cover reopening, shared writer refresh, newest prefix match, no exact/empty
  prefix completion, excluding multiline entries, pane-history separation/pruning, and clearAll.
- `scripts/relay-build --target relay`: pass (2026-09-21.09H.09).

## Live check

`python3 docs/qa_evidence/2026-09-21-autocomplete-across-sessions/drive.py`
launched Relay under Xvfb with isolated XDG config/data/cache/runtime directories and HISTFILE.
A harmless function named `sudo` printed its arguments; no package manager or system sudo ran.

1. Submit `sudo apt update` using Ctrl+Shift+Enter; assert it is in the command store.
2. Open a new tab with Ctrl+T and type `sudo apt`: `new-pane.png` shows dim ` update`.
3. Press Right: `accepted.png` shows the accepted full command. Do not execute it.
4. Clear the draft, stop Relay, remove the test's saved layout, and launch a fresh process.
   Type `sudo apt`: `restarted.png` again shows dim ` update`, in a new pane.

All three screenshots inspected by the implementer. Independent QA remains outstanding.

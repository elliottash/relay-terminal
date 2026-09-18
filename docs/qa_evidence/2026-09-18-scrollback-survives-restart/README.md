# Scrollback survives a quit and restart (card SB7K) — implementer evidence

Implementer: Claude Opus 5 (Claude Code session, agent C), 2026-09-18. These are implementer
screenshots, not a QA verdict. `drive.sh` reproduces them under Xvfb with an isolated profile
(`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME` **and** `XDG_RUNTIME_DIR`, because
the layout file has one owner at a time and a Relay running in the real session would make the
test instance "the second Relay" and stop it saving anything). No provider account, key or
network is involved: the pane's own shell does all the printing.

Three runs of the same profile: print 120 lines and quit, reopen and look, quit and reopen again.

| File | What it shows |
|---|---|
| `implementer-a-before-quit.png` | Run 1: `echo MARKER-BEFORE-QUIT; seq 1 120; echo TAIL-BEFORE-QUIT` has scrolled most of itself off the screen. Relay is then quit with Ctrl+W → Close |
| `implementer-b-restored.png` | Run 2, started with no `--workspace` so the saved layout is reopened: the same text is back, ending at `TAIL-BEFORE-QUIT`, then the muted rule "— end of restored scrollback; this shell is new —", then the new shell's own prompt |
| `implementer-c-scrolled-back.png` | Run 2 after four PageUps: the new shell's first lines, the opening rule "— scrollback from before the restart —", and then the saved text from `MARKER-BEFORE-QUIT` and `1`, `2`, `3`… — in order, and scrollable |
| `implementer-d-second-quit.png` | Run 3: the restore survives a second restart. `echo MARKER-AFTER-RESTART`, typed *after* the first restore, was saved with the rest, and exactly one pair of rules is shown — a previous restore's rules are filtered out when the pane saves again |
| `implementer-store.txt` | The state directory after each quit: `windows.json` and one `scrollback/<id>.txt`, the pane node carrying that `scrollback` id, the file's line count and its first and last lines, and a grep showing both runs' markers in the second save |
| `relay-stderr.log` | Empty: no Qt warnings across the three runs |

Read from `implementer-store.txt`: the first quit saved 128 lines (595 bytes) under the pane id
in `windows.json`; the second quit rewrote **the same file** (783 bytes) rather than adding a
second one, and it holds both `MARKER-BEFORE-QUIT` and `MARKER-AFTER-RESTART`.

Not covered here (needs a longer script or a desktop): splits and a second tab each keeping their
own text, two windows, the 5,000-line / 512 KiB caps on a real flood, the store being deleted by
"Start a fresh window set", and a pane left in `vim`. Those are checklist items 3, 4, 6, 7 and 8
of `issues/changes/needs_qa_llm/2026-09-18-scrollback-survives-restart.md`. The pure half of the
store — caps, id validation, pruning, the empty-means-delete rule — is covered by
`tests/windowstate_test.cpp` (ctest group `windowstate`, 29 cases).

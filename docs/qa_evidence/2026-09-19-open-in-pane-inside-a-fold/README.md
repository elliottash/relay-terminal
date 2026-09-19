# "open in pane" inside a fold (#7FD3) — independent QA run

Verified 2026-09-19 (11:50–11:55), live under Xvfb with isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_DATA_HOME`, `XDG_RUNTIME_DIR` (with the `bus` symlink pane isolation needs) and `TMPDIR`.
No provider account: `relay.conf` points the pane at `stub-provider.py` on 127.0.0.1 (the
thinking-fold harness shape, `2026-09-19-thinking-fold/drive.sh`).

**Binary:** `/tmp/relay-7fd3/build/relay`, built from a clean `git worktree` of `main` at
`424eff5` (2026-09-19 11:26, which contains the fix `7241a40`, landed 10:50) — *not* the repo's
`build/relay`, which by 11:32 carried uncommitted guest-bridge work in `src/Pane.h`.

## Reproduce

```bash
docs/qa_evidence/2026-09-19-open-in-pane-inside-a-fold/drive.sh <build-dir> [run merged]
```

Needs Xvfb, xdotool, ImageMagick, tesseract. Every step asserts on OCR of the live window; the
script fails loudly the moment one does not hold. Two scenes:

### `run` — the fold's own link, on a call seconds old (the case that failed every time)

1. Ask *"could you run seq 1 40 please"* → the stub answers one `run_command` call of `seq 1 40`;
   the pane draws `▸ ran seq 1 40 · 40 lines · exit 0` (`implementer-run-row.png`).
   *Note: the ask is phrased as a request — in AUTO input, `seq 1 40` alone routes to the
   terminal, and no agent turn happens at all.*
2. Click the row (OCR-located): the fold opens with the 40 lines and its foot link
   `open in pane` (`implementer-run-fold.png`).
3. Click `open in pane` ~6 s after the answer, pane never restarted: a preview pane opens,
   titled `run_command-call_1.log`, holding `RUN COMMAND / Working directory: … / Waits: 30s,
   then continues as a job / seq 1 40` and the numbered output (`implementer-run-preview.png`).
   The OCR asserts `RUN COMMAND` (or `Working directory`) is present, `40` is present, and the
   old failure text `not available` is not.

### `merged` — a merged run's fold offers no link, but files still open

1. Ask *"please show me those six files"* → the stub answers six `read_file` calls
   (`src/a.py`…`src/f.py`); the pane merges them into `▸ read 6 files · 72 lines`
   (`implementer-merged-row.png`).
2. Click the row: the fold lists the six members, `read src/a.py · 12 lines` …
   (`implementer-merged-fold.png`). The OCR asserts `a.py` is listed and the phrase `in pane`
   is **absent** — no `open in pane` link, by design.
3. Click a member row (`src/a.py`): the file's preview opens beside the pane — `MARKER-A file 1`
   and `a line 1…11` from the file's contents (`implementer-merged-file.png`).

`implementer-notes.txt` holds the OCR of every shot; `relay-<scene>.log` and `logs-<scene>/`
(the app and worker logs) were captured by `stop()` at each scene's end.

## Results

| Check (card QA checklist) | Result |
| --- | --- |
| Fold's `open in pane` opens the call's output in a preview pane, seconds-old call, pane not restarted | **Pass** (`implementer-run-preview.png`) |
| Merged run's fold offers no `open in pane`; member rows still open their files | **Pass** (`implementer-merged-fold.png`, `-file.png`) |
| `ctest --test-dir build -R calllines` | **Pass** (below) |

## Regression test

`tests/calllines_test.cpp`, `theFoldOpenInPaneLinkNamesOneRefetchableCall()`, beside
`everyAnchorCarriesEnoughToRefetchTheCall()` (#EC58): for every anchor a fold row can carry
(plain, merged `+6`, percent-encoded ids), the URI `Pane::foldOptions()` puts in
`FoldOptions::openInPane` — `openUri(pane, ref.turn, ref.call)` of the anchor's parsed ref —
must parse back to a valid `relay://open-call` ref with a non-empty turn and exactly the call
id `openCallTarget()` falls back to, never a merged run.

```
$ ctest --test-dir build -R calllines
1/1 Test #53: calllines ................   Passed    0.02 sec
100% tests passed, 0 tests failed out of 1
```

## Not verified here

The owner's running Relay instance (pid 56781, started 11:30:55) maps a **deleted** binary —
the repo's `build/relay` was rebuilt underneath it at 11:32. Restarting Relay picks up a build
that contains `7241a40`. No Relay is installed outside the repo (`~/.local/bin`, `/usr/local`,
`/usr` all have none), so "rebuild or update to at least `7241a40`" means the repo build plus a
restart.

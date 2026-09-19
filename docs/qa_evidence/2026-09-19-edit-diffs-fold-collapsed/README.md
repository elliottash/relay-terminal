# Edit diffs fold under the row, collapsed by default (2026-09-19, #WXT6)

## The request

Owner, 2026-09-19:

> for edit_file snippets, keep those collapsed by default.

Until now a write or an edit of at most 12 changed lines printed its diff under the call row
with no click at all (`Pane::printInlineDiff`, #TK9C's owner decision of 2026-09-18); only a
bigger diff opened the diff pane. This change retires the auto-print: every diff, small or big,
folds behind its row's click — a small one is answered from the diff the pane already stores
(`relay::calllines::foldForDiff`), a big one still opens the diff pane. The subagent transcript
gets the same rule (its `appendDiff` auto-print is gone too; the diff was already in the row's
detail, so a click draws it).

## Where the code is

The source half of this change reached `main` inside the 18:00 tree sweep `8147cc55` ("src and
engine: the sessions' in-flight code as the shared tree holds it"), which committed this
session's in-flight `src/` edits along with everyone else's. The commit that lands with this
card carries the rest: the headless tests, the protocol and architecture wording, and this
evidence.

- `src/Pane.h` — `printInlineDiff` and its `tool_result` call are gone; `foldRequested()` serves
  a write/edit fold from `record.diff` with no worker round trip (so it still answers once the
  worker's fifty-turn log has scrolled past the call).
- `src/CallLines.{h,cpp}` — `foldForDiff(unifiedDiff, palette, options)`: one unified diff as
  fold rows, drawn exactly as a `detail` section of style `diff` (green/red fills, hunk headers
  muted, the closing `open in pane · open <file>` row).
- `src/SubagentTranscript.{h,cpp}` — the auto `appendDiff` under the row is gone; the diff is in
  the row's detail, so the click that already folded the detail draws it.

## Live, under Xvfb (`pane-drive.sh`)

A real Relay (libvterm core) against `pane-stub-provider.py` on 127.0.0.1 — copied unchanged
from the #TK9C evidence — with `HOME`/`XDG_*`/`TMPDIR` isolated. Relay Free is turned off inside
the run (a `PYTHONPATH` package named `cryptography` that raises), so the first submission opens
the key dialog; `provider/base` is a loopback URL, the dialog sees "a model server on this
machine" and needs no key, and the run clicks Save (OCR-located beside Cancel).

The turn the stub drives: a python heredoc, `ls -la`, a failing grep, four reads (one merged
row), **a small edit of alpha.py (+2 −1)**, a new file, **a big edit of big.txt (+30 −30)**, and
a final answer.

    docs/qa_evidence/2026-09-19-edit-diffs-fold-collapsed/pane-drive.sh <build-dir> after

| Shot | What it shows |
|---|---|
| `after-02-rows-folded.png` | after the turn every row is one folded line: `▸ edited alpha.py · +2 −1` sits directly above `▸ wrote brand_new.py · new · 1 line` — **no diff under it**. The turn closes with `✦ 10 tool calls · 1 s` |
| `after-03-small-diff-unfolded.png` | a Ctrl+click on the alpha.py row (found by OCR): the marker turns ▾, and the fold opens in place — `@@ -1,2 +1,3 @@`, the removed `SMALL = 1` and the added `SMALL = 2` / `EXTRA = 3` on the green/red fills, ` keep = True`, and the closing `open in pane · open alpha.py` row. This fold is served from the pane's stored diff, not the worker |
| `after-04-big-diff-pane.png` | a Ctrl+click on `▸ edited big.txt · +30 −30`: the **diff pane** beside the terminal (`+++ b/big.txt`, `@@ -7,36 +7,36 @@`, old/new gutter) — unchanged behaviour |

`run-after.log` has every shot's OCR, including the two lines above. The old behaviour — the
same small edit printing its diff under the row with no click — is on record as
`docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-02-lines-folded.png` (shot list:
"`▸ edited alpha.py · +2 −1` **with its diff printed underneath**").

A second run (`before-*`, `run-before.log`) was driven to contrast plain `main`; by the time it
ran, the tree sweep `8147cc55` had already put the source change on `main`, so both builds held
the same behaviour and the run doubled as a repeat of the table above instead of a contrast.

## Headless

`tests/calllines_test.cpp` — `aStoredDiffFoldsWithoutAWorker`: `foldForDiff` drops the file
headers, keeps the hunk header and context, paints `-old` in the remove ink on the remove fill
and `+new` in the add ink on the add fill, and closes with the `open in pane · open x.py` links
row.

`tests/subagents_test.cpp` — the small-edit case now asserts the row stays collapsed (no
`-old = 1`, no `@@` anywhere) until `toggleToolCall`, and that the click draws the diff including
the hunk headers.

    scripts/relay-build --target relay-calllines-tests relay-subagents-tests \
                        relay-toollabel-tests relay-turntranscript-tests
    ctest --test-dir build -R "calllines|subagents|toollabel|turntranscript"
    # 4/4 passed, 2026-09-19

The landed tree itself (tip + this change, materialised outside the shared checkout) builds all
targets, `relay` included.

## What this does not show

- **`agent/show_tool_output` on.** With the stream on, output prints under the line as before
  (the code path is unchanged by this card).
- **A subagent transcript shot.** The subagent surface is covered headless
  (`tests/subagents_test.cpp` above); its click-to-expand path is the same detail fold the test
  drives.
- **A ghostty-core run.** libghostty-vt is not installed on this machine; the fold content is
  core-independent (the engine's fold layer is the same code the #TK9C evidence exercised).

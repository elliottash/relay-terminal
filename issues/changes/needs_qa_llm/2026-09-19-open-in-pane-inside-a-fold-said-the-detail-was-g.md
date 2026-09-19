---
id: 7FD3
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Fable 5.1 (batch review), 2026-09-19, in `7241a40`
rank: zzzz112
created: '2026-09-19'
acceptance: clicking "open in pane" at the foot of an open tool-call fold opens that call's output in a preview pane, including for a call that ran seconds ago
source: 'issues/bug_intake.txt, 2026-09-19: "i clicked on an open in pane item for a tool call and it gave an error ''calls detail not available in this pane any more''"'
links: {commits: [7241a40], evidence: [docs/qa_evidence/2026-09-19-open-in-pane-inside-a-fold/], github: null, plans: [], related: [EC58, TK9C]}
---
# "open in pane" inside a fold always said the call's detail was gone — even one second after the call

## Issue

i clicked on an open in pane item for a tool call and it gave an error "calls detail not available
in this pane any more"

## Reproduced (2026-09-19, live under Xvfb, isolated XDG dirs, scripted local model)

Against `build/relay` as it stood at 10:26 — i.e. a build made *before* `7241a40` landed at 10:50:

1. A turn whose agent calls `run_command` with `seq 1 40`. The pane draws `▸ ran seq 1 40 · 40 lines · exit 0`.
2. Click the row. The fold opens with the 40 lines and its last row, the link `open in pane`.
3. Click `open in pane`. Status line: **"That call's detail is not available in this pane any more."**
   (`src/Pane.h`, `openCallTarget()`). Nothing opens.

The call was one second old, the worker was ready and the pane held a live `CallRecord` for it, so
none of the reasons the message gives were true.

**Why it fired every time.** The map of call records (`m_calls`) is keyed by the *row's* anchor,
which for a foldable row is `relay://call/<pane>/<turn>/<call>`. The fold's own "open in pane" link
is a different URI — `relay://open-call/<pane>/<turn>/<call>` (`foldOptions()`, `src/Pane.h:4078`,
via `linkRow()` in `src/CallLines.cpp:332`). So `m_calls.value(uri)` in `openCallTarget()` misses
for that link *always*, not only for an old or restored line; and the guard it fed then read
`record.callIds.isEmpty()`, which was therefore always true. That is the same root cause as #EC58 —
the code gave up when the record lookup missed instead of using the ids the URI already carries —
but a different symptom from the ones #EC58's checklist lists: those are all about a record that
has *gone* (a restored pane, a line past the map's bound), while this one never had a record under
that key at all, so it failed on a live, seconds-old call.

## Fixed on `main` by `7241a40` (#EC58, steps 1-3)

That commit replaced the guard with `callId = record.callIds.isEmpty() ? ref.call : record.callIds.first()`,
so the URI's own call id is used when the record lookup misses.

**Verified, same steps, on a binary built from `main` at `7241a40`:** clicking `open in pane` opens
a preview pane titled `run_command-call_1.log` holding `RUN COMMAND / Working directory: … / seq 1 40`
and the 40 lines. No status line.

So there is nothing left to change in the code. What is left is QA of the path the owner actually
clicked, which #EC58's own checklist does not cover, and the fact that **the owner is running a
build older than `7241a40`** — the bug is in every Relay built before 2026-09-19 10:50.

## Tasks

- [ ] Owner: rebuild or update Relay to at least `7241a40`; the installed build that produced this <!-- t:k4 -->
      report predates the fix
- [x] A regression test for the fold's link specifically: `foldOptions()` must produce an <!-- t:m7 -->
      `openInPane` URI that `parseUri()` reads back to a non-empty turn and a single call id, so a
      later change to either spelling cannot silently break the link again
      (`tests/calllines_test.cpp`, beside `everyAnchorCarriesEnoughToRefetchTheCall`)

## Decisions

- 2026-09-19, agent: filed as its own card rather than appended to #EC58, because the clicked
  surface and the failing condition are different (a fold's link with a live record, versus a row
  whose record has gone) and #EC58 is already in the QA lane with a checklist that does not reach
  this path. Kept in `needs_qa_llm/` rather than closed: the fix is real but unverified by an
  independent QA session, and the implementer and the verifier here are both Claude.
- 2026-09-19, agent: `m_calls` is still keyed by the row anchor, so the fold's link still misses the
  record and the preview pane is built from the worker's reply rather than the stored label. That is
  harmless today — "open in pane" is only offered on rows whose click type is `Fold`, which is the
  one type that does not use the label to decide where to go — so it is recorded here rather than
  changed.

## QA checklist
- [x] Run a turn with a tool call, click the `▸` row to unfold it, then click `open in pane` at the
      foot of the fold: the call's output opens in a preview pane. The call is seconds old and the
      pane has not been restarted — this is the case that used to fail every time.
      Verified 2026-09-19 (independent session, binary from a clean worktree of `main` at `424eff5`):
      `run` scene of the evidence drive — fold clicked ~6 s after the answer, preview pane
      `run_command-call_1.log` with `RUN COMMAND / Working directory / seq 1 40` and the 40 lines,
      no status line (`implementer-run-preview.png`).
- [x] The same on a merged run's row (`read 6 files`): the fold offers no `open in pane` link, by
      design, and the member rows still open their files.
      Verified 2026-09-19: `merged` scene — six reads merged into `read 6 files · 72 lines`, the fold
      lists the members with no `in pane` phrase anywhere in the window, and clicking `src/a.py`
      opened the file's preview (`implementer-merged-fold.png`, `-file.png`).
- [x] `ctest --test-dir build -R calllines` passes.
      Verified 2026-09-19: `1/1 Test #53: calllines … Passed 0.02 sec`, with the new
      `theFoldOpenInPaneLinkNamesOneRefetchableCall()` in the suite.

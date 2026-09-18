---
id: TK9C
type: work
status: needs-qa-llm
labels: [feature]
component: [agent, worker, gui]
milestone: 0.1-preview
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagents, one per package), coordinated by Claude Fable 5.1, 2026-09-18
rank: zz06
created: '2026-09-18'
acceptance: every agent tool call prints one concise line ("ran python script · 14 lines · exit 0 · 1.2 s", "wrote x.py · new · 48 lines", "edited x.py · +3 −1"); clicking it folds the full detail open in place; a diff of at most 12 changed lines prints inline and a larger one opens a diff pane; consecutive reads and listings merge into one line
source: 'owner, 2026-09-18: "agent tool calls are too detailed. rather than seeing a mini python script, i would rather see something like ''executed python'' … you can then click on them to uncollapse the full details. similarly with ''wrote x.py'' or ''edited x.py'' … concise and informative and allow easy access of relevant information."'
links: {plans: [], commits: [95568c1, 59e86b6, d685107, e10e5f3, 8dd9189, c5f535c, 4fad23a, 1cad196, 661c118, 5b37065, b268024, 790fc76, acfac22, 1a86631, 2e75136, 07ad727, 40b785a, 77a0f25, 5d3ad7a, 062b2aa, d3f5624], evidence: ['docs/qa_evidence/2026-09-18-concise-tool-call-lines/'], related: [E4TX], github: null}
---
# Concise tool-call lines, with the detail one click away

## Issue
agent tool calls are too detailed. rather than seeing a mini python script, i would rather see
something like "executed python" … you can then click on them to uncollapse the full details.
similarly with "wrote x.py" or "edited x.py" … concise and informative and allow easy access of
relevant information.

## Decisions
- 2026-09-18, owner: the verb is **"ran"**, not "executed" — "keep it concise".
- 2026-09-18, owner: a click **folds the detail open in place** in the terminal, it does not open a
  separate pane. A diff of **at most 12 changed lines** (added + removed) prints inline under the
  line with no click at all; a larger one opens the diff pane (`src/DiffView`, #E4TX).
- 2026-09-18, owner: **consecutive similar calls merge** into one line ("read 6 files · 4,100 lines").
  Only reads and directory listings merge; commands, edits and agents never do, and a failed call
  never merges.
- 2026-09-18, owner: a **short command** (one line, at most ~40 characters) is shown whole
  ("ran ls -la src"); a longer one shrinks to the program name, keeping the subcommand for
  multi-command CLIs ("ran git commit"), and a pipeline or `&&` chain names the first real command
  and counts the rest ("ran grep +2").

## The look
```
▸ ran python script · 14 lines · exit 0 · 1.2 s
▸ ran git status · 6 lines
▸ ran grep +2 · 37 lines
▸ ran pytest ✗ exit 1 · 212 lines · 8 s
▸ read 6 files · 4,100 lines            (merged run of reads)
▸ listed src/ · 40 entries
▸ wrote x.py · new · 48 lines
▸ edited x.py · +3 −1                   (+ the short diff printed beneath)
▸ edited Pane.h · +212 −87              (click → diff pane)
▸ started job: npm run dev              (background run_command)
▸ started subagent "fix tests"
▸ updated todos · 3 open
▸ moved card #K7Q2 → done
✗ edit x.py · old_string was not found in the file
```

## Tasks
- [x] Package C — backend labels: `tool_labels.py`, the `label` on the tool events and the turn summary, the structured `detail` on the `tool_output_get` reply, and the protocol section the other packages read <!-- t:c1 -->
- [x] Package E — the engine fold layer: a terminal line that folds its detail open in place <!-- t:e1 -->
      `engine/view/FoldLayer.{h,cpp}` (the visual-row maths, on its own, `engine/tests/FoldLayerTest.cpp`),
      `VtCore::hyperlinkRuns()` / `searchCurrentRow()` on both cores, the view's paint, hit-testing,
      scrolling, visual-order selection and `engine/view/FoldSearch.{h,cpp}` so find reads the open
      folds in the order the rows are on screen. Commits 59e86b6, d685107, e10e5f3, 8dd9189,
      c5f535c, 4fad23a, 1cad196. Evidence: the `libvterm-*.png` shots in the evidence folder.
- [x] Package G — the GUI: render the label fields, the inline diff, the merged runs, and what a click opens (fold, file, diff pane, subagent, card, plan, todos) <!-- t:g1 -->
      The terminal pane: `src/CallLines.{h,cpp}` (the URI, the row and its cut, the
      rewrite-or-new-row state machine and a fold's rows, all headless in
      `tests/calllines_test.cpp`) and the glue in `src/Pane.h` that writes the bytes, plus the diff
      pane (`ToolPane::Kind::Diff` over `relay::DiffView`) the turn pane and the subagent
      transcript now open too. Commits 07ad727, 40b785a, 77a0f25, 5d3ad7a. Evidence:
      `docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-README.md`.
- [x] The other surfaces (web app, remote panes) render the same labels <!-- t:s1 -->
      Package F: `src/ToolLabel.{h,cpp}` (the shared parser, reused by the terminal pane's
      `src/CallLines.h`), the subagent transcript, the turn pane, the Switchboard cleanup's
      progress line, `app/app.js` and the `remote/panes.py` demo. Evidence:
      `docs/qa_evidence/2026-09-18-concise-tool-call-lines/surfaces-README.md`.

## Where it landed

| Package | Commits | Code |
|---|---|---|
| C — backend labels | 95568c1 | `backend/relay_core/tool_labels.py`, the `label` on `tool_started`/`tool_result`/`turn_summary`, the structured `detail` on `tool_output_get`, protocol § 23 |
| E — the engine fold layer | 59e86b6, d685107, e10e5f3, 8dd9189, c5f535c, 4fad23a, 1cad196 | `engine/view/FoldLayer.*`, `engine/view/FoldSearch.*`, `engine/view/TerminalView.cpp`, `VtCore::hyperlinkRuns()`/`searchCurrentRow()` on both cores, `engine/scripts/gui/folds.sh` |
| F — the other surfaces | 661c118, 5b37065, b268024, 790fc76, acfac22, 1a86631, 2e75136 | `src/ToolLabel.*`, `src/SubagentTranscript.*`, `src/TurnTranscript.*`, `app/app.js`, `remote/panes.py`, the Switchboard cleanup line |
| G — the terminal pane | 07ad727, 40b785a, 77a0f25, 5d3ad7a | `src/CallLines.*`, the glue in `src/Pane.h`, the two shortcut hints |
| The diff viewer it opens | 062b2aa | `src/DiffView.*` (`ToolPane::Kind::Diff`) |
| The tool the "+3 −1" line describes | d3f5624 | `edit_file`, card #E4TX — that card's commit, listed here because the edited-file line has nothing to show without it |

Tests: `tests/test_tool_labels.py` (the backend parser), `tests/toollabel_test.cpp`,
`tests/calllines_test.cpp`, `tests/turntranscript_test.cpp`, `engine/tests/FoldLayerTest.cpp`,
`engine/tests/FoldSearchTest.cpp` and the fold cases in `engine/tests/ViewTest.cpp`.

## QA checklist

Run a real turn against a real provider (or `docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-drive.sh`,
which drives a real Relay under Xvfb against a loopback stub — no keys, no network). Unless a step
says otherwise, Agent options › **Show tool output** is **off**, which is the new default behaviour.

1. **Every line kind.** Ask for a turn that runs a short command, a long one, a pipeline, a failing
   command, a background job, several reads, a directory listing, a small edit, a new file, a big
   edit, a subagent, a todo update and a card move. Each prints **one** row in the shape of "The
   look" above: `▸ ran ls -la · 9 lines · exit 0`, `▸ ran grep +2 · 37 lines`, a failure in the
   error ink with `✗`, `▸ started job: …`, `▸ read 4 files · 125 lines`, `▸ listed src/ · 40 entries`,
   `▸ wrote x.py · new · 48 lines`, `▸ edited x.py · +3 −1`, `▸ started subagent "…"`,
   `▸ updated todos · 3 open`, `▸ moved card #… → done`. No script body, no command output, no JSON.
   The turn ends with `✦ N tool calls · N s`.
2. **Fold and unfold.** Click a `▸ ran …` row. Its detail opens **underneath it, in the terminal**,
   tinted, indented, with a rule down the left edge and the `command` / `output` headings; the
   chevron turns `▾`; the rows below move down and the newest output stays on screen. Click again:
   it shuts and the real rows close up. Ctrl+Shift+Return unfolds the nearest row without the mouse.
   A row whose detail the worker no longer has says so in one row instead of opening empty.
3. **Scroll, select and find inside a fold.** With a long fold open (300+ lines), the scrollbar
   range covers the fold's rows and one wheel notch steps three of *them*, not three output lines.
   Drag from above the fold to below it: the copied text reads in the order you see it, the fold's
   lines included. Ctrl+F for a word only the open fold has: it is found, counted and stepped
   through in screen order together with the real-row matches, and the count changes when the fold
   is shut or opened. Resize the pane: matches and the fold stay under their line.
4. **An inline diff of at most 12 changed lines.** An edit of a few lines prints its diff **under
   the row with no click**, added lines green, removed red.
5. **A diff pane above 12.** Click `▸ edited big.txt · +30 −30`: the diff pane opens beside the
   terminal with the file name, the counts and the old/new gutter. `n`/`p` step the hunks.
6. **Merged reads.** Four consecutive `read_file` calls make **one** row, `▸ read 4 files · 125 lines`.
   Unfold it: one member row per file, each a link that opens that file in a preview pane. A command
   or an edit between two reads breaks the run into two rows; a failed read never merges.
7. **A failure.** A command that exits non-zero prints in the error ink with `✗` and its exit code,
   and clicking it always opens its fold (never a pane), so the reason is one click away.
8. **wrote → opens the file.** Click `▸ wrote brand_new.py · new · 1 line`: the file opens in a
   preview pane. Same for a read's member row.
9. **Deferred output while a program runs.** Start something that owns the terminal (`vim`, `top`,
   a long `tail -f`), then ask the agent to run a tool. Nothing is drawn over the program; when the
   program exits, the finished lines are replayed as plain text (see Known gaps 5).
10. **"Show tool output" on.** Turn Agent options › Show tool output on. The stream prints under the
    line again, as before, and the line is final where it stands — it is not rewritten and the
    output is not swallowed.
11. **The turn pane and the subagent transcript.** Ctrl+click `✦ N tool calls` to open the turn
    pane: every row is the same label line, a merged run is one parent row whose members are still
    openable, and each call's duration is its own column; opening a call shows `detail` section by
    section (command as code, output as output, a `(truncated)` note where the section asked for
    one). In a subagent tab the same rows appear, a running call in the present tense
    (`running pytest`), rewritten in place when it finishes, and a click folds the detail under the
    row inside the transcript.
12. **The phone.** Open the web app on a phone or in a browser (`app/`): the same one-line-per-call
    rows, `<details>` starting shut, the red/green diff rows, and a tap opening the detail.
13. **Both engine cores.** Steps 2 and 3 on `--engine-core libvterm` **and** on
    `--engine-core ghostty`. See Known gaps 1: the ghostty path has never been compiled here.

## Known gaps

Each of these stays open because it needs a decision, a machine or a file this card may not take —
not because it was left loose (CLAUDE.md, "Fix clear gaps; do not list them").

1. **The libghostty-vt core path has never been built or run.** `GhosttyCore::hyperlinkRuns()` and
   `GhosttyCore::searchCurrentRow()` (`engine/core/GhosttyCore.cpp`), and with them fold anchoring
   and the find merge on that core, exist only as source: this machine has no libghostty-vt, so
   `RELAY_ENGINE_WITH_GHOSTTY` is OFF (`engine/CMakeLists.txt:13`) and every test and screenshot in
   this card is libvterm. It needs a build on a machine with libghostty-vt
   (`engine/scripts/build-libghostty-vt.sh`). On that core an anchor must **start at column 0**,
   which is why the pane writes the `▸ ` placeholder first.
2. **A `todos` line has no opener.** `clickFor()` returns `Click::Todos` (`src/CallLines.cpp:165`)
   and `openCallTarget()` has no case for it (`src/Pane.h`, `openCallTarget`), so a click falls
   through to the generic path: the call's stored output in a preview pane, not the todo list.
   Which surface a todo update should open is a product decision (the request ledger's pane, the
   Switchboard, or nothing at all) — owner's, not this card's.
3. **A `file`-typed line opens the file and cannot also be folded.** Reads, new files and listings
   are anchored under `relay://open-call/`, not `relay://call/`, so there is no fold to open on
   them — one click, one destination, by the owner's "easy access of relevant information". Only a
   merged run of reads folds (to its member rows). Listed so QA does not read it as a missing fold.
4. **Alt-drag rectangle selection covers the real rows only.** `TerminalView::mousePressEvent`
   (`engine/view/TerminalView.cpp`) turns the visual-order gesture off for a rectangular drag and
   goes straight to the core, which has never heard of the fold rows. An ordinary drag does cross
   folds correctly. A rectangle over a mixed sequence needs the column maths of the fold rows'
   indent and the real rows' grid reconciled — a change to the selection model, not a loose end.
5. **A line printed while a program owns the terminal is plain, with no anchor.** `src/Pane.h`'s
   `tool_result` branch draws the finished label as text when the shell is not idle at its prompt,
   because a replayed line cannot be rewritten and nothing could fold under it. Such a line cannot
   be unfolded afterwards. Deliberate; #BPK3 owns what that mode shows.
6. **The `call.fold` hint hard-codes Ctrl+Shift+Return** (`src/Pane.h`, the `call.fold` hint). That
   is an engine built-in, not a Keymap action, so it cannot be read from the live Keymap the way
   WARP.md's hint rule asks. Giving it a Keymap action means a new action id and a migration for
   saved keymaps, which belongs with the keymap work, not here.
7. **After a restart, restored scrollback is plain text.** `replayRestoredScrollback()`
   (`src/Pane.h`) prints the saved lines sanitized, without their escape sequences, on purpose: a
   hand-edited or truncated save must not be able to drive the terminal. So tool-call lines that
   come back from a previous shell carry no anchor and cannot be folded. Restoring them as folds
   means saving the calls as well as the text — #RC7Z's file format, not this card's.
8. **`ctest` `backend-and-bash` runs past its CMake `TIMEOUT`.** The test is declared with
   `TIMEOUT "120"` (`CMakeLists.txt`) and the suite takes about 130 s on this machine, so ctest
   kills it although nothing in it fails; `./scripts/test.sh` is clean. Foreign to this card and in
   a file with other sessions' uncommitted work, so it is noted, not changed — a QA run will hit it.

## Not a bug: the italic muted stats

`pane-03-command-unfolded.png` shows the muted stats in italic and `pane-02-lines-folded.png` does
not, which reads like a fold restyling the real rows around it. It is not. The muted remainder of
every call row was drawn with `Ink::Note`, which carried SGR 3 (italic) whether or not anything was
folded — the `✦ 10 tool calls · 1 s` line, which is nowhere near a fold, is italic in `pane-04` for
the same reason. The two shots were taken minutes apart from two different `build/relay` binaries
and the later one had already picked up the legibility pass. Italic muted monospace is gone with
fac5dac (#LG7T, `docs/ARCHITECTURE.md` § 14 rule 3), and the shots were re-taken afterwards.
`ViewTest::anOpenFoldLeavesTheRealRowsAloneWhenItIsPainted` pins the engine side: a real row
painted directly under a fold's last row is pixel-identical to the same row with the fold shut.

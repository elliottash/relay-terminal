# SFZC — common-word folder names highlighted in agent messages

Implementer evidence, 2026-09-20. Fix commit `a7fc3ca2` (scanner + view + tests);
the prompt line is on main in `9628872d` — see notes below.

## What "done" was taken to mean

In Relay-printed prose (a `relay://prose/` block, #R2WQ — agent messages), a bare
folder word — `tests`, `remote`, equally `docs`, `build`, `data` — is no longer a
link: not under the pointer, not in the at-rest link colour, not in the Ctrl+Shift+L
walk. A folder in prose links only when the token carries a `/` (`tests/`,
`src/Pane.h`). Bare *file* names keep their links (owner decision, 2026-09-20).
Program output (an `ls` of extension-less folders) keeps today's behaviour exactly.

## Change

- `src/OutputLinks.{h,cpp}`: `Mode::Program|Prose` on `resolve()`/`scan()`. Candidates
  from the bare-token stage (and quoted paths) without a `/` are marked `bare`; in
  `Mode::Prose` a bare candidate resolves only to a *file*. Tracebacks,
  `file(line,col)`, `file:line` and URLs are untouched.
- `engine/view/TerminalView.cpp`: all three link surfaces pass the mode —
  - hover (`linkAt`): the row's prose anchor URI (already fetched) picks the mode;
  - at-rest colour (`restLinkColumns`): a row whose first linked cell carries the
    `relay://prose/` anchor scans as prose; the rest-link cache key now carries the
    mode (same text, different answer per surface);
  - the walk (`collectLinks`): `hyperlinkRuns("relay://prose/")` once per keypress
    marks prose rows; a logical line any of whose rows is inside a block scans as prose.
- `backend/relay_core/agent.py`: one system-prompt line — name a folder with a
  trailing `/`.

## Provenance note (prompt line)

The prompt line was edited in the worktree first, then the #B9V4 session's
`land.py` sweep of `backend/relay_core/agent.py` (their own claim on the path)
carried it onto main inside their commit `9628872d` before a #SFZC commit existed
for it. Word-for-word the same line, one copy, worktree clean afterwards — no
second commit was made (rewriting their commit was not an option). #SFZC's own
commit for the behaviour change is `a7fc3ca2`.

## Verified

- `scripts/relay-build` clean (stamp-back confirmed).
- `ctest -R outputlinks`: 35 passed, 0 failed — five new prose-mode cases
  (bare words suppressed; `tests/` and `src/Pane.h` link; bare files and
  `file:line` link; quoted `'src'` suppressed but `'src/'` links; pasted
  tracebacks and URLs keep their rule).
- `relay-engine-tests` (offscreen): 41 passed, 0 failed — new
  `ViewTest::proseBareFolderWordsAreNotLinks` prints a real prose anchor run and
  asserts, against real files in a temp dir: hover empty on the prose folder word,
  link on the program row's same word; walk count 3 (not 4); at-rest link colour
  present on the program row and the prose row's `tests/`, absent on the prose
  row's bare `tests`. Pixel-level via `grab()`.
- Live smoke: the app starts and stays up under `Xvfb :77` with an isolated
  `XDG_CONFIG_HOME` (10s, no early exit), `build/relay`.
- `python3 -c "import ast; ast.parse(...)"` on the edited agent.py: parses.

Logs: `logs/tests.txt`.

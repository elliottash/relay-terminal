# #E0Y0 — label editor on the Board card page

Implementation: `dd02b23a` (`src/BoardPane.cpp`, `tests/boardpane_test.cpp`).

- `card-labels-row.png` — the meta line: each label followed by a muted ×, the row ends in +.
- `card-labels-field.png` — after +: the one-line field pre-filled `bug, voice`.

Both written by `BoardPaneTests::theCardPagesLabelsEditInPlace` with `RELAY_SHOT_DIR` set
(offscreen, Qt 5.15.13, aarch64). That test asserts: × sends `board_update` with `base_hash` and
the list minus the clicked label; + opens the field; Enter sends the edited list and hides it;
Esc hides it and sends nothing; the plain `tag:` link still sends nothing.

`ctest --test-dir build -R '^boardpane$'` — 1/1 passed (2026-09-25 18:04).

Not covered here: the worker side of `board_update` rewriting the card's `labels:` front matter
is the existing title-save path (`BoardView::saveCardEdit`), untouched by this change; a live
click in the running app was not done.

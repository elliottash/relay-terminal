# The link colour: before/after mockups (2026-09-19)

Owner: "clickable things need to be understood from colors", then "make the 'add color in program
output' an option that is on by default", then "make a mockup terminal pane with all the relevant
pieces and associated colors, with before/after for each theme".

- `mockup.py` — builds the page from `data/theme/themes/*.toml`, so every colour on it is the one
  the app paints. Run it from anywhere: `python3 docs/qa_evidence/2026-09-19-link-colour/mockup.py
  > green-means-open.html`.
- `green-means-open.html` — the page as generated at the commit that added `[ui] link`. Published
  privately as an artifact for the owner the same day.

What the page shows, per theme, before and after: a path and a URL in program output at rest, a
path under the pointer, `ls`'s own blue and `git status`'s own red left alone, a card reference,
the agent's prose with a Markdown link and a filename in backticks, a fold's "open in pane · open
Pane.h" row, the composer's path token, the file preview's host chip and the Sessions page's URL.
The numbers table is the same measure `tests/theme_test.cpp` asserts: 4.5:1 on every ground a link
is read on, ΔE ≥ 20 from `success`, ANSI 2 and 10, `shell` and `agent`.

The design and the rules are in `docs/ARCHITECTURE.md` § 14, "Green means you can open it".

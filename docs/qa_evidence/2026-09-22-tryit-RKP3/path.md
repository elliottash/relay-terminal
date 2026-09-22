# The path, marked

1. `agent` — Run stage.sh under Xvfb, open the models pane (Ctrl+Shift+M): the priorities page
   shows the seeded high/main/flash sections, each listed row carrying ▲▼ buttons at the left,
   and the one-line footer with no alt+digits. (Captured: open.png.)
2. `agent` — Click ▼ on main rank 1: the row moves down one rank and stays moved.
   (Already played end-to-end and asserted against the written config in
   `docs/qa_evidence/2026-09-22-rank-reorder-repro/drive4.py` — main became m2, m1, m3.)
3. `agent` — Drag a main row onto high's section: it leaves main and lands in high at the dropped
   rank. (Same drive: high became h1, h2, m1, main became m2, m3.)
4. `person` — Look at the page and use the buttons and a cross-section drag once. Whether the new
   affordances *read* — that you can see the ▲▼, that dragging into another section feels like it
   will work, that the footer tells you what you need — is a judgement a capture cannot make.

Counts: 3 agent, 0 check (the unit tests are the card's `## Tests`, already run), 1 person.

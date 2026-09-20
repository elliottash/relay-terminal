<!-- relay:entry 20260919T223316Z-qh author=agent kind=event model=glm-5.3 pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/b6804a0f73d5457291343c6bed16db66 -->
- ✦ agent created this card in In progress · issues/features/2026-09-19-tab-headers-lose-the-theme-swatch-and-the-detach.md

<!-- relay:entry 20260919T223336Z-5k author=agent kind=event model=glm-5.3 pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/b6804a0f73d5457291343c6bed16db66 -->
- ✦ agent updated this card · appended to `## What changed`

<!-- relay:entry 20260919T223342Z-nv author=agent kind=event model=glm-5.3 pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/b6804a0f73d5457291343c6bed16db66 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T223348Z-77 author=agent kind=event model=glm-5.3 pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/b6804a0f73d5457291343c6bed16db66 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Swatch and ⧉ removed, relay built clean, Xvfb verification screenshots and suites recorded in the evidence README · evidence docs/qa_evidence/2026-09-19-tab-header-cleanup/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T223630Z-3j author=agent kind=event model=glm-5.3 pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/b6804a0f73d5457291343c6bed16db66 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-19-tab-heade… → {"plans": [], "commits": ["409e5e11", "7558c9b2"], "evidence": ["docs/qa_evidenc…

<!-- relay:entry 20260919T223634Z-m3 author=agent kind=progress model=glm-5.3 pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/b6804a0f73d5457291343c6bed16db66 -->
Landed. `409e5e11` carries the `src/RelayWindow.h` removals (6 hunks, +8 −75), the evidence
folder and this card; land.py's verify build compiled the exact tree (`--target relay`).
The four `docs/ARCHITECTURE.md` edits are on main via `7558c9b2` (the owner's docs sweep
picked them up from the shared tree before my commit; land.py de-duplicated them out).
`ctest`: 61/63 — the two failures (`wordwrap`, `buttonfit`) come from other sessions'
uncommitted `src/WordWrap.*` and `src/Theme.cpp` edits, neither of which compiles
`RelayWindow.h`. `scripts/test.sh`: 3413 backend tests OK.

<!-- relay:entry 20260919T225457Z-j5 author=agent kind=event model=glm-5.3-flash pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/7fb60a5013da4f03a54fb51015c855df -->
- ✦ agent moved this card · Needs QA (LLM) → In progress · Owner asked for the attached-project chip (the oval at the left of an attached tab) to go as well; extending the same removal before QA · implemented_by glm/glm-5.3-flash

<!-- relay:entry 20260919T225500Z-1e author=agent kind=decision model=glm-5.3-flash pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/7fb60a5013da4f03a54fb51015c855df -->
Owner, seeing the first removal: "still need to remove that extra \"attached to\" bit (the little
oval on the left)". The oval is the attached-project chip (#916B). So the tab header loses the
chip as well, and with it the only mouse path to detach: "Detach this tab from <project>" in the
palette (Ctrl+Shift+A) is the one way now.

<!-- relay:entry 20260919T225504Z-wq author=agent kind=event model=glm-5.3-flash pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/7fb60a5013da4f03a54fb51015c855df -->
- ✦ agent updated this card · appended to `## What changed`

<!-- relay:entry 20260919T225513Z-rm author=agent kind=event model=glm-5.3-flash pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/7fb60a5013da4f03a54fb51015c855df -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260919T225519Z-nh author=agent kind=event model=glm-5.3-flash pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/7fb60a5013da4f03a54fb51015c855df -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Chip removed with the swatch and the ⧉; relay builds, attached-tab before/after OCR and screenshots in the evidence README · evidence docs/qa_evidence/2026-09-19-tab-header-cleanup/ · implemented_by glm/glm-5.3-flash

<!-- relay:entry 20260919T225646Z-xk author=agent kind=event model=glm-5.3-flash pane=9a197f09 turn=b8dcc53e6760499ab7c761bdf9cf600a/7fb60a5013da4f03a54fb51015c855df -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["409e5e11", "7558c9b2"], "evidence": ["docs/qa_evidenc… → {"commits": ["409e5e11", "7558c9b2", "2eabaa77", "fb002856"], "evidence": ["docs…

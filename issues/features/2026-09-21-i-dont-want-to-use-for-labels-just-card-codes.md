---
id: S53Z
type: work
status: needs-verification
assignee: agent
session: 93ac5915-9b34-4252-a54a-dee24df0cf0a
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
links: {plans: [], commits: [36032e9d], evidence: [docs/qa_evidence/2026-09-21-s53z-labels/NOTES.md], related: [], github: null}
---
# i dont want to use # for labels, just card codes.

## Issue
i dont want to use # for labels, just card codes.

i saw some labels, eg #terminal, #bug, etc. it should be reserved for card codes. either remove a text tag for labels, or choose a different symbol

## Plan
**Goal.** `#` is reserved for card codes (`#S53Z`) and never appears on a label. Everywhere the Switchboard draws a label as a hashtag (`#bug`, `#terminal`) it draws the bare word instead, and clicking a label copies something that is not a hashtag.

**Decision (owner, 2026-09-21).** "dropping is OK" — drop the symbol entirely. Labels render as bare words everywhere, and clicking a label (badge, meta row, body link) copies `label:<name>` so the paste still lands a working filter term. No replacement symbol.

**Findings.** Labels become hashtags in these places, all under the `#3ZAP` behaviour ("labels read as the hashtags they are and copy on a click"):

- `src/BoardPane.cpp` ~6433: the card detail page's meta row renders `labels #bug #terminal`, each a `tag://<label>` link.
- `src/BoardPane.cpp` ~3416: a `#tag` inside a card's body text is linkified as a label hashtag; a `#ID` that names a card zooms instead (this distinction is why `#` is ambiguous).
- `src/BoardPane.cpp` ~7880–7889: the click handler (`tag:` scheme, also entered from 2011/2029) copies `"#" + tag` to the clipboard.
- `src/BoardPane.cpp` ~1183/1279/1331/1357: a click on a row's label badge copies the hashtag rather than selecting the row. The badge itself is drawn from `board::badges` (`src/BoardModel.cpp`, `badges()`) whose label badge text is already the bare word — confirm at implementation time whether any surface still prepends `#` to the badge text.
- The filter-bar label chips (`src/BoardPane.cpp` ~5516, `new QCheckBox(label, …)`) already show bare words — no change there.
- The text filter already understands `label:<name>` (`src/BoardModel.cpp` `matches()`, term starting with `label:`), so copying `label:bug` pastes into the filter box and works.
- Test: `tests/boardmodel_test.cpp` `hashtagClicksCopyAndCardRefsZoom()` (~1878) pins the current copy-the-hashtag behaviour and must be updated.
- `docs/SWITCHBOARD-DESIGN.md` mentions labels (rows, badges) but not the hashtag spelling; check the `#3ZAP` comments it may echo and update wording that says "hashtag".

**Steps.**

1. `src/BoardPane.cpp` meta row (~6433): render labels as bare words, keeping the `tag://` anchors so they still copy on click.
2. Click handler (~7880–7889): copy `label:<name>` instead of `#<name>`; update the toast/comment wording accordingly.
3. Body-text linkifier (~3416): stop linkifying `#tag` as a label. `#` followed by a code that names a card on this board still zooms; `#word` that is only a label becomes plain text (decided: plain text, not a clickable label).
4. Row badge click-copy (~1183/1279): copies `label:<name>`; update the four `#3ZAP` comments that say "hashtag".
5. Update `tests/boardmodel_test.cpp` `hashtagClicksCopyAndCardRefsZoom()` for the new copy text and the de-linkified `#tag`.
6. Sweep comments in `src/BoardPane.cpp` / `src/BoardPane.h` that call labels "hashtags", and any wording in `docs/SWITCHBOARD-DESIGN.md`.

**Risks / decisions.**

- Existing card bodies and threads contain `#tag` text written under the old behaviour; after step 3 those render as plain text, which is the intent, but old copied `#tag` strings in the wild will no longer be recognised — accepted, they were only ever clipboard copies.
- Do not touch `#ID` handling anywhere: `BoardModel::matches` `#`-term filter, `Card::reference()`, and the drag mime (`#` + id at ~1392) are card codes and stay.

**Verify.**

- `scripts/relay-build`, then `ctest --test-dir build -R boardmodel`.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: open a card with labels; the meta row shows bare words, clicking a badge/meta label puts `label:<name>` on the clipboard, pasting it into the filter box filters, and a `#tag` in a body no longer renders as a link while `#<a real card id>` still zooms.

## Decisions
- "dropping is OK" — labels render as bare words everywhere; no replacement symbol. Label clicks copy `label:<name>`; `#` stays reserved for card codes.

## Tasks
- [x] Render bare labels and copy label: terms; preserve card references. <!-- t:a1 -->
- [x] Verify document links, clipboard, filtering and live widgets. <!-- t:a2 -->
- [x] Land evidence and hand off for verification. <!-- t:a3 -->

## Execution Summary
Bare labels now copy `label:<name>`. Automatic body/thread links are restricted to known card references; explicit Markdown links keep their destination. Card IDs have a separate copy path preserving `#ID` for row/header buttons and the keyboard shortcut. Updated regressions and Switchboard design documentation.

Evidence: `docs/qa_evidence/2026-09-21-s53z-labels/NOTES.md` (logs and screenshot alongside).

## Tests
- `ctest -R ^board$`
- manual: docs/qa_evidence/2026-09-21-s53z-labels/NOTES.md

### Check 2026-09-21 21:37
- passed · ctest:board — ctest -R board passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
- not-applicable · manual:docs/qa_evidence/2026-09-21-s53z-labels/NOTES.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-s53z-labels/NOTES.md
- notice · ctest:board — ctest -R board is slow: p95 2.58 s, p50 0.93 s
history: thread
## QA checklist
- [ ] Labels in the list, filter chips and card meta row show bare words.
- [ ] Click a row badge and meta label; each copies `label:<name>` and shows matching feedback. Paste into the filter and confirm matching cards only.
- [ ] Body/thread `#bug` and unknown `#ZZZZ` remain plain text; known `#ID` links open that card.
- [ ] Explicit Markdown links and code keep their original meaning.
- [ ] Row/header card-copy buttons and `y` still copy `#ID`.

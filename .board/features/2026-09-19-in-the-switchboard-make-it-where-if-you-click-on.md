---
id: 3ZAP
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
priority: 1
rank: zzzzzzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [d8de5b77, 6127a540], evidence: [docs/qa_evidence/2026-09-20-switchboard-hashtag-copy/], related: [], github: null}
---
# in the switchboard, make it where if you click on hash tags, it copies them to c…

## Issue
in the switchboard, make it where if you click on hash tags, it copies them to clipboard

## Plan
**Goal** — Clicking a hashtag copies it to the clipboard (`#bug`) with a "Copied #bug" toast **only on label surfaces**: the list-row badges, the card-top meta labels, and label hashtags in the card detail body/thread. A `#K7Q2`-style card reference in an issue/thread/plan instead becomes a link that **zooms to that card** (no copy). Hashtag links in a terminal pane keep their current behaviour (navigate to the switchboard, no copy) — unchanged. (Owner decision, 2026-09-20.)

**Findings**
- The card detail is `CardDetail` (anonymous-namespace class at `src/BoardPane.cpp:1183`). Its `m_doc` is a `QTextBrowser` named `boardCardDocument` with `setOpenLinks(false)` (lines 1276–1278). Body is rendered with `doc->setMarkdown(...)` (line 2270); thread lines go through `insertLine` (declared 2204, `cursor.insertText` at 2211). No anchors exist in body/thread today.
- Anchor char-format precedent in the same document: thread pane links already set `setAnchor(true)` / `setAnchorHref("relay-pane:" + token)` at lines 2429–2430 — the linkify helper follows that shape.
- The click handler is the `m_doc` `anchorClicked` lambda at lines 1473ff: non-`file` schemes fall through to `QDesktopServices::openUrl`. New schemes (`tag:`, `card:`) are intercepted before that fallthrough.
- `card:` scheme precedent: the cleanup panel emits `<a href="card:K7Q2">` (line 5528) and handles it at lines 3100ff, extracting the id as `url.path().isEmpty() ? url.host() : url.path()`; the zoom target is the existing open-card path the cleanup panel uses.
- Copy-plus-toast precedent: `BoardView::copyReference()` (lines 4848 area) does `QApplication::clipboard()->setText("#" + id)` then `showNotice("Copied #%1")`. `<QClipboard>` already included.
- The card header meta is built by `metaText(...)` (line 2167; labels row added at 2185) and already emits `<a href=... style="color:%2">` anchors (line 2216) — labels become `tag:` anchors there.
- List-row label badges are painted by the row delegate; a badge click currently selects the row. The delegate needs hit-testing (or stored badge rects) so a click landing on a badge copies instead of selecting.
- Card-id shape: four base32-ish characters (`#K7Q2`). Distinguish refs from labels by regex `#[A-Za-z0-9]{4}\b` **plus** the id existing in the model; anything else matching `#[A-Za-z0-9][A-Za-z0-9_-]*` is a label tag.
- Terminal-pane hashtag links live in `src/OutputLinks.cpp` and already navigate to the switchboard — out of scope, no change.

**Steps**
1. In `CardDetail`, add a linkify helper that finds `#[A-Za-z0-9][A-Za-z0-9_-]*` in a text range and overlays an anchor char format (following the `relay-pane:` precedent at 2429): if the word is an existing card id → `setAnchorHref("card:" + id)`; otherwise → `setAnchorHref("tag:" + word)` with underline and the pane's link colour. Skip ranges that already carry a non-empty `anchorHref()` (markdown links) and fenced code blocks (`QTextFormat::BlockCodeLanguage` / monospace format), so `[#bug](http://x)` and `#include` stay untouched.
2. Apply the helper (a) after `doc->setMarkdown(...)` (line 2270) by scanning the body blocks with `QTextCursor::setCharFormat`, and (b) inside `insertLine` (2211) by splitting the line on the regex — covering existing, appended, and streamed thread entries in issue, thread, and plan text alike.
3. In the `m_doc` `anchorClicked` lambda (1473), intercept before the `openUrl` fallthrough: `tag:` → copy `"#" + tag` and `showNotice("Copied #%1")` (same wording as `copyReference()`); `card:` → zoom to that card via the same open-card call the cleanup panel uses.
4. `metaText` (2167): render each label as `<a href="tag:<label>">#<label></a>` so the card header's labels copy on click.
5. Row delegate: hit-test clicks against the painted label-badge rects; a badge click copies `#<label>` + toast instead of selecting the row; clicks elsewhere keep selecting. Filter chips and `BoardChat` stay as they are.
6. No shortcut-hint registry entry (WARP.md rule): click affordance, not a keyboard fast path.

**Open question (minor)**
- If a 4-char tag matches a card id that does *not* exist on the board, it is treated as a label (copy). Recommendation: yes, existence check wins over shape.

**Verify**
- New tests in `tests/boardmodel_test.cpp`, beside the other `boardCardDocument` tests (e.g. lines 1351, 1476, 1996): a card whose body and a thread comment contain `#bug` and a ref to an existing `#K7Q2` — assert a `tag:bug` anchor and a `card:K7Q2` anchor exist; `Q_EMIT` `m_doc->anchorClicked(QUrl("tag:bug"))` (emit precedent: `tests/filepanes_test.cpp:218`) and assert clipboard == `"#bug"`; emit `card:K7Q2` and assert the clipboard is unchanged and the other card opened. Assert `[#bug](http://x)` keeps its `http` href and fenced `#include` gets no anchor.
- Build with `scripts/relay-build`, then `ctest --test-dir build -R boardmodel`.
- Live check under Xvfb with an isolated `XDG_CONFIG_HOME`: click a list-row badge, a card-header label, and a body hashtag — each shows the "Copied #…" toast and pastes correctly; click a `#K7Q2` ref in a thread and confirm it zooms to that card without touching the clipboard.

## QA checklist
- [ ] `ctest --test-dir <build> -R "^board$"` green on a tree without other sessions' WIP — on the shared checkout it currently hangs inside #CYM9's uncommitted delete-confirm feature (reproduced there in a baseline worktree *without* this change; tip+this-change is 77/77, see `logs/exact-tree-ctest.txt`).
- [ ] `hashtagClicksCopyAndCardRefsZoom` on its own (`./relay-board-tests hashtagClicksCopyAndCardRefsZoom`): the `tag:`/`card:` anchors, the untouched `http` link, the bare fence, clipboard + notice on a tag click, zoom without clipboard on a card ref, badge copy without selection, title click still selects.
- [ ] Live (`drive.sh` re-runs it end to end): a list-row label badge, the card meta's `#label`, a body `#tag` and a thread `#tag` each copy with the "Copied #…" toast; `[#bug](http://x)` stays an http link; fenced `#include` gets no anchor; a `#ID` ref zooms to the card without touching the clipboard.
- [ ] A four-character tag that names no card (`#ZZZZ`) copies as a label — the board's existence check wins over shape.
- [ ] Hashtag links in a terminal pane still just navigate to the Switchboard (no copy) — `src/OutputLinks.cpp` untouched.

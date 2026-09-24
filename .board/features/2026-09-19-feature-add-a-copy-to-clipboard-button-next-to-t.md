---
id: YQC3
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
priority: 2
rank: zzzzzzzzzzzzz
created: '2026-09-19'
links: {plans: [], commits: [2fd7fb69, fb782c01], evidence: [docs/qa_evidence/2026-09-20-copy-session-id-info-pane/], related: [], github: null}
---
# feature: add a "copy to clipboard" button next to the session id in the pane inf…

## Issue
feature: add a "copy to clipboard" button next to the session id in the pane info

## Plan
**Goal.** In the ⓘ conversation-info pane (`InfoView`), put a one-click copy affordance next to the session id in the Session row — the standard overlapping-boxes copy icon — and make the id text itself a copy link too, so either click copies it. Same for the thread id on a thread page (same helper).

**Findings.**

- The pane is `src/SessionInfo.{h,cpp}` (`relay::sessioninfo::InfoView`); its body is a `QTextBrowser` filled with HTML from the pure `renderInfo()` (`sessionHtml()` / `threadHtml()`).
- The Session row is built in `sessionHtml()` (`src/SessionInfo.cpp:147-156`): `<code>%1</code>` with the `session_id`, then the session file link. The Thread row is the matching `idCell` in `threadHtml()` (`src/SessionInfo.cpp:253-255`).
- In-body actions are `relay-info:` anchors made by the `link()` helper (~line 21, percent-encodes values, drops empty ones) and dispatched in `InfoView::linkActivated()` (~line 472), which already handles `thread`, `session`, `live`, `file`. `linkQuery()` decodes a link's query and is exported for tests.
- Clipboard precedent: `BoardPane.cpp:4722-4725` — `QApplication::clipboard()->setText(...)` plus a transient "Copied …" notice. `InfoView` has no notice system, but it does own the bottom hint label (`dialogHint`, built inline in the constructor: "Links open subagent threads · Alt+Left back · …").
- Unit tests for this pane live in `tests/conversations_test.cpp` (`infoRendersSessionAndThread`, `infoViewNavigatesAndGoesBack`).

**Steps.**

1. `src/SessionInfo.cpp`, `sessionHtml()`: build the copy href once — `link(QStringLiteral("copy"), {{QStringLiteral("text"), sessionId}, {QStringLiteral("what"), QStringLiteral("session id")}})` — and use it twice in `sessionCell`: wrapping the `<code>` id as the anchor's label (so clicking the id itself copies), and again after it with the ⧉ glyph as label, styled small and muted (`<span style=…>⧉</span>` inside the anchor) to read as an icon. `link()` already drops empty values, so an empty id renders neither anchor nor icon.
2. Same two anchors in `threadHtml()` for `thread_id` in the Thread row (`what` = "thread id") — owner confirmed yes.
3. `InfoView::linkActivated()`: add `else if (what == QLatin1String("copy")) { QApplication::clipboard()->setText(value("text")); flashHint(QStringLiteral("Copied %1 to the clipboard").arg(value("what"))); }`.
4. Feedback: promote the constructor's inline hint `QLabel` to a member (`m_hint`), keep its standing text as a constant, and add a private `flashHint(const QString &)` that shows the message and restores the standing text on a single-shot `QTimer` (~2 s). Include `<QTimer>`.
5. Icon: ⧉ is U+29C9 ("two joined squares"), the standard overlapping-boxes copy sign. Verify under Xvfb that the default UI font actually draws it; if it tofu-boxes, fall back to `QIcon::fromTheme(QStringLiteral("edit-copy"))` rendered into a data-URL PNG embedded in the HTML (same anchor either way).
6. `src/SessionInfo.h`: the header comment enumerates the link schemes — add `copy?text=` to the list.
7. `docs/ARCHITECTURE.md` (~line 1610, the ⓘ pane section): add half a sentence that the Session/Thread id and its copy icon copy to the clipboard, if that paragraph enumerates the view's affordances. No protocol change (`docs/AGENT-SESSIONS-PROTOCOL.md` untouched — no new worker message).
8. `tests/conversations_test.cpp`:
   - In `infoRendersSessionAndThread`, assert the session HTML carries the `relay-info:copy?text=` href on both the id and the icon anchors and that `linkQuery()` decodes it back to the exact id; same for the thread page's `thread_id`.
   - Extend `infoViewNavigatesAndGoesBack` (or a sibling slot): after `setInfo(...)`, `QMetaObject::invokeMethod(body, "anchorClicked", Q_ARG(QUrl, QUrl("relay-info:copy?text=abc&what=session%20id")))` and assert `QApplication::clipboard()->text() == "abc"` and that the hint label shows the copied text.

**Risks.**

- The "button" is the ⧉ link-styled anchor, not a painted control: the body is a `QTextBrowser`, and the pane's established pattern for in-body actions is a `relay-info:` anchor. A real overlay widget would be disproportionate. The ⧉ glyph depends on the font (step 5's fallback covers a tofu box).
- Clicking the id now copies instead of just placing the cursor; drag-selection of the id text still works in `QTextBrowser`, so the id can still be selected by hand if wanted.
- Clipboard assertions need the offscreen platform the suite already runs under; if the runner lacks a clipboard, keep the wiring test to the hint flash and the pure-HTML assertions.
- No shortcut-hint entry: the standing rule covers keyboard fast paths, and this adds a mouse affordance, so there is no clean trigger to hook. Flag it here if you disagree.

**Verify.**

1. `scripts/relay-build`.
2. `ctest --test-dir build -R conversations` (the ⓘ tests ride in `conversations_test.cpp`).
3. Live check under Xvfb with an isolated `XDG_CONFIG_HOME`: open a pane's ⓘ view (Alt+I), click the ⧉ icon by the Session row and also the id itself, paste somewhere — the full id, not a truncated one, both ways; the ⧉ renders as the overlapping-boxes icon, not a tofu box; the bottom line flashes "Copied…" and returns to its standing text; the thread page works the same; a session with no id yet renders no dead link.
4. Land through `python3 scripts/land.py begin` / `commit` as usual, and move the card to `needs-verification` with the QA evidence path.

## Decisions
- 2026-09-20 (owner): thread page's Thread id gets the copy link too — "yes."
- 2026-09-20 (owner): the affordance is the standard overlapping-boxes copy icon, not the word "copy" — "it should be the standard overlapping boxes copy icon."
- 2026-09-20 (owner): clicking the session id itself also copies — "but clicking on the session id itself also copuies"

## QA checklist
- [ ] `scripts/relay-build` clean; `ctest --test-dir build -R conversations` passes — both ⓘ tests extended (the shared `relay-info:copy` href on the id and the ⧉ anchors, the percent-decode round-trip, the empty-id no-link case, and `anchorClicked` → clipboard → hint flash).
- [ ] Live under Xvfb (isolated config, stub provider): clicking the id text and clicking the ⧉ glyph itself each put the **exact** 32-hex session id on the clipboard, byte-compared against the saved session file (`docs/qa_evidence/2026-09-20-copy-session-id-info-pane/`, scenes 02–05).
- [ ] The bottom hint line flashes "Copied session id to the clipboard" and is back to its standing text ~2 s later (shots 03–05).
- [ ] The thread page's Thread row carries the same pair; clicking its ⧉ copies that thread's id (byte-compared with the saved `<session>.threads/<thread>.json`) and flashes "Copied thread id …" (shots 06–07).
- [ ] The ⧉ renders as two joined squares, not a tofu box — zoom crop `implementer-02-id-zoom.png`; fontconfig resolves U+29C9 to FreeSerif, which covers the codepoint, so no fallback was needed.
- [ ] A session with no id renders no copy anchor or icon — unit-tested; live sessions always have an id (see the evidence README's second note).

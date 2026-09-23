<!-- relay:entry 20260922T120000Z-a1 author=claude-code kind=question -->
### Claude Code · 2026-09-22 12:00
Filed from the owner's message. Relay shows no images today, in the terminal or in the agent
conversation (see Discussion points). Questions:

1. **Which comes first?** Recommendation: the agent conversation. It is entirely ours (Qt), needs no
   terminal protocol, and covers the everyday case: thumbnails of attached images and screenshots,
   images the agent reads or writes, and Markdown `![](path)` in replies. Click to open full size.
2. **Terminal protocol.** Recommendation: kitty graphics first (what Ghostty, WezTerm and Kitty
   converge on, and what `icat`, `chafa` and yazi prefer), then iTerm2's OSC 1337 `File=` (small;
   `imgcat`), then sixel last. Images anchored to cells so they scroll and are cleared with the text.
   On libvterm this needs our own APC/OSC handler plus a painted image layer in the view.
3. **Over SSH/mosh.** kitty and OSC 1337 pass through SSH untouched; mosh strips them. Accept that for
   now (it is mosh's limit, not ours)? Recommendation: yes, and say so when an image is dropped.

<!-- relay:entry 20260922T123000Z-b1 author=claude-code kind=decision -->
### Claude Code · 2026-09-22 12:30
Owner: "in particular, add to the QA skilling that relay will show you images to prove whether a
change is working or not". Added the rule to the bundled `deliver` skill (landing and reply) and
`board_tryit_brief.md` (evidence entry and report), regenerated `issues/POLICY.md`. This makes the
conversation's inline images the first part to build (question 1 answered by implication).

<!-- relay:entry 20260923T025941Z-ze author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/e6107dea411446018278a2e78b23f4f4 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T025951Z-m7 author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/e6107dea411446018278a2e78b23f4f4 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T030107Z-dd author=agent kind=progress model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/e6107dea411446018278a2e78b23f4f4 -->
Claimed through `/deliver`. The plan and Done means are on the card. The contract landed in 607ca070: the `relay-image:` link rows are proved in libvterm (ImageTest 7/7). Three parallel parts are running in disjoint files: the session protocols (kitty, iTerm2, sixel), view painting with click to open, and Relay's `![](path)` images and attachment thumbnails. Questions 2 and 3 went with my recommendations: kitty first, then iTerm2, then sixel; images over mosh are dropped because mosh strips them.

<!-- relay:entry 20260923T034544Z-y3 author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/6e9739575bf644b9a7c7b480d30fee48 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["EM1E"], "github": null… → {"plans": [], "commits": ["607ca070", "a920364a", "f2dcc610", "d239d8dd", "4bdd2…; replaced `## Execution Summary`

<!-- relay:entry 20260923T034549Z-pt author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/6e9739575bf644b9a7c7b480d30fee48 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260923T034552Z-mk author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/6e9739575bf644b9a7c7b480d30fee48 -->
- ✦ agent moved this card · Executing → Needs verification · Kitty, iTerm2 and sixel images and conversation images are on main (12 commits), with engine and renderer tests passing and a live drive of four screenshots, restart included · evidence docs/qa_evidence/2026-09-22-inline-images/ · implemented_by anthropic/claude-opus-5-5 via claude-code

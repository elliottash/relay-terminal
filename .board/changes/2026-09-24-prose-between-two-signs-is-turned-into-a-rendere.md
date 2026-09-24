---
id: N6Y3
type: work
status: needs-verification
labels: [bug, media]
assignee: agent
implemented_by: glm/glm-5.3
session: 0806c7fd-6b22-4d1d-ac66-ec3e1ea8f5bb
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'pane 0806c7fd, 2026-09-24 (found while investigating #J0VY)'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/], related: [], github: null}
---
# Prose between two $ signs is turned into a rendered math block (false positive in MarkdownAnsi inline math)

## Issue
Investigating the "equation is making relay run slow" report turned up a false positive in the inline-math detection: ordinary prose containing two dollar signs was converted to a math manifest and rendered through the LaTeX toolchain. Manifest ~/.cache/relay/media/db61b044….json holds {"kind":"math","latex":"1,500–2,300. A minimal version costs about "} (written 2026-09-24 10:10 EDT, rendered to 37b918ed…-math.png, 8.2 KB). Two $ in one line of prose (e.g. price ranges) should not become a math block.
`MarkdownAnsi`'s inline-math detection (src/MarkdownAnsi.cpp, `$…$` handled in `inlineStep`, escaped via `mathMediaEscape`) fires on any two `$` characters in one line, including ordinary prose such as price ranges. Evidence on this machine: `~/.cache/relay/media/db61b044041b0d1e84526f1ae2cdc2dde8484c9730ad4cdae7e999f9cf0dc503.json` contains `{"kind":"math","latex":"1,500–2,300. A minimal version costs about "}` — a sentence fragment that was displayed as a 3-row rendered math block (PNG `37b918ed…-math.png`, 8.2 KB, written 2026-09-24 10:10 EDT) and paid a full `relay-render-math` run (latex → dvisvgm → rsvg-convert). Part of the #MDA7 inline-media work. Likely fix direction: require math-ish content between the `$…$` (at least one `\` command or `^`/`_`), no sentence punctuation/prose between the markers, and/or opening `$` not followed by a space / closing `$` not preceded by a space (CommonMark-adjacent heuristics).

## Done means
Single-`$` inline math only renders when it looks like math, using Pandoc's rules: no whitespace directly after the opening `$` or directly before the closing `$`, and the closing `$` not followed by a digit. So "$1,500–2,300. A minimal version costs about $500" stays plain text (no `relay-media:` escape, no LaTeX run), `$x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$` still renders, and display math `$$ … $$` is unchanged. Proved by new cases in `relay-markdown-tests`, whole and streamed.

## Execution Summary
Implemented as planned (commit `15f65489` on main): a `delimitsInlineMath(text, open, end)` gate in src/MarkdownAnsi.cpp beside `mathMediaEscape`, applied only to single-`$` runs. Rejected markers are emitted literally and scanning continues, so a later `$` on the same line can still open math. While streaming, the run is held for one character past the closing `$` so the digit rule is decided on real text (`finish()` accepts when the text ends there). Display `$$…$$` is exempt. Evidence: docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/ (commit `d303a90f`).

## Tests
- `relay-markdown-tests` slot `inlineMathMustLookLikeMath`: the prose false positive is rejected whole and streamed with both `$` surviving as text; `$5 and $10` rejected; `$x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$` accepted whole and streamed (streamed equal to whole); padded `$$ x = 1 $$` accepted. Suite green.
- `ctest --test-dir build -R 'markdown|appcommands'` 2/2 green on the landed code; land.py verify-built the exact tree before the swap.
- Evidence: docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/

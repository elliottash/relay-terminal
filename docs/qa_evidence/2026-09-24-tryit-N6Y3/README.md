# Try it for #N6Y3 — could not be staged

The judgement this card would hand a person — "is the price sentence plain text while the
equation is typeset math, in a rendered agent reply?" — needs a real agent reply rendered
through `MarkdownAnsi` in a pane. Every no-model seam was checked and none renders agent
markdown: `scripts/relay-show` is for local media files (image/svg/table/audio), and a
pane only runs `MarkdownAnsi` on model output, which `stage.sh` may not require.

What does cover the card without a person: `relay-markdown-tests` slot
`inlineMathMustLookLikeMath` (commit `15f65489`) asserts exactly the decision — the wild
false positive produces no `relay-media:` escape whole and streamed, both `$` survive as
text, `$5 and $10` produces none, the quadratic formula and padded `$$…$$` still do — and
the rendering of an escaped math row is #MDA7's own verified surface
(docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/ holds the run).

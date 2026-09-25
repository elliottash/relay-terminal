<!-- relay:entry 20260918T020525Z-qr author=agent kind=event model=kimi-k3 pane=9f34a70e turn=6bd65b745b704be8ae904ba8c396487e/62de50879e5a474fa6279791afe486e8 -->
- ✦ agent created this card in Ready · issues/changes/2026-09-17-terminalview-hot-paths-linkat-rebuild-per-hover.md

<!-- relay:entry 20260918T020632Z-4k author=agent kind=event model=kimi-k3 pane=9f34a70e turn=6bd65b745b704be8ae904ba8c396487e/62de50879e5a474fa6279791afe486e8 -->
- ✦ agent updated this card · appended to `## Findings`

<!-- relay:entry 20260920T180026Z-c4 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 18:00
Measured for #PF4K (docs/qa_evidence/2026-09-20-perf-profile/engine/FINDINGS.md, finding 6), spark and sphinxpad, libvterm core:
1. `linkAt()` per hover cell is real but small: 0.141 ms and 4–5 syscalls per hovered cell. The same lookup run per *painted row* through `restLinkColumns()` is what costs (91–94 % of GUI-thread statx) — that is now card #6W0Z.
2. `colorsFor` twice per cell is below noise: 1.91 % + 1.44 % of GUI samples; with 15,695 live search matches a page-up costs 3.0–4.0 ms against 3.25–4.0 ms without.
3. `allText()` is on no hot path (guarded by `QAccessible::isActive()`); unmeasurable here without a screen reader.

<!-- relay:entry 20260924T042818Z-vr author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T042818Z-vs author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260924T043220Z-9t author=agent kind=event model=kimi-k3 pane=switchboard turn=b959100192fd164f54da103341e273c2/e559baf77ea34d838e7daec3bc07226a -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043309Z-fr author=agent kind=event model=kimi-k3 pane=switchboard turn=b959100192fd164f54da103341e273c2/e559baf77ea34d838e7daec3bc07226a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043323Z-6d author=agent kind=comment mode=plan model=kimi-k3 turn=b959100192fd164f54da103341e273c2/e559baf77ea34d838e7daec3bc07226a -->
One more check on the test surface before writing the plan.





I have the full picture. Writing `## Done means` first, then the plan.



**Planned #9MYY.** The plan adds a frame-version counter in `pullFrame` and keys three small caches on it: the hover `linkAt` logical-line + scan results (with a one-pass `reserve()` build), per-cell `CellColors` in `paintRow`, and the a11y `allText` string — verified by a counting-`setLinkProbe` test, the unchanged ViewTest link/hover suite, and pixel-identical `folds.sh` output.

One thing for the owner, flagged in Risks: #6W0Z's measurements show all three items are below noise today, so if you'd rather not spend the churn on items 2–3, the card can shrink to the `linkAt` fix alone — otherwise it's ready to Run.

<!-- relay:entry 20260924T043324Z-34 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T050113Z-p1 author=claude-code kind=note -->
### Claude Code · 2026-09-25 05:01
Re-checked against `8163b4ed`: all three hot paths are still in `engine/view/TerminalView.cpp` (`linkAt` 2120, `paintRow`/`colorsFor` 809/837, `TerminalAccessible::allText` 234). Nothing has fixed them since the plan of 2026-09-24. I replaced `## Plan` because it had gone stale:

- Line numbers are updated.
- The "O(line²) appends" claim is corrected. `QString +=` grows amortised, so the build is O(L) with one temporary per cell.
- Two per-hover-cell costs the old plan missed are added:
  - `linkAt` still calls the uncached `currentDirectory()` (2312, 2176): a readlink plus the core mutex. #6W0Z moved only `restLinkColumns` to `frameDirectory()`.
  - It calls `withCore → hyperlinkAt` (2222), another mutex acquisition on every hover cell.
- `links::scan` probes every candidate on the line, not only the one under the pointer.
- The a11y cost is O(viewport) per character query, via `characterRect`/`offsetAtPoint` `split`.
- The test command is corrected. There is no `ViewTest` ctest; it is `relay-engine-tests`, with `RELAY_ENGINE_TEST=ViewTest` to run one object.
- `folds.sh` (screenshots checked by eye) is replaced with an env-gated golden-grab test whose before and after images are compared with `cmp`.
- A tests-first commit is added: a probe-count test plus env-gated `QBENCHMARK` slots, so before/after numbers come from the same test source.

Also proposed a `verify` block (script primary, pairwise also, human optional, effort low). `## Done means` is unchanged. Its item 2 still names `folds.sh`; the plan's golden-grab test is the stricter check of the same claim. There are two owner questions under Risks: whether to keep items 2–3, and whether to keep the golden and bench slots in ViewTest permanently. Status is unchanged (planned).

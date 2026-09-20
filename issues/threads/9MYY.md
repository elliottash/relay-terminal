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

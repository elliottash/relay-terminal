---
id: WMX7
type: work
status: planned
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# panestatus tests fail on main: band hues from #MXMG

## Issue
Unrelated fault noticed while landing #W6ES: ctest -R panestatus fails on current main (built at 2392a62c6155) in PaneStatusTests::whoGetsABand and ::byTypeDiffersByGroupShares — colour-fill mismatches (`#ff27241e` vs `#ff302b22`) and byGroup.size() 2 vs 1, i.e. the band/hue logic of #MXMG (a605fe5e "Board vs Sessions & Projects: carry each pane's hue past its band"), not the tab-drag change. PaneStatus/Theme sources are unmodified in the tree; the failure is in the committed code.

## Done means
`ctest --test-dir build -R panestatus` is green on a clean main, with `whoGetsABand` and `byTypeDiffersByGroupShares` passing **unchanged** — their invariants stand: in ByGroup every tool pane shares one fill, and in Off all bands are equally neutral. The #MXMG look survives where it was asked for: Board and Sessions & Projects keep their stronger band in ByType mode, where the two panes wear distinct hues. Failure would look like either test still red on main, or the fix bought green by editing the tests instead of the code.

## Plan
**Goal.** Make `PaneStatusTests::whoGetsABand` and `::byTypeDiffersByGroupShares` pass on main again by fixing `typeStyle()`'s strength logic, not the tests: the #MXMG notch is a ByType device and must not split the ByGroup fill.

**Findings.**
- `src/PaneStatus.cpp:355-359` (#MXMG, a605fe5e): `listPane` adds 0.05 to the tint strength of `board` and `sessions` in **every** non-Off mode. But `typeStyle()` resolves the hue *before* that (line ~345): in ByGroup, board and sessions take `tokens.tool` like every tool pane, so the group's band splits into brass at 0.18 (board, sessions) vs brass at 0.13 (options, actions, testsuites, …).
- That is exactly both failures: `tests/panestatus_test.cpp:381` compares testsuites' fill with board's in ByGroup (`#ff27241e` at 0.13 vs `#ff302b22` at 0.18), and `:398` sees `byGroup.size()` 2 where the test asserts the group invariant is 1. ByType and Off are unaffected — their assertions pass.
- The tests encode the documented rule in the same function's comment ("By group: every tool pane brass, every agent pane violet"); the MXMG card (#MXMG Decisions) asked for a stronger band where the two lists are told apart by their *own hues*, which only ByType gives them.

**Steps.**
1. `python3 scripts/land.py begin <me> src/PaneStatus.cpp`.
2. In `typeStyle()` (src/PaneStatus.cpp:355), gate the notch to the mode that carries the pane's own hue: `const bool listPane = mode == ColourMode::ByType && (kind.type == "board" || kind.type == "sessions");`. Extend the `#MXMG` comment above it: the notch rides the pane's own hue, so it exists only in ByType; ByGroup keeps one fill per group, Off stays neutral.
3. Do not touch the tests.
4. `python3 scripts/land.py commit <me> -m "PaneStatus: MXMG band notch is ByType-only; ByGroup stays one fill per group (fixes #WMX7)"` — the build gate compiles the landed tree itself.

**Risks.**
- *Owner question:* if the stronger band was actually wanted in ByGroup too (two brass strengths within the tool group), the tests' group invariant — not the code — is what changes; that is a design call, so say so before Run and the plan flips to editing the tests instead. Default here is code-fix, since ByGroup's whole point is that tool panes are indistinguishable.
- Visual only: ByGroup-mode board/sessions bands drop back to the generic 13 %/10 % tint. No other surface reads that strength (remote/ssh bands come from `remoteStyle()`, untouched).

**Verify.**
- `scripts/relay-build --target relay-panestatus-tests && ctest --test-dir build -R panestatus` — all 18+ slots green, the two named tests among them.
- Spot check that the #MXMG look survives: `whoGetsABand`'s ByType assertions (board `#ff302b22`-strength band, sessions the shell hue at the same strength) still pass unchanged.
- Rebuild on current main after the commit (`ctest --test-dir build -R panestatus` once more) so "green on main" is what is claimed, not green on a dirty tree.

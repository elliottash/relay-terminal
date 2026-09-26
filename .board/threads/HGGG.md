<!-- relay:entry 20260926T043449Z-vs author=agent kind=event model=kimi-for-coding-highspeed pane=14119fb1 turn=6a5b30a50b8f4918a5c207e1711a4232/f56fb816c09743e39814456dfa432d71 -->
- ✦ agent created this card in Inbox · .board/features/2026-09-26-improve-the-appearance-of-the-usage-window.md

<!-- relay:entry 20260926T043502Z-k1 author=agent kind=note model=kimi-for-coding-highspeed pane=14119fb1 turn=6a5b30a50b8f4918a5c207e1711a4232/f56fb816c09743e39814456dfa432d71 -->
Where it is now: `relay::UsageChartDialog` / `UsageChartView` in `src/RelayWindowModels.cpp` (added by #62TG, commit `b5e2624b0a15`) — the `usage…` button on the Models pane's `models.refreshUsage` row opens it.

What could be improved, from the code and the evidence shot (`docs/qa_evidence/2026-09-25-62TG/03-chart-only.png`):

1. The dialog is a fixed 960 px with a full paragraph of explanation at the top; the formula reads better as a short legend beside the bars (or a `?` popup) than as prose over the chart.
2. Rows are fixed height (20 + 34 px per window + 24) whatever the window count, so an account with one window (weekly only) leaves a hole, and the bar/text columns do not line up between rows of different window counts.
3. The bars are theme-agnostic green/amber/red with no legend and no percentage inside the track; a muted "left" track and the figure inside the bar would read better at a glance.
4. No tier choice: it always charts the `main` list's rank 1. A tier selector (main / high / flash) would answer "what would /flash draw" too.
5. No link from a row back to its account in the Sources list, and no hover text (the rows' tooltips carry the long form, the chart has none).
6. Sorting is list order; an option to sort by draw share or by time-to-reset would make the "who is nearly out" reading immediate.

<!-- relay:entry 20260926T043502Z-v7 author=agent kind=event model=kimi-for-coding-highspeed pane=14119fb1 turn=6a5b30a50b8f4918a5c207e1711a4232/f56fb816c09743e39814456dfa432d71 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

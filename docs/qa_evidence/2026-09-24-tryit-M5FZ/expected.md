# Expected (sealed — the Try it compares against this, never the card)

Clicking the finished "notes QA sweep" row in the subagents strip opens its tab already
formatted: one concise line per landed tool call — a `read notes.md` row and a `grep` row, the
same folded style the pane's own agent shows while running — then the subagent's report text,
then the live marker. No `{"ok": true, ...}` / `{"lines": [...]}` JSON dump appears anywhere in
the first paint. Clicking a tool row still unfolds the raw result. A call that never landed
would keep its `⚙ name (pending)` line.

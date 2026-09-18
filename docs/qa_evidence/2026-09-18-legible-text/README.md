# Legible text: implementer evidence

Card [#N50J](../../../issues/changes/needs_qa_llm/2026-09-18-legible-text.md). Implementer screenshots
(Claude Opus 5), not QA verdicts. 1440×900 under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, taken by `drive.sh` with the same steps for both builds:

    docs/qa_evidence/2026-09-18-legible-text/drive.sh <relay-binary> <prefix> [relay-dark relay-light]

No provider account: the model is a local endpoint pointed at the subagents card's
`../2026-09-18-subagents-tabbed-pane/fake-provider.py` on 127.0.0.1, which starts three background
subagents that each run one command. The prompt runs in a tab that is then put in the background,
so its end is notified; back on it, the subagents strip opens the subagent pane, then the
Switchboard and Options (scrolled to Diagnostics) open beside it, and the bell is clicked.

`implementer-before-*` is the tree before this change (the same build dir, built from the working
tree just before the first edit); `implementer-after-*` is with it.

| File | Shows |
|---|---|
| `implementer-{before,after}-relay-dark-layout.png` | Terminal with agent notes, the subagent pane on a tab with its transcript, the Switchboard, Options — Relay Dark |
| `implementer-{before,after}-relay-light-layout.png` | The same in Relay Light |
| `implementer-{before,after}-relay-{dark,light}-bell.png` | The notification list over the same layout; the bell's count badge |
| `implementer-crop-terminal-notes-dark-before-over-after.png` | 100% crop, before above after: the terminal's agent notes lose their italic; the subagent transcript's `── live ──` too |
| `implementer-crop-subagent-transcript-light-before-over-after.png` | 100% crop, Relay Light: the subagent's prose was Relay Dark's near-white (1.2:1), now the theme's text (16.3:1) |
| `implementer-crop-options-log-detail-dark-before-over-after.png` | 100% crop: "Log detail" one word per line, and after |
| `implementer-crop-bands-and-switchboard-dark-before-over-after.png` | 100% crop: the pane bands' 8pt mono caps against the new sentence-case title weight; the Switchboard's 8pt tools at 9pt |

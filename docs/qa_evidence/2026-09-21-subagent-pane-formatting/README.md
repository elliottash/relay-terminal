# Implementer evidence — #S8FT

The subagent transcript uses `calllines::MarkdownStream`, the same Markdown renderer used by regular panes. Prose keeps settled rows while replacing only its streaming tail. Reasoning uses muted Markdown folds, with a six-row streaming preview, elapsed-time labels, and the existing collapse/always/never preference. Concise tool rows retain their folds and diff styling.

Validation:

- `scripts/relay-build --target relay-subagents-tests relay`
- `ctest --test-dir build -R '^subagents$' --output-on-failure`: 29 Qt test cases pass.
- Xvfb with a temporary `XDG_CONFIG_HOME`, `QT_QPA_PLATFORM=xcb`, and `RELAY_SUBAGENT_SCREENSHOT=<absolute evidence path>/implementer.png`: `build/relay-subagents-tests transcriptMarkdownAndThinkingMatchPane`.
- The widget test exercises split Markdown markers, styled text, live thinking refresh, collapse/reopen, explicit user folding, always/never preferences, and opening an earlier fold while prose continues. Assertions check that the answer appears exactly once. Existing tests cover tool results/diffs, snapshots, messaging, and restored tabs.

`implementer.png` is the live Qt transcript widget after those interactions, with the application theme applied. This is implementer verification, not independent QA. Historical reasoning is unavailable in the existing worker snapshot; these folds display the thinking events received while subscribed. Saved restart transcripts retain the existing plain-text format.

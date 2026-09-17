# Show agent output live while a program owns the terminal

- **Status**: needs-qa-llm
- **Component**: gui
- **Workstream**: agent
- **Milestone**: desktop-alpha
- **Assignee**: implemented by Claude Opus 5 (Claude Code session, pane UX subagent), 2026-09-17
- **Acceptance evidence**: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
- **Source**: owner report 2026-09-17 ("i opened vim, went into agent mode, and said 'type something interesting', but nothing happened"); owner approved the fix

## Behavior as implemented

- Inline agent output is still buffered while a foreground program owns the terminal (printing over vim would corrupt its screen).
- Buffered output now also appears live in a transcript panel above the prompt: header "Agent · <model> — output will also print in the terminal when <program> exits", Relay's ink colors, auto-scroll, at most ~40% of the pane height.
- × hides the panel until the program exits. When the program exits, the buffered output prints into the terminal as before and the panel clears and hides.
- The panel never shows when nothing is buffered.

## Implementer check (not a QA verdict)

Xvfb, isolated config, Kimi K3: `vim scratch.txt`, Ctrl+Shift+H, Ctrl+Enter "type something interesting". The reply streamed into the panel while vim stayed intact; after Ctrl+H and `:q!` the same text printed in the terminal and the panel disappeared.
Evidence: `docs/qa_evidence/2026-09-17-agent-output-while-program-runs/implementer-overlay-and-flush.png`.

## QA checklist

1. Same repro with GLM-5.3 and DeepSeek; long replies scroll inside the panel.
2. Tool calls during the turn (e.g. ask it to `ls`) appear in the panel with tool colors.
3. × hides the panel; it does not reappear until the program exits; output still prints afterward.
4. Split panes: the panel appears only in the pane whose agent is replying.

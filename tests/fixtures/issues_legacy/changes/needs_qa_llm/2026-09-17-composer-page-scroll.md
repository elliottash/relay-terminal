# PageUp / PageDown scroll the terminal from the prompt box

- **Status**: needs-qa-llm
- **Component**: gui
- **Workstream**: terminal
- **Milestone**: desktop-alpha
- **Assignee**: implemented by Claude Opus 5 (Claude Code session, pane UX subagent), 2026-09-17
- **Acceptance evidence**: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
- **Source**: `issues/feature_intake.txt` ("check that page up and page down scroll the terminal window up and down")

## Findings and behavior

- Before: in the prompt box PageUp/PageDown moved the cursor inside the few-line editor; in the terminal Konsole scrolls with Shift+PageUp/PageDown and passes plain PageUp to the shell or program.
- Now: plain PageUp/PageDown in the prompt box scroll the pane's terminal scrollback by a page, through Konsole's (hidden) vertical scrollbar. Terminal-focused keys are unchanged: Shift+PageUp/PageDown still scroll, plain PageUp still reaches programs such as less and vim.

## Implementer check (not a QA verdict)

Xvfb: after `seq 1 500`, two PageUp presses in the prompt box scrolled the terminal to lines 423–449.
Evidence: `docs/qa_evidence/2026-09-17-composer-page-scroll/implementer-pageup-scrolled.png`.

## QA checklist

1. PageDown returns to the bottom; typing a new command still works.
2. Inside `less` with human control, PageUp pages within less.
3. Shift+PageUp in native terminal input scrolls as in Konsole.

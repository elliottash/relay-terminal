<!-- relay:entry 20260922T170739Z-qq author=agent kind=evidence model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/4efd7519196e41f2b6eef7086c3e07a9 -->
Confirmed the reported missing upper gap in a real Bash PTY: delayed background output, followed by a Done notification during staged Readline redisplay, immediately precedes the next command. Lower gap survives. Reproduction: docs/qa_evidence/2026-09-22-background-command-spacing/reproduction.md. Investigation only; runtime fix remains open.

<!-- relay:entry 20260922T170739Z-qr author=agent kind=event model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/4efd7519196e41f2b6eef7086c3e07a9 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260922T172812Z-qj author=agent kind=progress model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/9bf900d3f32349f89dc5ccbc7308fb9a -->
Claimed via file fallback following the user's “fix it”. Applying the deliver workflow: add an ordered shell input-gap marker during command loading and conditionally supply missing blank rows in TerminalSession, after Bash's job notifications. Verify fragmented terminal input, existing gaps, screen edges, and a real Bash staged-command background-job reproduction.

<!-- relay:entry 20260922T172856Z-gv author=agent kind=event model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/9bf900d3f32349f89dc5ccbc7308fb9a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T173313Z-3t author=agent kind=event model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/9bf900d3f32349f89dc5ccbc7308fb9a -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T173313Z-9s author=agent kind=event model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/9bf900d3f32349f89dc5ccbc7308fb9a -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T173313Z-9t author=agent kind=event model=gpt-6-astra pane=9fbe8b36 turn=712bf57bf0464b0fa92f3c3cb4dae4e8/9bf900d3f32349f89dc5ccbc7308fb9a -->
- ✦ agent moved this card · Executing → Needs verification · Real Bash regression, 12 shell tests, 12 session checks and live isolated GUI confirm missing command gap is restored without duplicate gaps. · evidence docs/qa_evidence/2026-09-22-background-command-spacing/fix.md · implemented_by openai/gpt-6-astra via codex

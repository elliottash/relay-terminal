# Run unrecognized input in the terminal first, then fall back to the agent

- **Status**: done
- **Component**: gui, router
- **Milestone**: 0.1-preview
- **Workstream**: routing
- **Acceptance evidence**: a model QA session from a non-Claude family drives the built app
  and records the checks below under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5.5 (Claude Code session), 2026-09-16
- **Source**: `issues/feature_intake.txt`, "try something as a terminal command first, if it fails, run it as the agent"

## Behavior as implemented

- A **Terminal first** toolbar toggle, on by default, persisted in QSettings.
- In Auto mode, input the router marks `ambiguous` runs in the terminal.
- If Bash reports exit 127 (not found) or 126 (not executable), the same text is sent to
  the agent and the status bar says so.
- If the text is not valid Bash (`bash -n` fails), it goes straight to the agent without running.
- A recognized command that fails with any other exit code is not sent to the agent, because
  the agent cannot see terminal output. Widening this is a separate decision.
- With the toggle off, the old "choose Terminal or Agent" dialog returns.
- If no provider is configured or the agent is busy, the fallback reports that and sends nothing.

Code: `dispatch()`, `submitAgent()` and the ready branch of `pollShell()` in `src/main.cpp`;
ambiguous routes report `syntax_ok` in `backend/relay_core/router.py`.

## Implementer check (not a QA verdict)

On 2026-09-16, under Xvfb with `xdotool`, typing `frobnicate_widgets now` printed
`frobnicate_widgets: command not found` in Konsole, then the agent received the same text
and started a tool call. Screenshot:
`docs/qa_evidence/2026-09-17-terminal-first-fallback/implementer-xvfb-fallback.png`.
That run also exposed and fixed a readiness bug: `tcgetpgrp()` returned `ENOTTY` on kernel
7.0, so composer commands never reached the shell.

Build: the feature and the readiness fix are in commit `efc1228`. QA should record the SHA it checks.

## QA checklist

1. `frobnicate_widgets now` runs in the terminal, then reaches the agent.
2. `don't break the build` goes to the agent without running, and the route label says so.
3. `git status` in a non-repository directory fails in the terminal and is not sent to the agent.
4. With **Terminal first** off, ambiguous input shows the choice dialog.
5. With no provider configured, the fallback prints a message and sends nothing.
6. The toggle state survives a restart.

Independence: the implementer is Claude. The QA session must be a different family, for
example GLM-5.3 or Kimi K3, and record both identities.

## Superseded (2026-09-17)

The owner replaced this behavior before QA ran. The **Terminal first** toggle and the
exit-127 fallback were removed. Invalid input now goes to the agent without running, and
terminal mode fixes invalid or failing commands. Successors:
`changes/needs_qa_llm/2026-09-17-pre-submit-run-check.md` and
`features/needs_qa_llm/2026-09-17-fix-and-rerun-terminal-commands.md`.
Nothing from this issue remains to check. Status: done, as a supersession.

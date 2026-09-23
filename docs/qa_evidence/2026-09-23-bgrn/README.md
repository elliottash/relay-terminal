# BGRN implementation evidence

On 2026-09-23, `xvfb-run -a bash docs/qa_evidence/2026-09-23-bgrn/drive.sh`
launched a disposable Relay profile with its own private socket. The driver opened
`#BGRN`; [initial.png](initial.png) shows the Board's **Run (r)** and **Run in pane**
actions. [panes.json](panes.json) contains only the disposable session. Running the
pane action without an agent selected returned `not_agent_safe` in
[run-without-task.json](run-without-task.json), leaving the pane visible.

Focused checks: `scripts/relay-build --target relay-requests-tests --target relay`
built successfully; `ctest --test-dir build -R '^(requests|board)$'` passed 2/2;
`PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_claude
tests.test_guest_harness_codex tests.test_background_plan -q` passed 150 tests.
The UI drive checks labels, navigation, and the no-agent guard. It does not run a
real agent to completion; the request, todo, subagent, card, and Codex-plan cases
are covered by the focused tests.

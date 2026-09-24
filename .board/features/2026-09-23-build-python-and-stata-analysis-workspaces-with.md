---
id: 33G0
type: work
status: executing
labels: [feature, plugins, python, statistics]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Editable workspaces for Relay.md
links: {plans: [], commits: [], evidence: [], related: [MEPR, P2W8, F8R7, E85D, C0Q8], github: null}
---
# Build Python and Stata analysis workspaces with shared execution state

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**Second task plugin from #MEPR.** Two stages share the same language-router interface. Stage 0 makes the composer understand an already-running Python/IPython or Stata REPL in the visible terminal; typed code reaches that program, natural-language requests reach the agent, and a person can take control back. The full workspace owns a persistent kernel/session so human cells, script cells and agent `run_cell` calls share state and render structured output. Python with ipykernel is the first complete implementation; Stata follows through a validated installed bridge (`stata_kernel`, `pystata` or console), not a hard-coded assumption about license/version. Reserve Relay's forced agent/shell prefixes consistently; explicitly define how Stata `*` comments and IPython `!` escapes are entered. The console retains a Bash terminal for ordinary commands; `!` forces it. Plots and tables should reuse #MDA7's media layer when available.

## Done means
- With a known foreground Python/IPython or Stata REPL, auto mode identifies runnable statements and sends them to that program even when it uses raw tty input; a natural-language request goes to the agent. The UI shows the chosen destination before submit; multi-line paste, interrupt, takeover and forced shell/agent modes work.
- A Python workspace starts one persistent, interruptible kernel for a tab. Composer statements, `# %%` script cells and the agent's `run_cell` tool execute in that same state, with ordered input/output/error records and visible agent intent. Restart makes state loss explicit.
- The workspace shows variables and previews DataFrames/plots through typed output adapters; export contains enough code, outputs and ordering to reproduce the session without exposing secret input.
- Stata activation detects installed bridge/version/license availability and gives a clear supported path or a console fallback. The chosen Stata bridge passes the same routing/state/interrupt tests on a machine where Stata is available; absence of Stata is reported, not treated as a passing integration test.
- Router, foreground-input, kernel state, tool parity, output and restart tests pass; a live Python analysis scenario proves human and agent share one value.

## Plan
**Goal.** Make Relay's composer and agent work in the same interactive analysis state.

**Findings.** `router.classify` validates Bash; `Pane::sendLineToProgram` sends ordinary lines only to a canonical tty reader, so raw-mode REPLs often require Ctrl+H. The native agent's shell commands are separate processes and do not preserve Python variables.

**Steps.** 1. Add language-aware destination checks for Python/IPython and conservative Stata commands, with explicit prefix semantics and foreground detection. 2. Enable safe line/block delivery to a visible REPL without stealing terminal control. 3. Implement a workspace-owned Python kernel and namespaced `run_cell`, interrupt, restart and introspection tools. 4. Render output, variable/table/plot panes and `# %%` editor cells. 5. Select and validate a Stata bridge against the user's installed version, or provide an honest console-only mode. 6. Test routing and shared state; stage a live analysis scenario.

**Risks.** Syntax parsing alone does not prove a statement is safe or complete; incomplete blocks require continuation handling. Automatic execution must never send a prose request to a live REPL. Kernel state and tool access must be scoped to a workspace. Stata may be absent or licensed differently across machines.

**Verify.** Table-driven router tests, foreground program input tests, Python kernel integration tests, guest/native tool parity tests and live UI evidence; Stata integration only where installed.

## Tasks

- [ ] Implement REPL foreground detection and Python/Stata routing <!-- t:v0 blocked_by=#C0Q8 -->
- [ ] Send code blocks to visible REPLs with safe takeover/interrupt <!-- t:xj blocked_by=v0 -->
- [ ] Add persistent Python kernel and shared human/agent run_cell <!-- t:zb blocked_by=#C0Q8 -->
- [ ] Render history, variables, tables, plots and script cells <!-- t:9h blocked_by=zb,#E85D -->
- [ ] Select and validate Stata bridge or console fallback <!-- t:gm blocked_by=v0,zb -->
- [ ] Verify routing, shared state, export and restart live <!-- t:wj blocked_by=xj,9h,gm -->

---
id: T4JV
type: work
status: needs-qa-llm
labels: [change, bug]
component: [router, gui]
milestone: desktop-alpha
workstream: routing
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'A plain request auto-routed to the agent carries no "command not found" line under it; a mistyped command still does; `tests/test_router.py` passes'
source: 'owner, 2026-09-18: "still a bug that in auto mode, its saying command not found, eg: ✦ symlink from ~/projects to here / command not found: symlink", then "this is still bugged -- it says command not found: ✦ resume"'
links: {plans: [], commits: [cb010d8, b00b368], evidence: ['docs/qa_evidence/2026-09-18-command-not-found-under-a-request/'], related: [Q4SD], github: null}
---
# The "command not found" note has to prove a command was meant

## Report

`symlink from ~/projects to here` was answered correctly by the agent, with
`command not found: symlink` printed under the ✦ echo. Nothing had run in the shell: the note is
Relay's own explanation of why the line did not go there, and it reads exactly like a shell error.
The owner reported it twice — the second time for a bare `resume`, after the first fix still
treated any single word as a probable typo.

## Change

`relay::router.explain_invalid()` decides whether the GUI prints `invalid_reason` under a line
auto-routed to the agent; `Pane::dispatch` honours it and no longer runs its own test. The note
is shown only with evidence that a command was meant and mistyped:

- shell syntax, operators, globs or quotes in the line, or a flag among the arguments;
- a name that is not a plain lowercase word (`kubectl2`, `pip3`, `./run.sh`);
- otherwise, a word **one edit away from a command this machine actually has** — one insertion,
  deletion, substitution or transposition (`gti`→`git`, `pyton`→`python`, `docekr`→`docker`,
  `lls`→`ls`), which `resume`→`resize` is not.

Short English words sit one edit from some command or other (`add` from `adb`), so a line that
reads as a sentence is not rescued by the typo test: a second word no command takes as an operand
(`add **a** note`, `symlink **from** ~/projects`), or four plain words with nothing path-like
among them, keeps it quiet. A syntax error is still always explained.

## QA checklist

1. **Quiet.** In auto mode submit `symlink from ~/projects to here`, `resume`, `again`, `status`,
   `add a note about this`, `commit this`, `deploy the site`, `tell ryan about the meeting`,
   `35 * 30`: each goes to the agent with the ✦ echo and **no** note under it.
2. **Explained.** `nonexistentcmd123`, `gti status`, `docekr ps -a`, `pyton script.py`, `lls`,
   `ls | nonexistentcmd123`: each keeps `command not found: …` under the echo.
3. **Syntax.** `don't break the build` still reports its syntax error.
4. **Terminal mode.** `!gti status` and Ctrl+Shift+Enter still behave as they did — this rule only
   governs the note under an auto-routed line.
5. **Tests.** `PYTHONPATH=backend python3 -m unittest tests.test_router` (35) passes.

## Known gaps

- The typo test asks the PATH of *this* machine, so `kubctl get pods` is quiet where kubectl is
  not installed and explained where it is. That is the intended reading — there is no command to
  have mistyped — but it does make the message machine-dependent.
- Unrelated to card Q4SD, which handles a `/command` Relay does not have; the two paths do not
  meet.

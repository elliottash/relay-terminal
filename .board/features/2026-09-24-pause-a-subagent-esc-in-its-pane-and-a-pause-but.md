---
id: ZQNG
type: work
status: needs-verification
labels: [feature, subagents, ux]
assignee: agent
implemented_by: glm/glm-5.3
session: d067d65d-0caa-494c-82fd-0f973e068a42
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: system, primary: script, also: [], human: none, criteria: pytest tests/test_subagents.py (pause/resume/stop-of-paused/todo stays in_progress) green; relay-editor-tests subagents test green; the pause button draws and Esc pauses in a manual run., sign_off: none, effort: medium, stakes: rework}
source: pane 1, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-subagent-pause/], related: [], github: null}
---
# Pause a subagent: Esc in its pane, and a pause button on the subagent strip

## Issue
esc should work to pause a subagent when you are in their pane (rather than going back to main). and there should be a pause button on the subagent list, in addition to the x to stop them

## Done means
- A live subagent can be paused: the in-flight step is cancelled, the run ends in status `paused`, its linked todo stays in_progress and no result is delivered to the main agent.
- Esc in a subagent's pane (the tabs pane or its message box) pauses the live subagent in front instead of returning to main; when it is not live, Esc still returns to main as today.
- The subagent strip under the composer shows a pause button on running rows (beside the x that stops them); on a paused row it becomes resume, and x dismisses/stops as before.
- A paused subagent resumes with the resume button, or with a message, continuing its conversation; a stop or a reset clears it, and its todo returns to pending.

## Execution Summary
Landed in `49dd6a6` (+ evidence `9ea13f3`):

- Backend: `agent_pause {id|"all"}` → `agent_paused {ids}` and `agent_resume {id}` in `backend/worker.py`; `Subagents.pause()` / `.resume()` in `backend/relay_core/subagents.py`. A held run ends `status=paused`, `subagent_finished {outcome: "paused", handoff: "held"}`: the step in flight is cancelled, nothing is delivered to the main agent, and a linked todo stays in_progress. `agent_resume` continues it with a labelled `Continue.` (the same resume a message performs); a message to a paused agent resumes it too. `agent_stop` now also marks a paused agent stopped, returning its todo to pending, so x and `stop_all(reset=True)` clear held runs.
- Esc in the subagent pane (`SubagentTabsView::keyPressEvent` and the message box's Esc): a live agent in front is held first — Esc only goes back to main when nothing can be held. A first-time hint says so, and the pane header gained a ‖/▶ button doing the same with a click.
- The strip (`SubagentsPanel`): a ‖ button sits beside the × on running rows and becomes ▶ on a paused row; clickable and on `p`; the row status icon for paused is ‖, the tab text is amber, the handoff line reads "held: nothing was delivered…", and the folded count names paused runs.

## Tests
- `tests/test_subagents.py` (python, all 50 green): `test_pause_holds_a_running_subagent_and_resume_continues_it` (pause mid-flight at a gate → paused/held; resume → done with `Continue.` labelled; `subagent_started resumed=true` once), `test_paused_run_keeps_its_todo_until_it_is_stopped` (no todo finished event while held; stop → finished/stopped), `test_pause_and_resume_refuse_what_they_cannot_do`, `test_reset_stops_a_paused_subagent`.
- `tests/subagents_test.cpp` (`ctest -R subagents`, 37 green): `pausedOutcomeHoldsTheRow`, `stripPauseKeyHoldsAndResumes` (p holds → resumes → x stops the held run rather than dismissing), `escInThePanePausesALiveAgent` (Esc holds, does not leave; ▶ resumes; Esc on a paused run leaves), and `tabsOpenSwitchFollowTheListAndClose` updated to the new Esc contract.
- `build/relay` and `relay-subagents-tests` build clean through `scripts/relay-build`; land.py's slot verified the exact committed tree. `tests.test_pane_view` (37) and `tests.test_agent` re-run green.

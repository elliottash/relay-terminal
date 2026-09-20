# One helper system, live (card #FEJQ, protocol §30)

Implementer evidence for the integration of card #FEJQ: the agent's power to drive the app, the
helper panel in Options, Actions and Sessions, the Ask rows on Info and Activity, and one helper
worker per tab. Run 2026-09-20 against `main`, on the binary `scripts/land.py` built from the
exact landed tree.

**Nothing here called a provider.** `stub-provider.py` is a loopback-only OpenAI-compatible
endpoint; the profile points a local model endpoint at it, so the terminal pane's agent and the
tab's helper are both that script. A scene is picked by a keyword in a *user* message — never by
the last message, because the worker appends a user-role message of its own at the end of a turn
and that would take the scene away from the prompt that was typed.

## How to run it again

```
docs/qa_evidence/2026-09-20-agent-app-control/drive.sh <relay binary> [out dir]
```

Xvfb, an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` under
a short path (the 108-byte socket limit), `RELAY_KEYRING=off`, a two-card fixture board. Needs
Xvfb, xdotool, ImageMagick and tesseract. `notes.txt` is the result: one PASS/FAIL line per check,
each naming the screenshot it was read from.

Two things the harness had to learn, both worth keeping:

- **It launches Relay four times on one profile, once per phase.** The first version ran one
  window through all of it, and by the fourth phase five panes were sharing 1500 px, the
  Switchboard had a card open in its editor, and a keystroke meant for a helper's composer went
  into a card title. A relaunch costs ten seconds and is the difference between evidence and a
  guess.
- **The pointer goes back where it was after every screenshot.** There is no window manager under
  Xvfb, so the input focus follows the pointer: parking it off the window to keep a hover
  highlight out of the picture also takes the keyboard away, and the next `xdotool type` goes
  nowhere. Three asks were lost that way before the shot function restored it.

## What the run proved

`notes.txt` in full: **29 checks, 29 passed**. The shots, in order:

| Shot | What it shows |
| --- | --- |
| `00-start.png` | Relay up on the stub provider, one terminal pane |
| `01-open-options.png` | **a1** `app_open {options, terminal, option:terminal/copy_on_select}`: Options is open, on the Terminal page, at the row — and the tool came back `ok`, not refused |
| `02-option-set.png` | **a2** `app_option_set`: the row is on, marked "changed by the agent just now: off → on", and the agent's own answer says "went from off to on" |
| `03-notification.png` | **a3** the notification behind the bell: "Agent changed Copy on select: off → on" with **Undo** |
| `04-undone.png` | **a4** after Undo: the row is off again and the mark is gone — pressing Undo is the person touching the row |
| `05-action-run.png` | **a5** `app_action_run {agent.internalsPane}`: an `agent_safe` action ran and the Activity pane opened beside the terminal |
| `06-sessions-search.png` | **a6** `app_sessions_search`: answered inside the worker, out of the session index, with no pane open |
| `07-open-card.png` | **a7** `app_open {switchboard, <card>}`: the Switchboard opened on the fixture card |
| `08-options.png` | **b1** Options carries the helper, collapsed to "? Helper Agent (Alt+Q)" at the pane's bottom right |
| `09-options-ask.png` | **b2** expanded by the key: the head says "Options helper", the composer asks about *this pane*, the model box is the Switchboard's |
| `10-options-answer.png` | **b3** the helper answered in the Options panel, with a link into the app |
| `11-options-link.png` | **b4** the `option:` link clicked: the row is revealed in this pane, no second Options pane |
| `12-helper-write.png` | **b5** the helper's own `app_option_set`, on the *helper worker's* pipe: "I turned **Copy on select** on for you: off → on", announced with Undo |
| `13-sessions.png` | **c1** the session manager carries the same collapsed row |
| `14-sessions-ask.png` | **c2** expanded: "Sessions helper" — one worker, a different pane |
| `15-sessions-answer.png` | **c3** the Sessions helper answered, with an `option:` link |
| `16-sessions-link.png` | **c4** that link opened Options at the row: the link a pane cannot resolve goes to the window |
| `17-info.png`, `18-info-draft.png` | **d1, d2** the ⓘ pane's Ask row, and its chip drafting the question into the terminal's composer — nothing sent |
| `19-activity.png`, `20-activity-draft.png` | **d3, d4** the same on Activity |
| `21-tab1-board.png` … `24-tab1-after.png` | **e** two tabs on one project: tab 2's Switchboard answers, and tab 1's log stays empty — two helpers, two conversations, one set of board files |

## Bugs this run found, and what was done about them

1. **The helper's answer never reached the panel** (`c2f158fd`). A worker *event* names itself in
   `event`; `type` is what a message going the other way carries. `RelayWindow::wireHelperPanel`
   read `type`, so every embedded panel was handed an untyped event: a `delta` arrived, the panel
   took any tagged event as proof that a turn was running and then matched none of its branches,
   so the Options helper sat at "running · 0:43" with an empty log while the worker's own log said
   the turn was done in 628 ms.
2. **The panel called itself the board's in two places** (`c2f158fd`). The busy strip under the
   log read "✦ Switchboard agent" in every pane, and an empty log invited a question about "the
   board itself — what is where, what duplicates what", which the Options helper cannot answer.
3. **Undo by hand left the agent's mark on the row** (`b5756ab0`). The row went back to off and
   then said "changed by the agent just now: on → off" about a revert the person had performed by
   hand a second earlier. A person's Undo now clears the mark — pressing it *is* touching the row
   — while an agent's own `app_undo` keeps it, with what the undo made the row.
4. **The helper's first ask was refused, silently** (`caa07979`). The worker's own log had it:
   `protocol_error kind=board_chat error=ValueError msg="Configure a provider and workspace
   first."` `BoardWorker::start()` holds `configure` until the worker answers `ready`, so that it
   never races the handshake — but `send()` wrote straight to a process that was already running,
   and a process is running well before it is ready, so anything sent in that window went out *in
   front of* the configure. That is exactly what a helper panel's first ask does (§30.7: the
   worker starts on the first ask, and `sendToHelper` starts it and sends in the same breath). The
   refusal came back as an `error` event the panel does not draw, so the Options helper sat with
   its clock running and its log empty; the second ask always worked, which made it look like a
   timing fluke for three runs. `send()` now holds everything but the configure until `ready` and
   writes it in order behind it — the rule `open()` already followed, which is why the Switchboard
   never hit this.

Three things the run found that were the *harness* being wrong rather than the app, noted because
the next driver will meet them: a toggle row's catalog id is `option:` + its QSettings key, not
the key (`RelayWindow::toggleRow`); Info is Alt+I and Activity is Alt+Shift+R (Ctrl+I is
`input.toggle`); and an answer streams, so a screenshot taken on the first frame that matches can
be the sentence *before* the link — wait for the turn to settle before clicking one.

And one thing that is the app's, left as it is because it costs nothing and is not this card's:
`relay::appcommands::actionIsAgentSafe` names a few keys that are no longer in the action catalog
(`help.shortcuts` among them). An agent told about one finds nothing to run.

## What this does not cover

- **The helper's conversation across a restart.** §30.7 wants it persisted per (project, tab);
  §30.8 lists it as not built, and the backend subagent is landing it separately. Each launch here
  therefore starts the helper's conversation again, which is what the phases assume.
- **Two tabs on one project under a restart**, for the same reason.
- **A real provider.** Everything an agent "decided" here was scripted; what is tested is the
  wiring, the round trip and what the GUI does with the answer, not a model's judgement.
- **`app_undo` asked for by the agent** (§30.4). The GUI's Undo — the notification's button, which
  is what owner decision 6 is about — is exercised live; the unit test covers the agent's own.
- **The Actions mode of the Options pane.** The panel follows the mode in the unit test
  (`ctest -R settings`, `theHelperPanelFollowsTheModeAndAsksAsTheOptionsPane`); the live run asks
  it only in Options.

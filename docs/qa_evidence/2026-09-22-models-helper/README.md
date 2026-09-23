# Helper agent on the Models pane (#MH7P)

Owner, 2026-09-22: "there needs to be a helper agent on the model page".

Live drive of `build/relay` built from this change, under Xvfb (`:71`) with isolated `XDG_*`,
`TMPDIR` and `RELAY_KEYRING=off`. The glm-coding key was passed through the environment only.

- `00-row-under-providers.png`: first run. The Models pane opens on providers, and the one
  "Helper Agent (Alt+Q)" row sits at its bottom right. The embedded Options renderer above it has
  no row of its own.
- `01-alt-q-opens-helper.png`: priorities tab, Models pane focused, Alt+Q. The console opens
  under the tab, headed "Models helper" with "Ask the Models helper…" in the box, at the same
  bounded height as the Options helper's console.
- `02-helper-answers.png`: a real turn on glm-5.3-flash. The helper names the tab (priorities,
  "ordered per-class lists with reasoning levels and box cutoffs"), the pane it serves ("Check
  Models tab state") and the class in focus (main), from its brief and screen line. The left pane
  shows the same question sent to the terminal agent by mistake (Alt+Q was pressed while that pane
  was active, so it routed there). That agent could not tell, which is the difference the context
  makes.

Found on the way, not caused by this change: in a profile whose main list starts with a guest
(`guest:claude|opus`), picking glm in any helper console's model box is applied (`model_selection
… applies=now` in worker.log), but the next turn still fails with "The helper agent cannot run
on Claude Code". The failure is in the shared helper worker (`board_protocol.py`, #BMS1/#GH5T),
so the same happens in the Options, Sessions and Board helpers. For this drive, glm was put first
in the main list instead.

<!-- relay:entry 20260920T210500Z-a1 author=claude-code kind=event -->
### Claude Code · 2026-09-20 21:05
filed from the owner's own words, 2026-09-20, comparing the main pane's prompt box with the
"Switchboard agent" and "Options helper" panels, and with the card page's reply box. Claimed it
in the same turn (land session `promptboxes`, contact "prompt-box restyle subagent").

<!-- relay:entry 20260920T210600Z-a2 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 21:06
the owner's words, as the spec: "i dont like the helper agent prompt UI … there is the useless
help sentence, and then a bunch of wasted space, and then the tiny text box. and the buttons dont
look as good"; "the card agent looks a lot better"; "[remove] the send button on all, make it like
the pane agent"; and, of Plan / Execute / Verify, "move those buttons out of there (plan / execute
/ etc), because they actually dont do anything in the chat box. can we instead put buttons like
that in a row above the chat box. they are actions the agent can take that dont require typing. we
put the 'clean up' button there for the main switchboard agent, for example."

So the rule implemented everywhere is: the main pane's prompt box is the reference, and an action
that needs no typing goes in a row above the box.

<!-- relay:entry 20260920T211200Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 21:12
**the helper panel, `502f2b22`.** `relay::HelperChatPanel` (all four panes) is now a head row —
the name and clock, then Check / Clean up / the fold — then the findings, the survey, the log, the
queue, and then one rounded frame `QFrame#boardChatBox` styled rule for rule as `QFrame#composer`
is, taking the accent border while the cursor is in it. Inside the frame: the busy strip with its
`✕ Stop` as the first row and on screen only while a turn runs; the editor, borderless
(`setFrameShape(NoFrame)` plus the qss) and in the prompt font, `setAutoHeight(2, 8)`; then the
chip strip — context, model, microphone, right-aligned, at the pane's chip height.

`boardChatSend` is gone entirely; Esc in the box now stops a running turn, mirroring the pane. The
empty-log paragraph is gone with it: the placeholder carries what the box is for and names the
agent ("Ask the Switchboard agent — Enter sends, a second prompt queues"), and the log is hidden
outright while the conversation is empty and, once there is one, no taller than what it holds.

Tests: `theHelpersPromptBoxIsTheSameShapeAsAPanes` is new in `tests/boardmodel_test.cpp`; the old
Send-button test is now the busy strip's, and `tests/settingspane_test.cpp` and
`tests/conversations_test.cpp` assert the same box in the Options and Sessions embeds.

<!-- relay:entry 20260920T213000Z-b2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 21:30
**the card page, `ce29cb8e`.** Plan (p), Execute (x) and Verify (v) moved out of the `boardReply`
frame into `boardCardActions`, a right-aligned row directly above it — same object names, keys,
tooltips and behaviour, and both still read whatever is typed in the reply box as their note. The
row hides and shows with the box, and `controlsHeight()` counts it so a floating notice still
clears the controls. Inside the frame: the busy strip, the borderless editor, and the model box
alone on its own chip strip. `fitButtons()` measures the action row now and no longer counts the
model box — the two no longer share a width, so at a ~350 px card the labels keep their keys,
which is what moving them bought.

`QFrame#boardReply` was given the panel's frame (same ground, 10 px radius, accent border on
focus) so the list page and the card page are visibly one component.

<!-- relay:entry 20260920T214500Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:45
live Xvfb run, `docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/`: `drive.sh` with its own
`stub-provider.py` (no provider account, `RELAY_KEYRING=off`, isolated HOME/XDG/TMPDIR), driving
the binary `land.py` built from the exact tree it committed. **22 checks passed, 0 failed.** Six
screenshots: the pane's box beside the Switchboard agent's idle and with a short conversation, the
card page, the Options helper expanded and idle, the Sessions helper expanded, and a ~350 px pane.
`NOTES.md` has the QA checklist and what to read in each shot. Targeted tests:
`ctest --test-dir build -R '^board$|^boardpane$|^settings$|^conversations$'` — 4/4 pass.

Moved to `needs-verification`.

# Skill slash detection — #D7AV

The auto-mode destination ignored SKILL verdicts, retaining the previous color. Pane::refreshDestinationColor now maps SKILL to Agent, covering all discovered skills. Built-in commands and `/skill` already use COMMAND and need no change; the existing skills.slash hint remains applicable.

Verification:
- `scripts/relay-build --target relay` passed.
- land.py built the exact committed tree for 98c308c1298e successfully.
- `ctest --test-dir build -R '^(slash|input)$' --output-on-failure`: both passed.
- `RELAY_TEST_BINARY=/tmp/claude-1000/land/codex-slash-detect/verify/build/relay python3 /tmp/slash-detect-drive.py`: passed on that exact build. Retained driver is drive.py (defaults to build/relay). Isolated Xvfb and XDG directories, fake worker, no provider calls.
- Screenshots: ls shows the cyan terminal indicator; deliver, clean-commit-fix-this, local-model-setup, skill-deliver-fix-this and compact show the purple agent indicator after replacing ls. Enter sends `/deliver fix this` as an ask with `skills: ["deliver"]`.

The first fixture lacked agent_finished, leaving subsequent asks queued; corrected its protocol and reran successfully. Initial all-target build encountered another session's unfinished contextmeter test linker failure; focused app build passed. Board validation found no D7AV findings; existing MDL1 identifier/task errors remain outside this change.

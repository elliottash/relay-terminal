# Shell command spacing — #SG4P

`shell/integration.bash` leaves at least two newlines at the end of a nonempty PS1,
giving the command one blank row above. The first-command DEBUG hook marks the
input rows, then prints a newline to separate command output from the input band.
This is loaded by new pane shells; existing shells retain their loaded functions.

Verified:

- `python3 -m unittest tests.test_shell -v`: all 11 tests pass. Existing real PTY
  assertions now also cover the lower gap for staged and typed commands, and the
  upper gap with a custom prompt. Multiline input, heredocs, exit status, and
  interrupts remain covered.
- `bash -n shell/integration.bash`: passed.
- Live `build/relay` under Xvfb with isolated XDG directories and
  `RELAY_DATA_DIR` pointing to this checkout. Submitted two printf commands through
  the composer. Inspected `commands.png`: each cyan command band has one empty row
  above and below; output text and the following folder prompt remain intact.

No C++ changed; the existing GUI executable loads the edited shell script at launch.

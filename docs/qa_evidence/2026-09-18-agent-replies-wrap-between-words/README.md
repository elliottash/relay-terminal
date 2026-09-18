# Agent replies wrap between words — implementer evidence

Relay built from this change, run under Xvfb (display :173) with every XDG directory and TMPDIR
isolated, `--clean-shell --fresh`, provider `glm-coding` (glm-5.3), a 900-px-wide window (93
columns). One prompt typed into the composer.

- `before-mid-word.png` — the same build with the wrapper switched off (a temporary env switch,
  not committed): "typ / ically", "withou / t", "wr / apped", as in the owner's report.
- `after-wrapped.png` — the wrapper on: every row ends between words, and each bullet's
  continuation rows hang under its text, not under the bullet.

`relay-wordwrap-tests` (ctest `wordwrap`) covers the rules: breaks between words, a space at the
edge dropped, streamed one character at a time equal to whole, SGR and OSC 8 zero-width, hanging
indent for `•`/`◦`/`12.`/indented code, over-long words left to the terminal, wide characters.

Seen during the run and **not** caused by this change: a row of the reply occasionally vanishes
mid-stream (visible in both screenshots' source scrollback — "once a / of output" in the after
run). It happens with the wrapper off too; Relay's own writes to the terminal were logged and are
complete and in order, so something else (likely the shell redrawing its two-row prompt) writes
over that row. Reported to the owner in chat; not fixed here.

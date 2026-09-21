# 2026-09-21 — #7QFW user-input band: solid block, cyan for shell commands

Implementer evidence (kimi-k3, pane 2b383612).

## What changed

1. `src/Pane.h` (`printInline`, `Ink::User`/`Ink::UserAgent`): blank rows that end in `\r\n`
   inside a user echo are now marked with the row's OSC 7772 role, so a multi-paragraph prompt
   is one solid band instead of banded lines with ground-coloured gaps at the blank rows.
2. `shell/integration.bash`: the pane shell marks the rows a command's echo occupies with
   `OSC 7772;shell` (`__relay_mark_typed_rows`, first DEBUG-trap fire of a command line; row
   count staged by `__relay_load`, `history 1` fallback for hand-typed lines), so a shell
   command sits on the same block in the shell's cyan. Engine/view unchanged —
   `TerminalView::paintRow` already paints `userShellBand`/`userAgentBand` from the live theme.

## Ran

- `python3 -m unittest tests.test_shell -v` — 11/11 OK, incl. three new cases that drive a real
  PTY bash with `shell/integration.bash` and assert the emitted bytes
  (`\x1b[1A` + `\x1b]7772;shell\x1b\\` + `\x1b[B` for a one-row command; 3 marks for a
  three-row staged command; history fallback for a hand-typed command).
- `ctest --test-dir build -R consolemode` — passed.
- `scripts/relay-build` — clean.

Not run: full test suites (per WARP.md); a visual check — see the card's QA checklist.

## Pre-check that shaped the design

Verified empirically (bash 5, PTY): the DEBUG trap fires inside a `bind -x` handler only as the
function call itself (`BASH_COMMAND=__relay_load`), which the existing `__relay_*` guard already
excludes — so row counting in `__relay_load` cannot trip the marking branch early.

# Vendored libvterm

- Upstream: https://www.leonerd.org.uk/code/libvterm/ (Paul Evans), MIT license (see `LICENSE`).
- Version: 0.3.3, from `libvterm-0.3.3.tar.gz`
  (sha256 `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`).
- Imported: `LICENSE`, `include/`, `src/` (including the pre-generated `*.inc` tables).
  Not imported: `bin/`, `t/` (perl test harness), `Makefile`.

Relay builds these files into the static library `relay-vterm-c` (see `engine/CMakeLists.txt`)
so every platform gets identical emulator behaviour. Relay's changes are marked in the code
with `RELAY PATCH` comments and listed below; everything else is byte-identical to upstream.

## Relay patches

| # | Files | Change |
|---|---|---|
| 1 | `screen.c` | **Row-pointer scrolling.** `getcell()` goes through a per-buffer row table; full-width vertical scrolls rotate row pointers instead of `memmove()`ing every cell of the scroll region. `resize()` linearizes first, so the reflow code is unchanged. Headless 200 MB `cat`: 21.3 -> 42.2 MB/s (see `docs/ENGINE-PERF.md`). |
| 2 | `screen.c` | Scrollback push converts a whole row at once (`row_to_external`) instead of calling `vterm_screen_get_cell()` per column; resize pushes spare lines from the buffer being resized (upstream read the active buffer, wrong while the alternate screen is active). |
| 3 | `vterm.h`, `state.c`, `screen.c` | `VTermScreenRelayCallbacks` (`sb_pushline4` / `sb_popline4`) carry `VTermLineInfo` (soft-wrap continuation, marks) with scrollback lines so the host can reflow scrollback; `sb_popline4` fills rows at the new width during resize. The state keeps the line infos of rows scrolled off the top (`relay_scrolled_lineinfo`). |
| 4 | `vterm.h`, `screen.c` | Per-cell hyperlink id (`VTermScreenCell.hyperlink`, `vterm_screen_relay_set_hyperlink()`) for OSC 8, carried through scrollback and reflow. |
| 5 | `vterm.h`, `state.c`, `screen.c` | `VTermLineInfo.relay_marks` (4 bits) + `vterm_state_relay_mark_cursor_line()` for OSC 133 prompt marks; kept by scrolling and reflow. |
| 6 | `vterm.h`, `state.c` | Optional grapheme clusters (`vterm_state_relay_set_grapheme_clusters()`): emoji skin-tone modifiers, ZWJ + pictograph, regional-indicator pairs (2 cells) join the previous cell; width of the base character. `VTERM_MAX_CHARS_PER_CELL` 6 -> 10. |
| 7 | `vterm.h`, `state.c` | `vterm_state_relay_get_bracketpaste()`. |
| 8 | `screen.c` | Reflow no longer indexes row -1 when row 0 is a continuation line. |
| 9 | `vterm.h`, `vterm.c` | `vterm_relay_parser_at_ground()` so the host can inject display bytes between sequences. |

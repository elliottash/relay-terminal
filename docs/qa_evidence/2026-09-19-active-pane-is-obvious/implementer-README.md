# Which pane is active, and clicking into one (#H3TQ) — implementer evidence

`drive.sh [build-dir] [tag] [theme]` drives Relay under Xvfb with an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, so neither a live Relay on this
machine nor the owner's profile is involved. It makes three terminal panes, then clicks into each
one's **terminal body** — the `Qt::NoFocus` surface this card is about, not the prompt box — and
shoots the window each time. Screenshots are `implementer-<tag>-<scene>.png`.

| Scene | What it does |
|---|---|
| 01 | three panes, the last one made is the active one |
| 02–05 | click into each pane's terminal output in turn, and back to the first |
| 06 | click the model box of a pane that is **not** active |
| 07–10 | the Switchboard opened as a tool pane, then clicks between it and two terminals |

- `implementer-before-*` — the build this change was made from. Scenes **01–05 only**: the harness
  grew scenes 06–10 while the fix was being written, and the before build was not kept.
  `implementer-before-02-clicked-left.png` is the report: the click landed in the left pane and the
  bottom-right pane still holds the caret and the accented composer.
- `implementer-after-*` — the same build with the change, scenes 01–10.

The popup in scene 06 is its own X window, so `import -window` captures it as a black rectangle;
what the shot proves is that it is still open *and* that the pane behind it went active.

Measured rather than eyeballed, on the after shots (`convert <png> -crop 20x1+0+400 txt:`): the
left pane's border column reads `#8B919C` when that pane is active and `#2A2E37` when it is not,
and the brightest pixel of its name reads 231/255 against 153/255. The same two pairs are asserted
against the live tokens, for all five shipped themes, in
`tests/themeswitch_test.cpp::theActivePaneIsVisiblyTheActiveOne`.

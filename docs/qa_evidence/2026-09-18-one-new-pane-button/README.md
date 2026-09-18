# One "new pane" button (#803C) — implementer evidence

Implementer: Claude Opus 5 (1M context), Claude Code subagent, 2026-09-18. These are implementer
screenshots, not a QA verdict.

Run: a build of HEAD plus this change, under Xvfb (`:97`, 1400x900) with `XDG_RUNTIME_DIR`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_CACHE_HOME` and `TMPDIR` all pointed at
fresh scratch directories, `RELAY_KEYRING=off`, `relay --clean-shell --fresh`, driven with `xdotool`.

The first version (484ee4a) asked for a side before making the pane; the owner turned that down the
same day, so these screenshots are of the second version: ⊞ makes the pane on the right at once.

| Screenshot | Shows |
|---|---|
| `implementer-1-one-button.png` | The pane's corner row has one ⊞; the ⬓+ / ◫+ pair is gone. |
| `implementer-2-pane-on-the-right-and-toast.png` | One click on ⊞: a new pane on the right, focused, and the toast "New pane · drag its header to place it". |
| `implementer-3-shortcut-hint.png` | The queued shortcut hint that follows: "Next time: Ctrl+E · new pane". |
| `implementer-4-dragging-the-header.png` | Dragging the new pane's header over the bottom of the left pane: the drop zone lights up. |
| `implementer-5-dropped-below.png` | Dropped: the new pane is now below the original. |
| `implementer-6-arrow-after-button-does-not-move.png` | ⊞ on the bottom pane, then the Left key at once: a pane on the right, and the arrow does not move it (no #78BN window after the button). |
| `implementer-7-ctrl-e-then-down-unchanged.png` | Ctrl+E then Down: the key path of #78BN is unchanged (the new pane lands below). |

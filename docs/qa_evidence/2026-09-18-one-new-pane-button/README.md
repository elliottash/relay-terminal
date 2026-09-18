# One "new pane" button that asks for a side (#803C) — implementer evidence

Implementer: Claude Opus 5 (1M context), Claude Code subagent, 2026-09-18. These are implementer
screenshots, not a QA verdict.

Run: a build of HEAD plus this change, under Xvfb (`:97`, 1400x900) with `XDG_RUNTIME_DIR`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_CACHE_HOME` and `TMPDIR` all pointed at
fresh scratch directories, `RELAY_KEYRING=off`, `relay --clean-shell --fresh`, driven with `xdotool`.

| Screenshot | Shows |
|---|---|
| `implementer-1-one-button.png` | The pane's corner row: ⊞, ⇱, ×. The ⬓+ / ◫+ pair is gone. |
| `implementer-2-prompt.png` | After clicking ⊞: "Use arrow keys to place the new pane", ← ↑ → ↓ as buttons, and the first-time hint "Next time: Ctrl+E, then an arrow". |
| `implementer-3-left-arrow-key.png` | The Left arrow key: the new pane is made to the **left** of the original and has the focus. |
| `implementer-4-clicked-down-arrow.png` | (fresh start) ⊞, then a click on the prompt's ↓ button: a new pane below. |
| `implementer-5-esc-cancels.png` | ⊞, then Esc: the prompt is gone and no pane was made (still 2). A click elsewhere did the same. |
| `implementer-6-up-inserts-above.png` | ⊞ on the bottom pane, then the Up key: the new pane is inserted between the two (tab reads "· 3"). |
| `implementer-7-ctrl-e-unchanged.png` | Ctrl+E is unchanged: a pane on the right at once, with the #78BN "← ↑ ↓ to place" toast. |

Unit tests: `tests/panelayout_test.cpp` — `chooseModeTakesAllFourArrows`,
`chooseModeEscCancelsAndOtherKeysPassThrough`, `chooseModeWaitsLongerThanTheKeyPath` (28/28 pass).

Found and fixed during the live run: a click on the prompt's arrow first closed the prompt, because
the press reaches the application event filter through the `QWindow`, which is not a widget. The
filter now asks `QApplication::widgetAt()` whether the press is on the prompt.

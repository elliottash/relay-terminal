# Ctrl+Shift+Y toggles the session manager

Implementer evidence, not a QA verdict. Owner, 2026-09-20: "pressing ctrl shift y again should
close the sessions manager".

`drive.sh` starts `build/relay` under `Xvfb :207` with a fresh isolated HOME and XDG profile
(`RELAY_KEYRING=off`) and presses the key six times over one window:

| capture | step | what it shows |
| --- | --- | --- |
| `implementer-0-before.png` | — | one terminal pane, no manager |
| `implementer-1-open.png` | Ctrl+Shift+Y | "Sessions" opens beneath the pane and takes the focus |
| `implementer-2-closed.png` | Ctrl+Shift+Y | the pane is gone and the focus is back in the prompt box |
| `implementer-3-open-again.png` | Ctrl+Shift+Y | it opens again — the toggle is not a one-shot |
| `implementer-4-focus-in-pane.png` | click the terminal | the manager stays open, the focus leaves it |
| `implementer-5-brought-forward.png` | Ctrl+Shift+Y | still open, brought forward and focused — **not** closed |
| `implementer-6-closed-again.png` | Ctrl+Shift+Y | focused, so this press closes it |

The rule is in `RelayWindow::toggleSessionsPane`: the manager closes only when it is the active leaf
or holds the keyboard focus, so the key never closes a pane the user is not looking at; otherwise it
goes through `Pane::openResume`, which opens it or brings it forward and rebinds it to the asking
pane. `conversations.open` (unbound by default) is the same toggle. `/resume`, `/conversations` and
the palette rows are unchanged openers — they are typed in a pane's prompt box, which is never the
manager.

## QA checklist

- [x] `scripts/relay-build`
- [x] `drive.sh`: the six captures above, on this build
- [ ] With the manager open, `/resume` typed in the terminal pane still opens (focuses) it rather
      than closing it
- [ ] Esc in the manager still closes it, and the focus returns to the pane that opened it

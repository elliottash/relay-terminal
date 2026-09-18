# Actions red-orange, lit title-bar buttons, Alt+I — implementer evidence, 2026-09-18

Cards [#SRM2](../../../issues/changes/needs_qa_llm/2026-09-18-actions-red-orange.md) (Actions is
red-orange), [#E01Z](../../../issues/features/needs_qa_llm/2026-09-18-lit-title-bar-buttons.md) (a
button is lit while its pane is open, and closes it on a second click),
[#HECG](../../../issues/changes/needs_qa_llm/2026-09-18-solarized-dark-removed.md) (Solarized Dark
removed) and [#CSMK](../../../issues/changes/needs_qa_llm/2026-09-18-alt-i-and-closed-rows.md)
(Alt+I). Implementer screenshots: they show the features working once, and are not independent QA.

`drive.sh [build-dir] [scene...]` retakes all of them under Xvfb on a free display, with an isolated
HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. There is no provider account and no sshd: the
profile points a local model endpoint at `stub-provider.py` on 127.0.0.1, and `ssh` on PATH is
`fake-ssh.c`. Scenes: `buttons`, `toggle`, `info` (and `layout`, `tabs`, inherited from the
pane-types script this one was copied from).

| File | What it shows |
| --- | --- |
| `implementer-buttons-relay-dark-type-none.png` (+`-bar`) | Nothing open: no tool button is lit. |
| `implementer-buttons-relay-dark-type-actions.png` (+`-bar`) | The Actions pane: a red-orange band (ground `#2B201D`, glyph `#E5844F`) and its title-bar button lit in the same hue. |
| `implementer-buttons-relay-dark-type-all.png` (+`-bar`) | Sessions, Options and the Switchboard open together, each button lit in its own colour. |
| `implementer-buttons-relay-light-type-*.png` | The same in Relay Light (Actions `#A8450C`). |
| `implementer-buttons-relay-dark-off-*.png` | Pane colours off: no tint, and the lit state is a neutral raised ground. |
| `implementer-toggle-relay-dark-type-1-open.png` | A click on the bolt opens Actions. |
| `implementer-toggle-relay-dark-type-2-closed.png` | The same button again closes it; the light goes out. |
| `implementer-toggle-relay-dark-type-3-board.png` (+`-bar`) | The Switchboard button, lit brass. |
| `implementer-toggle-relay-dark-off-*.png` | The same toggle with pane colours off. |
| `implementer-info-relay-dark-alt-i.png` | Alt+I opens the conversation-info pane; its footer teaches Alt+I. |

The theme list in these runs offers five themes: Solarized Dark is gone (#HECG). What a profile
still carrying `theme/name=solarized-dark` does is checked by `ctest -R themeswitch`, not here.

No key, token or account is used anywhere in this folder: the model is `stub-provider.py`.

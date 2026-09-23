# #1MGS: inline images — evidence

`drive.sh [build-dir]` stages a real Relay under Xvfb with an isolated HOME, XDG dirs,
XDG_RUNTIME_DIR, TMPDIR and `RELAY_KEYRING=off`. The model is a loopback stub
(`stub-provider.py`, named `llava-stub` so Relay counts it as reading images). Pictures come from
`make-images.py`, and `demo.sh` prints one per protocol from the sandbox's `~/.bashrc`, so nothing
is typed into the terminal.

| Shot | What it shows |
|---|---|
| `before-01-protocols.png` | the binary before this card: kitty, iTerm2 and sixel all draw nothing |
| `before-02-conversation.png` | before: `![the chart](reply.png)` shows as `!` plus a link, and the attachment gets no thumbnail |
| `01-protocols.png` | after: one picture per protocol, with the text after them intact |
| `02-conversation.png` | after: the attachment thumbnail under the prompt, the reply's picture, and the remote image kept as a link |
| `03-restored.png` | Relay quit and started again on the same profile: the saved pane draws its pictures again |
| `04-cleared.png` | after `clear`: every picture is gone with its text |
| `view-offscreen.png` | the view's own offscreen test render (ViewTest), an 8x4-cell image between two lines |
| `before-03-cleared.png` | the first harness run's `clear` shot, before this card |

Run `drive.sh` again to regenerate `01`-`04`.

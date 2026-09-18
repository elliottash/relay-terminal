# Files on the host: opening, editing and saving one (#S5SH)

Implementer evidence for the second half of the card's link story — docs/SSH-AND-MOSH.md § 9. The
driver is `implementer-remote-file-drive.sh`; it runs Relay under Xvfb with its own `HOME`,
`XDG_*` and `TMPDIR`, logs into `localhost` with the user's key, and edits a file it made in
`/tmp` on the host and removes afterwards. No system file is touched.

    BUILD=<your build dir> QA_DISPLAY=91 ./implementer-remote-file-drive.sh

Run 2026-09-18, Relay built from the working tree, `ssh localhost` with a key.

| Shot | What it shows |
| --- | --- |
| `01-the-host-printed-a-path` | `ls /tmp/relay-remote-qa-…/service.conf` at the remote prompt. |
| `02-the-links-are-the-hosts` | The path underlined, with its tooltip, under a motionless pointer. It is a link because the host answered `test -e` for it over this same connection — the machine Relay runs on was never asked. |
| `03-open-from-the-host` | One click: a preview pane titled `localhost:/tmp/…/service.conf`, the `localhost` chip beside the title, a Save button, the file's four lines, syntax-highlighted. The tab reads `work; localhost:/tmp/relay-remote-qa…`. |
| `04-edited-not-yet-saved` | A line typed in. `●` in the pane header and in the tab. |
| `05-saved-over-ssh` | Ctrl+S: "Saved to localhost · 17:47:22", and the `●` is gone. |
| `06-the-host-has-the-new-bytes` | `cat` **on the host**, in the terminal pane, prints the edited file. The bytes really are there, written by the host's own `mv`. |
| `07-the-connection-ended` | `exit`, then the control master itself stopped (as closing a laptop lid would). A line typed in and Ctrl+S: "The connection to localhost has ended · your edits are safe in this pane. Log in to localhost again in the terminal pane and press Ctrl+S, or copy the text out." The buffer is untouched and the host's file still holds the earlier save. |

The file's mode was `640` before the run and `640` after it: the save writes a temporary file
beside it, `chmod --reference`s it and `mv`s it into place, so the mode and the inode's neighbours
are the host's, not Relay's.

Two things worth knowing, both by design:

- The host's answers arrive a moment after the output does, so the very first look at a fresh run
  of output has nothing to underline yet; the pointer resting on a path gets its underline when
  the batch comes back (about a fifth of a second here). The same goes for the first Ctrl+Shift+L
  on brand new output — a second press has the answers.
- `exit` alone does not end the shared connection: `ControlPersist` keeps the master alive for ten
  minutes, and a save over it still works, which is what a user who typed `exit` by accident
  wants. Shot 07 stops the master on purpose to photograph the refusal.

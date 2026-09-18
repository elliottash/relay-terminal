# Options and Actions side by side (#P2WD), and IBM Beige's louder pair (#K7VJ)

Implementer evidence, not a QA verdict. Everything here was taken by `drive.sh` under Xvfb with
an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, so a Relay running on the
real desktop cannot affect it and it writes nothing to the owner's profile. No provider account
and no agent turn: every scene is chrome.

```
docs/qa_evidence/2026-09-18-options-and-actions-side-by-side/drive.sh /abs/path/to/build [scene...]
```

Pass an **absolute** build directory: the driver runs Relay from inside the sandbox workspace.

## Both panes at once (#P2WD)

| Shot | What it shows |
|---|---|
| `implementer-both-ibm-beige-1-options.png` | Ctrl+, — Options alone; only the gear is lit. |
| `implementer-both-ibm-beige-2-both.png` | Ctrl+Shift+A after it — Options **and** Actions, side by side. The tab reads "project; Options; Actions · 3", and the bolt and the gear are both lit in their own header colours. Before this change the second key turned the first pane into the second. |
| `implementer-both-relay-dark-*.png` | The same pair on Relay Dark. |
| `implementer-close-ibm-beige-1-actions-gone.png` | With both open and Actions focused, Ctrl+Shift+A closes **Actions only**: Options is untouched and only the gear stays lit. |
| `implementer-close-ibm-beige-2-both-gone.png` | Ctrl+, then closes Options. |

The `-bar.png` files are the title-bar crop of the shot they are named after, at 200%, so the lit
buttons can be read.

## The destination colours on beige (#K7VJ)

| Shot | What it shows |
|---|---|
| `implementer-beige-1-shell.png` | A shell command in the composer: `grep` in the new `#0049a9`, on the beige chip face, next to ordinary near-black text. |
| `implementer-beige-2-agent.png` | The same box after `*`, the agent prefix: the caret in the new `#7500c3`. |
| `implementer-beige-3-both-panes.png` | Both tool panes open on IBM Beige, which is where the owner reported both problems. |

## Not covered here

- The contrast arithmetic behind the colours is a unit test, not a screenshot:
  `relay-theme-tests` and `relay-panestatus-tests`.
- A second window and a second tab lighting their own buttons is on the QA checklist in #P2WD;
  the driver takes one window.

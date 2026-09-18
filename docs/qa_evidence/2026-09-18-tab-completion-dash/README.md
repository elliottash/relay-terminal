# Tab completion left a character behind (2026-09-18)

Owner report: "when i did tab autocompelte and selected a folder, it added a \"-\" for some
reason: `cd 2026-09-18-EG/-` and then the command failed."

Reproduced with two folders that share a prefix, `2026-09-18-EG/` and `2026-09-18-EG-notes/`:

| Shot | What it shows |
|---|---|
| `01-tab-fills-the-common-prefix.png` | `cd 2026-09-18-E` + Tab. Tab fills in the candidates' common prefix, so the line now reads `cd 2026-09-18-EG`, and the list offers both folders. |
| `02-before-the-leftover-character.png` | Enter on the first item, before the fix: `cd 2026-09-18-EG/G`. The popup replaced the word as it was when it opened, which was one character shorter, so the `G` the common prefix had added stayed. The owner's folders diverged at a dash, so his leftover was `-`. |
| `03-after-first-item.png` | After the fix: `cd 2026-09-18-EG/`. |
| `04-after-second-item.png` | After the fix, second item: `cd 2026-09-18-EG-notes/`. |

Driven under Xvfb :187 with an isolated profile, in a throwaway workspace.
